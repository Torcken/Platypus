#ifndef PLATYPUS_LLM_BACKEND_HPP
#define PLATYPUS_LLM_BACKEND_HPP

#include <string>
#include <vector>
#include <memory>
#include "../util/result.hpp"
#include "../util/logger.hpp"

namespace platypus::llm {

/**
 * @brief Represents a single token or a chunk of text from the LLM.
 */
struct ChatChunk {
    std::string content;
    bool is_final = false;
    std::string role; // "assistant", "system", "user"
};

/**
 * @brief Interface for LLM backends.
 */
class LlmBackend {
public:
    virtual ~LlmBackend() = default;

    /**
     * @brief Generates a chat completion.
     * @param messages A list of messages (role, content).
     * @param temperature Controls randomness.
     * @param max_tokens Limits the output.
     * @return A result containing a list of chunks.
     */
    virtual util::Result<std::vector<ChatChunk>> generate_chat(
        const std::vector<std::pair<std::string, std::string>>& messages,
        float temperature,
        int max_tokens) = 0;

    /**
     * @brief Streams a chat completion.
     * @param messages A list of messages (role, content).
     * @param temperature Controls randomness.
     * @param max_tokens Limits the output.
     * @param callback A function to call for each received chunk.
     * @return A result indicating success or failure.
     */
    virtual util::Result<platypus::util::Void> stream_chat(
        const std::vector<std::pair<std::string, std::string>>& messages,
        float temperature,
        int max_tokens,
        std::function<void(const ChatChunk&)> callback) = 0;
};

} // namespace platypus::llm

#endif // PLATYPUS_LLM_BACKEND_HPP
