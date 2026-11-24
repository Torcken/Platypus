#ifndef PLATYPUS_LLM_FAKE_BACKEND_HPP
#define PLATYPUS_LLM_FAKE_BACKEND_HPP

#include "backend.hpp"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace platypus::llm {

/**
 * @brief A mock LLM backend that returns predefined responses.
 * Useful for testing the agent's logic without actual LLM calls.
 */
class FakeBackend : public LlmBackend {
  public:
    FakeBackend();

    // Allows setting a scripted sequence of responses for a specific task/prompt.
    // Each response can be a plain string or a JSON tool call.
    void set_script(const std::string &key, const std::vector<std::string> &responses);

    util::Result<std::vector<ChatChunk>>
    generate_chat(const std::vector<std::pair<std::string, std::string>> &messages,
                  float temperature = 0.7f, int max_tokens = 4096) override;

    util::Result<platypus::util::Void>
    stream_chat(const std::vector<std::pair<std::string, std::string>> &messages, float temperature,
                int max_tokens, std::function<void(const ChatChunk &)> callback) override;

  private:
    // Map of task keys to a vector of responses (to simulate multi-turn)
    std::map<std::string, std::vector<std::string>> scripts_;
    // Keeps track of which turn we are on for each task key
    std::map<std::string, size_t> turn_counters_;
};

} // namespace platypus::llm

#endif // PLATYPUS_LLM_FAKE_BACKEND_HPP
