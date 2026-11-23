#ifndef PLATYPUS_LLM_LLAMA_SERVER_BACKEND_HPP
#define PLATYPUS_LLM_LLAMA_SERVER_BACKEND_HPP

#include "backend.hpp"
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

    util::Result<std::vector<ChatChunk>> generate_chat(
        const std::vector<std::pair<std::string, std::string>>& messages,
        float temperature,
        int max_tokens) override;

    util::Result<platypus::util::Void> stream_chat(
        const std::vector<std::pair<std::string, std::string>>& messages,
        float temperature,
        int max_tokens,
        std::function<void(const ChatChunk&)> callback) override;

private:
    std::string base_url_;

    // Helper methods for network communication (to be implemented in .cpp)
    util::Result<std::string> post_request(const std::string& endpoint, const std::string& json_payload);
    util::Result<platypus::util::Void> stream_request(const std::string& endpoint, const std::string& json_payload, 
                                                       std::function<void(const std::string&)> chunk_callback);
};

} // namespace platypus::llm

#endif // PLATYPUS_LLM_LLAMA_SERVER_BACKEND_HPP
