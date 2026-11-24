#ifndef PLATYPUS_CORE_CODEC_HPP
#define PLATYPUS_CORE_CODEC_HPP

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

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
     * 2. Native Qwen XML and Gemma tool-call syntax selected by the profile.
     *
     * @param response The raw string output from the LLM backend.
     * @return A vector of ToolCall objects.
     * Malformed native batches, excessive nesting and responses over 64 KiB return no calls.
     */
    static std::vector<ToolCall>
    parse_tool_calls(const std::string &response, const std::string &syntax = "json_tagged",
                     const nlohmann::json &schemas = nlohmann::json::object());

  private:
    static std::vector<ToolCall> parse_json(const std::string &json_str);
};

} // namespace platypus::core

#endif // PLATYPUS_CORE_CODEC_HPP
