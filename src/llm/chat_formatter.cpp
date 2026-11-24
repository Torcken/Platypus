// Renders stable prompts, native tool definitions and persisted tool observations.
// Syntax-family handling follows the publisher templates cited by the shipped profiles.
// The backend uses this module for raw prompts and generic chat JSON messages.
#include "chat_formatter.hpp"
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <sstream>

namespace platypus::llm {
namespace {
using History = std::vector<std::pair<std::string, std::string>>;

/// Substitute a role into the profile's fixed turn prefix without changing other bytes.
std::string turn_prefix(const ModelProfile &profile, const std::string &role) {
    auto prefix = profile.turn_prefix;
    auto marker = prefix.find("{role}");
    if (marker == std::string::npos)
        throw std::runtime_error("Profile turn_prefix requires {role}");
    prefix.replace(marker, 6, role == "assistant" ? profile.assistant_role : role);
    return prefix;
}

/// Recover a typed tool observation; corrupted session data is refused rather than guessed.
nlohmann::json observation(const std::string &content) {
    auto value = nlohmann::json::parse(content);
    if (!value.is_object() || !value.at("name").is_string() || !value.at("content").is_string() ||
        !value.at("is_error").is_boolean() ||
        (value.contains("truncated") && !value.at("truncated").is_boolean()))
        throw std::runtime_error("Invalid tool observation");
    return value;
}

/// Preserve error status when the native template accepts a string-valued tool response.
std::string observation_text(const nlohmann::json &value) {
    return (value.at("is_error").get<bool>() ? "Error: " : "") +
           value.at("content").get<std::string>();
}

/// Format Gemma's string values using its published special quote delimiters.
std::string gemma_value(const nlohmann::json &value) {
    if (value.is_string())
        return "<|\"|>" + value.get<std::string>() + "<|\"|>";
    if (value.is_array()) {
        std::string result = "[";
        for (const auto &item : value) {
            if (result.size() > 1)
                result += ',';
            result += gemma_value(item);
        }
        return result + ']';
    }
    return value.dump();
}

/// Render flat primitive schemas; every shipped tool uses this deliberately small schema subset.
std::string gemma_parameters(const nlohmann::json &schema) {
    std::string result = "{properties:{";
    bool first = true;
    auto properties = schema.value("properties", nlohmann::json::object());
    for (const auto &[name, property] : properties.items()) {
        if (!first)
            result += ',';
        first = false;
        auto type = property.at("type").get<std::string>();
        std::transform(type.begin(), type.end(), type.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        result += name + ":{";
        if (property.contains("description"))
            result += "description:" + gemma_value(property.at("description")) + ',';
        result += "type:" + gemma_value(type) + '}';
    }
    result += "},required:" + gemma_value(schema.value("required", nlohmann::json::array()));
    return result + ",type:<|\"|>OBJECT<|\"|>}";
}

/// Qwen groups consecutive tool results inside one user turn with native response blocks.
std::string render_qwen(const History &messages, const ModelProfile &profile) {
    std::string result;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        const auto &[role, content] = messages[i];
        if (role == "tool") {
            if (i == 0 || messages[i - 1].first != "tool")
                result += turn_prefix(profile, "user");
            else
                result += '\n';
            result +=
                "<tool_response>\n" + observation_text(observation(content)) + "\n</tool_response>";
            if (i + 1 == messages.size() || messages[i + 1].first != "tool")
                result += profile.turn_suffix;
        } else {
            result += turn_prefix(profile, role);
            if (role == "assistant")
                result += profile.generation_suffix;
            result += content + profile.turn_suffix;
        }
    }
    return result + turn_prefix(profile, "assistant") + profile.generation_suffix;
}

/// Gemma continues the same model turn across tool calls, responses and the next generation.
std::string render_gemma(const History &messages, const ModelProfile &profile) {
    std::string result, previous_role;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        const auto &[role, content] = messages[i];
        if (role == "tool") {
            if (previous_role != "assistant")
                throw std::runtime_error("Tool response has no model turn");
            auto value = observation(content);
            result += "<|tool_response>response:" + value.at("name").get<std::string>() +
                      "{value:" + gemma_value(observation_text(value)) + "}<tool_response|>";
            if (i + 1 < messages.size() && messages[i + 1].first != "tool" &&
                messages[i + 1].first != "assistant")
                result += profile.turn_suffix;
            continue;
        }
        bool continuation = role == "assistant" && previous_role == "assistant";
        if (!continuation) {
            result += turn_prefix(profile, role);
            if (role == "assistant")
                result += profile.generation_suffix;
        }
        result += content;
        bool next_tool = i + 1 < messages.size() && messages[i + 1].first == "tool";
        bool next_assistant = i + 1 < messages.size() && messages[i + 1].first == "assistant";
        if (!(role == "assistant" && (next_tool || next_assistant)))
            result += profile.turn_suffix;
        previous_role = role;
    }
    if (!messages.empty() && messages.back().first == "tool")
        return result;
    return result + turn_prefix(profile, "assistant") + profile.generation_suffix;
}
} // namespace

