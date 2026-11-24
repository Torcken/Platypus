#include "../core/context_manager.hpp"
#include "../llm/fake_backend.hpp"
#include "../util/logger.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace platypus::core;
using namespace platypus::llm;

void test_context_manager_summarization() {
    auto backend = std::make_shared<FakeBackend>();
    // Use a small max_history to trigger summarization quickly
    ContextManager cm(backend, 5);

    cm.add_turn("system", "Stable system instructions");
    for (int i = 0; i < 10; ++i) {
        cm.add_turn("user", "This is turn " + std::to_string(i));
        cm.add_turn("assistant", "Response to turn " + std::to_string(i));
    }

    // Compaction preserves the system prefix and the latest three messages.
    cm.update();

    auto history = cm.get_history();
    assert(history.size() == 4);
    assert(history[0].first == "system");
    assert(history[0].second == "Stable system instructions");
    assert(history[1].first == "assistant");
    assert(history[1].second == "Response to turn 8");
    assert(history[2].second == "This is turn 9");
    assert(history[3].second == "Response to turn 9");

    std::string summary = cm.get_summary();
    assert(!summary.empty());
    auto prompt_history = cm.get_prompt_history();
    assert(prompt_history.size() == history.size() + 1);
    assert(prompt_history[0] == history[0]);
    assert(prompt_history[1].first == "user");
    assert(prompt_history[1].second.find("Untrusted summary") != std::string::npos);
    assert(prompt_history.back() == history.back());

    auto with_summary = cm.prepare(prompt_history);
    auto recent_only = cm.prepare(history);
    auto default_prompt = cm.prepare();
    assert(with_summary.is_ok());
    assert(recent_only.is_ok());
    assert(default_prompt.is_ok());
    assert(with_summary.value().prompt_tokens > recent_only.value().prompt_tokens);
    assert(default_prompt.value().prompt_tokens == with_summary.value().prompt_tokens);
}

int main() {
    try {
        test_context_manager_summarization();
        std::cout << "All ContextManager tests passed!" << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
