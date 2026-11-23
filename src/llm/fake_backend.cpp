#include "fake_backend.hpp"
#include "../util/logger.hpp"
#include <thread>
#include <chrono>
#include <algorithm>

namespace platypus::llm {

FakeBackend::FakeBackend() {
    // Default fallback responses
    scripts_["hello"] = {"Hello! How can I help you today?"};
}

void FakeBackend::set_script(const std::string& key, const std::vector<std::string>& responses) {
    scripts_[key] = responses;
    turn_counters_[key] = 0;
}

util::Result<std::vector<ChatChunk>> FakeBackend::generate_chat(
    const std::vector<std::pair<std::string, std::string>>& messages,
    float temperature,
    int max_tokens) {
    
    std::string response_text = "Default response for mock backend.";
    
    // Check if any message content matches a script key
    for (const auto& [role, content] : messages) {
        for (auto const& [key, _] : scripts_) {
            if (content.find(key) != std::string::npos) {
                auto& script = scripts_[key];
                size_t turn = turn_counters_[key];
                
                if (turn < script.size()) {
                    response_text = script[turn];
                    turn_counters_[key]++;
                }
                break;
            }
        }
        if (response_text != "Default response for mock backend.") break;
    }

    std::vector<ChatChunk> chunks;
    chunks.push_back({response_text, true, "assistant"});
    return chunks;
}

platypus::util::Result<platypus::util::Void> FakeBackend::stream_chat(
    const std::vector<std::pair<std::string, std::string>>& messages,
    float temperature,
    int max_tokens,
    std::function<void(const ChatChunk&)> callback) {
    
    std::string response_text = "Default response for mock backend.";
    for (const auto& [role, content] : messages) {
        for (auto const& [key, _] : scripts_) {
            if (content.find(key) != std::string::npos) {
                auto& script = scripts_[key];
                size_t turn = turn_counters_[key];
                
                if (turn < script.size()) {
                    response_text = script[turn];
                    turn_counters_[key]++;
                }
                break;
            }
        }
        if (response_text != "Default response for mock backend.") break;
    }

    size_t chunk_size = 10;
    for (size_t i = 0; i < response_text.length(); i += chunk_size) {
        std::string chunk_content = response_text.substr(i, chunk_size);
        bool is_final = (i + chunk_size >= response_text.length());
        
        callback({chunk_content, is_final, "assistant"});
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return platypus::util::Result<platypus::util::Void>::ok();
}

} // namespace platypus::llm
