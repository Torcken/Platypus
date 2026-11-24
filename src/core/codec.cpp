// Extracts JSON tool calls from model text and repairs a small set of syntax mistakes.
// Repairs never invent names or arguments; schema validation happens in ToolRegistry.
#include "codec.hpp"
#include <cctype>
#include <regex>
namespace platypus::core {
std::vector<ToolCall> Codec::parse_tool_calls(const std::string &response,
                                              const std::string &syntax,
                                              const nlohmann::json &schemas) {
    if (syntax == "qwen_xml") {
        std::vector<ToolCall> result;
        std::regex function(R"(<function=([A-Za-z_][A-Za-z_0-9]*)>([\s\S]*?)</function>)");
        for (auto it = std::sregex_iterator(response.begin(), response.end(), function);
             it != std::sregex_iterator(); ++it) {
            auto args = nlohmann::json::object();
            auto body = (*it)[2].str();
            std::regex parameter(R"(<parameter=([A-Za-z_][A-Za-z_0-9]*)>([\s\S]*?)</parameter>)");
            for (auto item = std::sregex_iterator(body.begin(), body.end(), parameter);
                 item != std::sregex_iterator(); ++item) {
                auto text = (*item)[2].str();
                if (text.starts_with('\n'))
                    text.erase(0, 1);
                if (text.ends_with('\n'))
                    text.pop_back();
                auto property = (*item)[1].str();
                bool string_value = schemas.contains((*it)[1].str()) &&
                                    schemas.at((*it)[1].str())
                                            .value("properties", nlohmann::json::object())
                                            .value(property, nlohmann::json::object())
                                            .value("type", "") == "string";
                auto value = string_value ? nlohmann::json(text)
                                          : nlohmann::json::parse(text, nullptr, false);
                args[(*item)[1].str()] = value.is_discarded() ? nlohmann::json(text) : value;
            }
            result.push_back({(*it)[1].str(), args});
        }
        if (!result.empty())
            return result;
    }
    if (syntax == "gemma") {
        auto start = response.find("<|tool_call>call:");
        auto end = response.find("<tool_call|>", start);
        auto brace = response.find('{', start);
        if (start != std::string::npos && end != std::string::npos && brace < end) {
            auto name = response.substr(start + 17, brace - start - 17);
            auto text = response.substr(brace, end - brace);
            std::string json;
            for (std::size_t i = 0; i < text.size();) {
                if (text.compare(i, 5, "<|\"|>") == 0) {
                    auto close = text.find("<|\"|>", i + 5);
                    if (close == std::string::npos)
                        return {};
                    json += nlohmann::json(text.substr(i + 5, close - i - 5)).dump();
                    i = close + 5;
                } else if (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_') {
                    auto begin = i;
                    while (i < text.size() &&
                           (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_'))
                        ++i;
                    auto word = text.substr(begin, i - begin);
                    auto next = text.find_first_not_of(" \t\r\n", i);
                    json += next != std::string::npos && text[next] == ':'
                                ? nlohmann::json(word).dump()
                                : word;
                } else
                    json += text[i++];
            }
            auto args = nlohmann::json::parse(json, nullptr, false);
            if (args.is_object() && !name.empty())
                return {{name, args}};
        }
    }
    auto begin = response.find('{');
    if (begin == std::string::npos)
        return {};
    return parse_json(response.substr(begin));
}
std::vector<ToolCall> Codec::parse_json(const std::string &text) {
    std::string repaired;
    std::vector<char> closing;
    bool quoted = false, escaped = false;
    for (char c : text) {
        if (quoted) {
            repaired += c;
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
            continue;
        }
        if (c == '"')
            quoted = true;
        if (c == '{')
            closing.push_back('}');
        if (c == '[')
            closing.push_back(']');
        if (c == '}' || c == ']') {
            if (closing.empty() || closing.back() != c)
                return {};
            while (!repaired.empty() && std::isspace(static_cast<unsigned char>(repaired.back())))
                repaired.pop_back();
            if (!repaired.empty() && repaired.back() == ',')
                repaired.pop_back();
            closing.pop_back();
        }
        repaired += c;
        if (closing.empty())
            break;
    }
    if (quoted) {
        if (escaped)
            repaired += '\\';
        repaired += '"';
    }
    while (!closing.empty()) {
        repaired += closing.back();
        closing.pop_back();
    }
    try {
        auto value = nlohmann::json::parse(repaired);
        nlohmann::json calls = nlohmann::json::array();
        if (value.contains("tool_call"))
            calls.push_back(value.at("tool_call"));
        else if (value.contains("tool_calls"))
            calls = value.at("tool_calls");
        else if (value.contains("plan"))
            calls = value.at("plan");
        else if (value.contains("name"))
            calls.push_back(value);
        if (!calls.is_array())
            return {};
        std::vector<ToolCall> result;
        for (auto call : calls) {
            if (call.contains("function"))
                call = call.at("function");
            auto name = call.value("name", call.value("tool_name", std::string{}));
            auto args = call.value("arguments", nlohmann::json::object());
            if (args.is_string())
                args = nlohmann::json::parse(args.get<std::string>());
            if (name.empty() || !args.is_object())
                return {};
            result.push_back({name, args});
        }
        return result;
    } catch (const nlohmann::json::exception &) {
        return {};
    }
}
} // namespace platypus::core