util::Result<std::string> ChatFormatter::visible_response(const std::string &response,
                                                          const ModelProfile &profile) {
    using Return = util::Result<std::string>;
    if (profile.reasoning_open.empty() || profile.reasoning_close.empty())
        return Return::ok(response);
    auto first = response.find_first_not_of(" \t\r\n");
    auto suffix_end = profile.generation_suffix.find_last_not_of(" \t\r\n");
    auto suffix = suffix_end == std::string::npos
                      ? std::string{}
                      : profile.generation_suffix.substr(0, suffix_end + 1);
    bool explicit_open =
        first != std::string::npos &&
        response.compare(first, profile.reasoning_open.size(), profile.reasoning_open) == 0;
    bool implicit_open = suffix.ends_with(profile.reasoning_open);
    if (!explicit_open && !implicit_open)
        return Return::ok(response);
    auto close = response.find(profile.reasoning_close,
                               explicit_open ? first + profile.reasoning_open.size() : 0);
    if (close == std::string::npos)
        return Return::fail("Incomplete reasoning channel; no tools executed");
    return Return::ok(response.substr(close + profile.reasoning_close.size()));
}

std::string ChatFormatter::tool_observation(const std::string &name, const std::string &content,
                                            bool is_error, bool truncated) {
    return nlohmann::json{
        {"name", name}, {"content", content}, {"is_error", is_error}, {"truncated", truncated}}
        .dump();
}

std::string ChatFormatter::tool_call_instruction(const std::string &syntax) {
    if (syntax == "qwen_xml")
        return "Use "
               "<tool_call><function=read><parameter=path>file</parameter></function></tool_call>.";
    if (syntax == "gemma")
        return "Use <|tool_call>call:read{path:<|\"|>file<|\"|>}<tool_call|>.";
    return "Use JSON: {\"tool_call\":{\"name\":\"read\",\"arguments\":{\"path\":\"file\"}}}.";
}

std::string ChatFormatter::tool_definitions(const nlohmann::json &tools,
                                            const std::string &syntax) {
    if (syntax == "gemma") {
        std::string result;
        for (const auto &tool : tools) {
            const auto &function = tool.at("function");
            result += "<|tool>declaration:" + function.at("name").get<std::string>() +
                      "{description:" + gemma_value(function.at("description")) +
                      ",parameters:" + gemma_parameters(function.at("parameters")) + "}<tool|>";
        }
        return result;
    }
    std::string result = syntax == "qwen_xml" ? "# Tools\n\n<tools>\n" : "Tools:\n";
    for (const auto &tool : tools)
        result += tool.dump() + '\n';
    if (syntax == "qwen_xml")
        result += "</tools>\n";
    return result;
}

std::string ChatFormatter::render(const History &messages, const ModelProfile &profile) {
    if (profile.tool_syntax == "qwen_xml")
        return render_qwen(messages, profile);
    if (profile.tool_syntax == "gemma")
        return render_gemma(messages, profile);
    std::string result;
    for (const auto &[role, content] : messages) {
        result += turn_prefix(profile, role == "tool" ? "user" : role);
        if (role == "assistant")
            result += profile.generation_suffix;
        result += (role == "tool" ? observation_text(observation(content)) : content) +
                  profile.turn_suffix;
    }
    return result + turn_prefix(profile, "assistant") + profile.generation_suffix;
}

std::string ChatFormatter::to_json(const History &messages) {
    nlohmann::json formatted = nlohmann::json::array();
    for (const auto &[role, content] : messages) {
        if (role == "tool") {
            auto value = observation(content);
            formatted.push_back({{"role", "user"},
                                 {"content", "Tool " + value.at("name").get<std::string>() +
                                                 " output: " + observation_text(value)}});
        } else
            formatted.push_back({{"role", role}, {"content", content}});
    }
    return formatted.dump();
}

std::string ChatFormatter::to_raw_string(const History &messages,
                                         const std::string &system_prompt) {
    std::string result = system_prompt.empty() ? "" : "System: " + system_prompt + "\n\n";
    for (const auto &[role, content] : messages)
        result += role + ": " + content + '\n';
    return result;
}
} // namespace platypus::llm
