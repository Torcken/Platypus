#include "codec.hpp"
#include <iostream>
#include <regex>

namespace platypus::core {

std::vector<ToolCall> Codec::parse_tool_calls(const std::string& response) {
    std::vector<ToolCall> tool_calls;

    // Attempt JSON parsing first
    try {
        auto j = nlohmann::json::parse(response);
        if (j.contains("tool_calls") && j["tool_calls"].is_array()) {
            for (const auto& tc : j["tool_calls"]) {
                ToolCall call;
                call.tool_name = tc.value("name", "");
                call.arguments = tc.value("arguments", nlohmann::json::object());
                if (!call.tool_name.empty()) {
                    tool_calls.push_back(call);
                }
            }
            return tool_calls;
        }
    } catch (const nlohmann::json::parse_error& e) {
        // Not valid JSON, fall back to regex/heuristic
    }

    // Fallback: Simple regex for "Call tool: name, args: {...}"
    // This is a simplified heuristic for non-JSON outputs
    std::regex tool_regex(R"(Call tool:\s*(\w+),\s*args:\s*(\{.*?\}))");
    std::smatch match;
    std::string search_str = response;

    while (std::regex_search(search_str, match, tool_regex)) {
        ToolCall call;
        call.tool_name = match[1].str();
        try {
            call.arguments = nlohmann::json::parse(match[2].str());
            tool_calls.push_back(call);
        } catch (...) {
            // If arguments aren't valid JSON, skip this match
        }
        search_str = match.suffix().str();
    }

    return tool_calls;
}

std::vector<ToolCall> Codec::parse_json(const std::string& json_str) {
    // This was intended to be the helper for explicit JSON paths, 
    // but we currently do it inline in parse_tool_calls for simplicity.
    return {};
}

} // namespace platypus::core
