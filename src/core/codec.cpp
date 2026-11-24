// Extracts JSON tool calls from model text and repairs a small set of syntax mistakes.
// Repairs never invent names or arguments; schema validation happens in ToolRegistry.
#include "codec.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
namespace platypus::core {
namespace {
constexpr std::size_t kMaxResponseBytes = 64 * 1024;
constexpr std::size_t kMaxToolCalls = 64;
constexpr int kMaxJsonDepth = 64;

/// Reject duplicate keys and stop deep input before constructing an unbounded JSON tree.
nlohmann::json checked_json(const std::string &text) {
    if (text.size() > kMaxResponseBytes)
        throw std::runtime_error("JSON input exceeds parser limit");
    std::map<int, std::set<std::string>> keys;
    using Event = nlohmann::json::parse_event_t;
    return nlohmann::json::parse(text, [&](int depth, Event event, nlohmann::json &value) {
        if (depth > kMaxJsonDepth ||
            ((event == Event::object_start || event == Event::array_start) &&
             depth >= kMaxJsonDepth))
            throw std::runtime_error("JSON nesting exceeds parser limit");
        if (event == Event::object_start)
            keys[depth + 1].clear();
        if (event == Event::key && !keys[depth].insert(value.get<std::string>()).second)
            throw std::runtime_error("Duplicate JSON key");
        return true;
    });
}

/// Native tool/argument names use the ASCII identifier subset from the published templates.
bool valid_name(const std::string &name) {
    auto first = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    return !name.empty() && first(name.front()) &&
           std::all_of(name.begin(), name.end(),
                       [&](char c) { return first(c) || (c >= '0' && c <= '9'); });
}

/// Scan literal tags without std::regex recursion on long model-supplied argument strings.
std::vector<ToolCall> parse_qwen(const std::string &text, const nlohmann::json &schemas) {
    std::vector<ToolCall> calls;
    std::size_t cursor = 0;
    while ((cursor = text.find("<function=", cursor)) != std::string::npos) {
        auto name_end = text.find('>', cursor);
        auto end = text.find("</function>", name_end);
        if (name_end == std::string::npos || end == std::string::npos ||
            calls.size() >= kMaxToolCalls)
            return {};
        auto name = text.substr(cursor + 10, name_end - cursor - 10);
        if (!valid_name(name))
            return {};
        auto args = nlohmann::json::object();
        auto position = name_end + 1;
        while ((position = text.find_first_not_of(" \t\r\n", position)) < end) {
            if (text.compare(position, 11, "<parameter=") != 0)
                return {};
            auto key_end = text.find('>', position);
            auto value_end = text.find("</parameter>", key_end);
            if (key_end >= end || value_end >= end)
                return {};
            auto key = text.substr(position + 11, key_end - position - 11);
            if (!valid_name(key) || args.contains(key))
                return {};
            auto value_text = text.substr(key_end + 1, value_end - key_end - 1);
            if (value_text.starts_with('\n'))
                value_text.erase(0, 1);
            if (value_text.ends_with('\n'))
                value_text.pop_back();
            auto kind = schemas.value(name, nlohmann::json::object())
                            .value("properties", nlohmann::json::object())
                            .value(key, nlohmann::json::object())
                            .value("type", "");
            nlohmann::json value = value_text;
            if (kind != "string") {
                try {
                    value = checked_json(value_text);
                } catch (const nlohmann::json::parse_error &) {
                } // Non-JSON text remains a string.
            }
            args[key] = std::move(value);
            position = value_end + 12;
        }
        calls.push_back({name, std::move(args)});
        cursor = end + 11;
    }
    return calls;
}

/// Convert Gemma's delimited string values and bare keys into bounded JSON.
nlohmann::json gemma_arguments(const std::string &text) {
    std::string json;
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 5, "<|\"|>") == 0) {
            auto close = text.find("<|\"|>", i + 5);
            if (close == std::string::npos)
                throw std::runtime_error("Unclosed native string");
            json += nlohmann::json(text.substr(i + 5, close - i - 5)).dump();
            i = close + 5;
        } else if (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_') {
            auto begin = i;
            while (i < text.size() &&
                   (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_'))
                ++i;
            auto word = text.substr(begin, i - begin);
            auto next = text.find_first_not_of(" \t\r\n", i);
            json +=
                next != std::string::npos && text[next] == ':' ? nlohmann::json(word).dump() : word;
        } else
            json += text[i++];
    }
    return checked_json(json);
}

/// Parse all calls in a native batch; a malformed member rejects the whole batch.
std::vector<ToolCall> parse_gemma(const std::string &text) {
    std::vector<ToolCall> calls;
    std::size_t cursor = 0;
    while ((cursor = text.find("<|tool_call>", cursor)) != std::string::npos) {
        if (text.compare(cursor, 17, "<|tool_call>call:") != 0)
            return {};
        auto end = text.find("<tool_call|>", cursor);
        auto brace = text.find('{', cursor);
        if (end == std::string::npos || brace >= end || calls.size() >= kMaxToolCalls)
            return {};
        auto name = text.substr(cursor + 17, brace - cursor - 17);
        auto last = name.find_last_not_of(" \t\r\n");
        if (last != std::string::npos)
            name.resize(last + 1);
        auto args = gemma_arguments(text.substr(brace, end - brace));
        if (!valid_name(name) || !args.is_object())
            return {};
        calls.push_back({std::move(name), std::move(args)});
        cursor = end + 12;
    }
    return calls;
}
} // namespace

std::vector<ToolCall> Codec::parse_tool_calls(const std::string &response,
                                              const std::string &syntax,
                                              const nlohmann::json &schemas) try {
    if (response.size() > kMaxResponseBytes)
        return {};
    // Native failures must not reinterpret JSON embedded in a malformed argument as a new call.
    std::vector<ToolCall> calls;
    if (syntax == "qwen_xml" && response.find("<function=") != std::string::npos)
        calls = parse_qwen(response, schemas);
    else if (syntax == "gemma" && response.find("<|tool_call>") != std::string::npos)
        calls = parse_gemma(response);
    else {
        auto begin = response.find('{');
        if (begin != std::string::npos)
            calls = parse_json(response.substr(begin));
    }
    // Reject invalid UTF-8 in native arguments rather than changing model-requested data.
    for (const auto &call : calls)
        static_cast<void>(call.arguments.dump());
    return calls;
} catch (const std::exception &) {
    return {};
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
        if (closing.size() > static_cast<std::size_t>(kMaxJsonDepth))
            return {};
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
        auto value = checked_json(repaired);
        nlohmann::json calls = nlohmann::json::array();
        if (value.contains("tool_call"))
            calls.push_back(value.at("tool_call"));
        else if (value.contains("tool_calls"))
            calls = value.at("tool_calls");
        else if (value.contains("plan"))
            calls = value.at("plan");
        else if (value.contains("name"))
            calls.push_back(value);
        if (!calls.is_array() || calls.size() > kMaxToolCalls)
            return {};
        std::vector<ToolCall> result;
        for (auto call : calls) {
            if (call.contains("function"))
                call = call.at("function");
            auto name = call.value("name", call.value("tool_name", std::string{}));
            auto args = call.value("arguments", nlohmann::json::object());
            if (args.is_string())
                args = checked_json(args.get<std::string>());
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
