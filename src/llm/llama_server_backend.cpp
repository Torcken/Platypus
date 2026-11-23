#include "llama_server_backend.hpp"
#include "../util/logger.hpp"
#include <sstream>
#include <iostream>
#include <cstring>

namespace platypus::llm {

LlamaServerBackend::LlamaServerBackend(std::string base_url) : base_url_(std::move(base_url)) {}

platypus::util::Result<std::string> LlamaServerBackend::post_request(const std::string& endpoint, const std::string& json_payload) {
    // In a production environment, we would use a library like libcurl or a socket-based implementation.
    // For this phase, we will simulate the network call to the llama.cpp server.
    // We will output a mock success for now.

    platypus::util::Logger::get_instance().debug("POST request to " + base_url_ + endpoint + " with payload: " + json_payload);

    // Placeholder: Simulation of a successful response from llama.cpp /v1/chat/completions
    return {"{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Mock response from llama.cpp server.\"},\"finish_reason\":\"stop\"}]}]"};
}

platypus::util::Result<platypus::util::Void> LlamaServerBackend::stream_request(const std::string& endpoint, const std::string& json_payload, 
                                                                               std::function<void(const std::string&)> chunk_callback) {
    // In a production environment, we would use a library like libcurl or a socket-based implementation.
    // For this phase, we will simulate the streaming response from the llama.cpp server.
    
    platypus::util::Logger::get_instance().debug("Streaming request to " + base_url_ + endpoint + " with payload: " + json_payload);
    
    // Placeholder: Simulation of streaming chunks
    std::vector<std::string> mock_chunks = {
        "data: {\"choices\":[{\"delta\":{\"content\":\"Mock \"}}],\"finish_reason\":null}",
        "data: {\"choices\":[{\"delta\":{\"content\":\"response \"}}],\"finish_reason\":null}",
        "data: {\"choices\":[{\"delta\":{\"content\":\"from \"}}],\"finish_reason\":null}",
        "data: {\"choices\":[{\"delta\":{\"content\":\"server.\"}}],\"finish_reason\":\"stop\"}"
    };

    for (const auto& chunk : mock_chunks) {
        chunk_callback(chunk);
    }

    return platypus::util::Result<platypus::util::Void>::ok();
}

platypus::util::Result<std::vector<ChatChunk>> LlamaServerBackend::generate_chat(
    const std::vector<std::pair<std::string, std::string>>& messages,
    float temperature,
    int max_tokens) {
    
    // Simplified JSON construction
    std::stringstream ss;
    ss << "{\"messages\":[";
    for (size_t i = 0; i < messages.size(); ++i) {
        ss << "{\"role\":\"" << messages[i].first << "\",\"content\":\"" << messages[i].second << "\"}";
        if (i < messages.size() - 1) ss << ",";
    }
    ss << "],\"temperature\":" << temperature << ",\"max_tokens\":" << max_tokens << "}";

    auto result = post_request("/v1/chat/completions", ss.str());
    if (result.is_err()) return platypus::util::Result<std::vector<ChatChunk>>::fail(result.error());

    // Extremely simplified JSON parsing (not using a real JSON library)
    // Looking for the "content" value in a basic way for this placeholder
    std::string content = "Mock response from llama.cpp server.";
    
    std::vector<ChatChunk> chunks;
    chunks.push_back({content, true, "assistant"});
    return chunks;
}

platypus::util::Result<platypus::util::Void> LlamaServerBackend::stream_chat(
    const std::vector<std::pair<std::string, std::string>>& messages,
    float temperature,
    int max_tokens,
    std::function<void(const ChatChunk&)> callback) {
    
    std::stringstream ss;
    ss << "{\"messages\":[";
    for (size_t i = 0; i < messages.size(); ++i) {
        ss << "{\"role\":\"" << messages[i].first << "\",\"content\":\"" << messages[i].second << "\"}";
        if (i < messages.size() - 1) ss << ",";
    }
    ss << "],\"temperature\":" << temperature << ",\"max_tokens\":" << max_tokens << "}";

    auto result = stream_request("/v1/chat/completions", ss.str(), [&](const std::string& chunk) {
        // Basic parsing of the SSE "data: " prefix
        std::string content = chunk;
        if (content.find("data: ") == 0) {
            content = content.substr(6);
        }
        
        // Further parsing of the JSON inside the chunk would happen here
        // For now, we just pass it through as a chunk
        callback({content, chunk.find("finish_reason\":\"stop\"") != std::string::npos, "assistant"});
    });

    return result;
}

} // namespace platypus::llm
