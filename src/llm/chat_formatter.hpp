#ifndef PLATYPUS_LLM_CHAT_FORMATTER_HPP
#define PLATYPUS_LLM_CHAT_FORMATTER_HPP

#include "../util/result.hpp"
#include "model_profile.hpp"
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
