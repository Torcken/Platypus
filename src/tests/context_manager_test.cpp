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

    // Add 10 turns.
    // The summarization logic in generate_summary() triggers if history_size > max_history_
    // and it summarizes turns from index 1 to (history_size - 4).
    for (int i = 0; i < 10; ++i) {
        cm.add_turn("user", "This is turn " + std::to_string(i));
        cm.add_turn("assistant", "Response to turn " + std::to_string(i));
    }

    // Current history size is 20 (10 user + 10 assistant)
    // max_history_ is 5.

    cm.update();

    auto history = cm.get_history();
    std::cout << "History size after update: " << history.size() << std::endl;

    // The logic in generate_summary:
    // 1. history_size (20) > max_history_ (5) -> triggers update
    // 2. turns_to_summarize: index 1 to (20 - 4) = 1 to 16.
    // 3. new_history:
    //    - history[0]
    //    - history[20-3] to history[19] (last 3 turns)
    //    - Wait, the code in generate_summary:
    //      new_history.push_back(history_[0]);
    //      for (size_t i = history_size - 3; i < history_size; ++i) {
    //          new_history.push_back(history_[i]);
    //      }
    //      history_ = std::move(new_history);
    // So expected size is 1 (first turn) + 3 (last turns) = 4.

    assert(history.size() == 4);
    std::cout << "History size check passed!" << std::endl;

    std::string summary = cm.get_summary();
    // Since FakeBackend returns "Default response for mock backend."
    // and the summary prompt includes the turn content, the summary should be populated.
    assert(!summary.empty());
    std::cout << "Summary not empty check passed!" << std::endl;
    std::cout << "Summary content: " << summary << std::endl;
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
