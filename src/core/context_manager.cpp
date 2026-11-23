#include "context_manager.hpp"
#include "../llm/chat_formatter.hpp"
#include "../util/logger.hpp"
#include <algorithm>

namespace platypus::core {

ContextManager::ContextManager(std::shared_ptr<llm::LlmBackend> backend, size_t max_history)
    : backend_(std::move(backend)), max_history_(max_history) {}

void ContextManager::add_turn(const std::string& role, const std::string& content) {
    history_.push_back({role, content});
}

void ContextManager::update() {
    if (history_.size() > max_history_) {
        auto result = generate_summary();
        if (!result.is_ok()) {
            util::Logger::get_instance().error("ContextManager: Failed to generate summary: " + result.error());
        }
    }
}

std::vector<std::pair<std::string, std::string>> ContextManager::get_history() const {
    return history_;
}

std::string ContextManager::get_summary() const {
    return context_summary_;
}

util::Result<std::string> ContextManager::generate_summary() {
    size_t history_size = history_.size();
    if (history_size <= 4) {
        return util::Result<std::string>::ok("");
    }

    std::vector<std::pair<std::string, std::string>> turns_to_summarize;
    for (size_t i = 1; i < history_size - 3; ++i) {
        turns_to_summarize.push_back(history_[i]);
    }

    std::string summary_prompt = "Summarize the following conversation history. Your summary should be a concise technical overview that captures:\n1. The main goals of the current task.\n2. Key technical decisions made (e.g., architectural choices, specific algorithms).\n3. The current status of the code/implementation (what is finished, what is pending).\n4. Any specific errors or blockers encountered.\n\nKeep the summary technical and brief. Focus on providing enough context for a continuation of the work.\n\n\n";
    for (const auto& turn : turns_to_summarize) {
        summary_prompt += "[" + turn.first + "]: " + turn.second + "\n";
    }

    auto response = backend_->generate_chat({{"user", summary_prompt}}, 0.3f, 512);
    if (!response.is_ok()) {
        return util::Result<std::string>::fail("LLM Backend error during summarization: " + response.error());
    }

    if (!response.value().empty()) {
        context_summary_ = response.value()[0].content;
        
        std::vector<std::pair<std::string, std::string>> new_history;
        new_history.push_back(history_[0]);
        for (size_t i = history_size - 3; i < history_size; ++i) {
            new_history.push_back(history_[i]);
        }
        history_ = std::move(new_history);
    }

    return util::Result<std::string>::ok(context_summary_);
}

} // namespace platypus::core
