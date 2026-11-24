#ifndef PLATYPUS_LLM_BACKEND_HPP
#define PLATYPUS_LLM_BACKEND_HPP

#include "../util/logger.hpp"
#include "../util/result.hpp"
#include "model_profile.hpp"
#include <memory>
#include <nlohmann/json.hpp>
#include <stop_token>
#include <string>
#include <vector>

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
struct CompletionRequest {
    std::vector<std::pair<std::string, std::string>> messages;
    float temperature = 0.3f;
    int max_tokens = 2048;
    /// Schemas already rendered in the prompt; optional benchmark instrumentation uses these.
    nlohmann::json tools = nlohmann::json::array();
};
struct CompletionResult {
    std::string content;
};
using TokenCallback = std::function<void(const std::string &)>;
class LlmBackend {
  public:
    virtual ~LlmBackend() = default;
    virtual const ModelProfile *profile() const { return nullptr; }
    virtual util::Result<CompletionResult> complete(const CompletionRequest &request,
                                                    TokenCallback on_token, std::stop_token stop) {
        set_stop_token(stop);
        std::string content;
        if (on_token) {
            auto result = stream_chat(request.messages, request.temperature, request.max_tokens,
                                      [&](const ChatChunk &chunk) {
                                          content += chunk.content;
                                          on_token(chunk.content);
                                      });
            if (!result.is_ok())
                return util::Result<CompletionResult>::fail(result.error());
        } else {
            auto result = generate_chat(request.messages, request.temperature, request.max_tokens);
            if (!result.is_ok())
                return util::Result<CompletionResult>::fail(result.error());
            if (result.value().empty())
                return util::Result<CompletionResult>::fail("Empty completion");
            for (const auto &chunk : result.value())
                content += chunk.content;
        }
        return util::Result<CompletionResult>::ok({content});
    }
    virtual void set_stop_token(std::stop_token stop) { stop_ = stop; }

    /**
     * @brief Generates a chat completion.
     * @param messages A list of messages (role, content).
     * @param temperature Controls randomness.
     * @param max_tokens Limits the output.
     * @return A result containing a list of chunks.
     */
    virtual util::Result<std::vector<ChatChunk>>
    generate_chat(const std::vector<std::pair<std::string, std::string>> &messages,
                  float temperature, int max_tokens) = 0;

    /**
     * @brief Streams a chat completion.
     * @param messages A list of messages (role, content).
     * @param temperature Controls randomness.
     * @param max_tokens Limits the output.
     * @param callback A function to call for each received chunk.
     * @return A result indicating success or failure.
     */
    virtual util::Result<platypus::util::Void>
    stream_chat(const std::vector<std::pair<std::string, std::string>> &messages, float temperature,
                int max_tokens, std::function<void(const ChatChunk &)> callback) = 0;

  protected:
    std::stop_token stop_;
};

} // namespace platypus::llm

#endif // PLATYPUS_LLM_BACKEND_HPP
