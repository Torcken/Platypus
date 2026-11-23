#ifndef PLATYPUS_CORE_CODEC_HPP
#define PLATYPUS_CORE_CODEC_HPP

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>
#include "tool.hpp"

namespace platypus::core {

struct ToolCall {
    std::string tool_name;
    nlohmann::json arguments;
};

class Codec {
public:
    /**
     * @brief Parses a raw LLM response to identify tool calls.
     * 
     * Supports:
     * 1. JSON format: {"tool_calls": [{"name": "...", "arguments": {...}}]}
     * 2. Raw text: "Call tool: name, args: {...}" (Simplified)
     * 
     * @param response The raw string output from the LLM backend.
     * @return A vector of ToolCall objects.
     */
    static std::vector<ToolCall> parse_tool_calls(const std::string& response);

private:
    static std::vector<ToolCall> parse_json(const std::string& json_str);
};

} // namespace platypus::core

#endif // PLATYPUS_CORE_CODEC_HPP
