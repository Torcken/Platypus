#ifndef PLATYPUS_LLM_LLAMA_SERVER_BACKEND_HPP
#define PLATYPUS_LLM_LLAMA_SERVER_BACKEND_HPP

#include "backend.hpp"
#include <optional>
#include <string>
#include <vector>

namespace platypus::llm {

/**
 * @brief Backend that communicates with a llama.cpp server.
 */
class LlamaServerBackend : public LlmBackend {
  public:
    /**
     * @param base_url The base URL of the llama.cpp server (e.g., "http://localhost:8080").
     */
    LlamaServerBackend(std::string base_url = "http://localhost:8080");

    /// Add opt-in proxy metadata without modifying the model request or prompt.
    util::Result<CompletionResult> complete(const CompletionRequest &request,
                                            TokenCallback on_token, std::stop_token stop) override;
    /// Tokenize the exact rendered raw prompt, including model-configured special tokens.
    util::Result<PromptSize>
    count_prompt_tokens(const std::vector<std::pair<std::string, std::string>> &messages,
                        std::stop_token stop) override;

    util::Result<std::vector<ChatChunk>>
    generate_chat(const std::vector<std::pair<std::string, std::string>> &messages,
                  float temperature = 0.7f, int max_tokens = 4096) override;

    util::Result<platypus::util::Void>
    stream_chat(const std::vector<std::pair<std::string, std::string>> &messages, float temperature,
                int max_tokens, std::function<void(const ChatChunk &)> callback) override;

    void set_profile(ModelProfile profile) { profile_ = std::move(profile); }
    const ModelProfile *profile() const override { return profile_ ? &*profile_ : nullptr; }
    /// Checks readiness using the pinned server health endpoint.
    util::Result<std::string> health();
    /// Tokenize with the selected server tokenizer, including special-token parsing.
    util::Result<std::vector<int>> tokenize(const std::string &content);

  private:
    std::string base_url_;
    std::optional<ModelProfile> profile_;
    bool benchmark_metadata_enabled_ = false;
    std::string benchmark_metadata_;

    // Helper methods for network communication (to be implemented in .cpp)
    util::Result<std::string> post_request(const std::string &endpoint,
                                           const std::string &json_payload);
    util::Result<platypus::util::Void>
    stream_request(const std::string &endpoint, const std::string &json_payload,
                   std::function<void(const std::string &)> chunk_callback);
};

} // namespace platypus::llm

#endif // PLATYPUS_LLM_LLAMA_SERVER_BACKEND_HPP
