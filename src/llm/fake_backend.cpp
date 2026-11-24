#include "fake_backend.hpp"
#include "../util/logger.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>

namespace platypus::llm {

FakeBackend::FakeBackend() {
    // Default fallback responses
    scripts_["hello"] = {"Hello! How can I help you today?"};
}

util::Result<util::Void> FakeBackend::load_recording(const std::string &path) {
    using Return = util::Result<util::Void>;
    try {
        std::ifstream input(path);
        if (!input)
            return Return::fail("Cannot open recording: " + path);
        std::vector<std::string> responses;
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty())
                continue;
            auto record = nlohmann::json::parse(line);
            auto endpoint = record.at("endpoint").get<std::string>();
            if (endpoint != "/completion" && endpoint != "/v1/completions" &&
                endpoint != "/v1/chat/completions")
                continue;
            const auto &response = record.at("response");
            if (response.contains("choices")) {
                auto choice = response.at("choices").at(0);
                if (choice.contains("message")) {
                    auto message = choice.at("message");
                    if (message.contains("tool_calls"))
                        responses.push_back(
                            nlohmann::json{{"tool_calls", message.at("tool_calls")}}.dump());
                    else
                        responses.push_back(message.at("content").get<std::string>());
                } else
                    responses.push_back(choice.at("text").get<std::string>());
            } else
                responses.push_back(response.at("content").get<std::string>());
        }
        if (responses.empty())
            return Return::fail("Recording has no completion responses");
        scripts_.clear();
        turn_counters_.clear();
        set_script("", responses);
        strict_recording_ = true;
        return Return::ok();
    } catch (const std::exception &e) {
        return Return::fail(std::string("Invalid recording: ") + e.what());
    }
}
void FakeBackend::set_script(const std::string &key, const std::vector<std::string> &responses) {
    scripts_[key] = responses;
    turn_counters_[key] = 0;
}

util::Result<std::vector<ChatChunk>>
FakeBackend::generate_chat(const std::vector<std::pair<std::string, std::string>> &messages,
                           float temperature, int max_tokens) {

    (void)temperature;
    (void)max_tokens;
    std::string response_text = "Default response for mock backend.";

    // Check if any message content matches a script key
    for (const auto &[role, content] : messages) {
        for (auto const &[key, _] : scripts_) {
            if (content.find(key) != std::string::npos) {
                auto &script = scripts_[key];
                size_t turn = turn_counters_[key];

                if (turn < script.size()) {
                    response_text = script[turn];
                    turn_counters_[key]++;
                } else if (strict_recording_) {
                    return util::Result<std::vector<ChatChunk>>::fail("Recording exhausted");
                }
                break;
            }
        }
        if (response_text != "Default response for mock backend.")
            break;
    }

    std::vector<ChatChunk> chunks;
    chunks.push_back({response_text, true, "assistant"});
    return chunks;
}

platypus::util::Result<platypus::util::Void>
FakeBackend::stream_chat(const std::vector<std::pair<std::string, std::string>> &messages,
                         float temperature, int max_tokens,
                         std::function<void(const ChatChunk &)> callback) {

    (void)temperature;
    (void)max_tokens;
    std::string response_text = "Default response for mock backend.";
    for (const auto &[role, content] : messages) {
        for (auto const &[key, _] : scripts_) {
            if (content.find(key) != std::string::npos) {
                auto &script = scripts_[key];
                size_t turn = turn_counters_[key];

                if (turn < script.size()) {
                    response_text = script[turn];
                    turn_counters_[key]++;
                } else if (strict_recording_) {
                    return util::Result<util::Void>::fail("Recording exhausted");
                }
                break;
            }
        }
        if (response_text != "Default response for mock backend.")
            break;
    }

    size_t chunk_size = 10;
    for (size_t i = 0; i < response_text.length(); i += chunk_size) {
        if (stop_.stop_requested())
            return util::Result<util::Void>::fail("Cancelled");
        std::string chunk_content = response_text.substr(i, chunk_size);
        bool is_final = (i + chunk_size >= response_text.length());

        callback({chunk_content, is_final, "assistant"});

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return platypus::util::Result<platypus::util::Void>::ok();
}

} // namespace platypus::llm
