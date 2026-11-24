#ifndef PLATYPUS_LLM_CHAT_FORMATTER_HPP
#define PLATYPUS_LLM_CHAT_FORMATTER_HPP

#include "../util/result.hpp"
#include "model_profile.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace platypus::llm {

/**
 * @brief Utility to format message history for different LLM backend requirements.
 */
class ChatFormatter {
  public:
    /**
     * @brief Formats the conversation into a structured JSON-like list of message objects.
     * @param messages A list of (role, content) pairs.
     * @return A JSON string representing the conversation.
     */
    /// Remove a leading reasoning channel for tool parsing; literal markers inside arguments remain
    /// data.
    static util::Result<std::string> visible_response(const std::string &response,
                                                      const ModelProfile &profile);
    /// Serialize a tool result for the append-only log without treating it as a user instruction.
    static std::string tool_observation(const std::string &name, const std::string &content,
                                        bool is_error);
    /// Render one syntax family's call example and fixed native tool declarations.
    static std::string tool_call_instruction(const std::string &syntax);
    static std::string tool_definitions(const nlohmann::json &tools, const std::string &syntax);
    static std::string render(const std::vector<std::pair<std::string, std::string>> &messages,
                              const ModelProfile &profile);
    static std::string to_json(const std::vector<std::pair<std::string, std::string>> &messages);

    /**
     * @brief Formats the conversation into a raw string, useful for models that don't support
     * explicit message objects and expect a single prompt.
     * @param messages A list of (role, content) pairs.
     * @param system_prompt The system prompt to prepend.
     * @return A formatted prompt string.
     */
    static std::string
    to_raw_string(const std::vector<std::pair<std::string, std::string>> &messages,
                  const std::string &system_prompt = "");
};

} // namespace platypus::llm

#endif // PLATYPUS_LLM_CHAT_FORMATTER_HPP
