#include "core/context_manager.hpp"
#include "core/execution_engine.hpp"
#include "core/tool.hpp"
#include "llm/chat_formatter.hpp"
#include "llm/fake_backend.hpp"
#include "util/logger.hpp"
#include <cassert>
#include <iostream>
#include <vector>

namespace platypus::core {

void test_multi_step_plan_execution() {
    auto backend = std::make_shared<llm::FakeBackend>();
    auto tool_registry = std::make_shared<ToolRegistry>();

    int executed = 0;
    tool_registry->register_tool({"dummy_tool", "Count a plan step", R"({"type":"object"})",
                                  [&](const nlohmann::json &, util::SnapshotManager &) {
                                      ++executed;
                                      return util::Result<std::string>::ok("dummy output");
                                  }});
    ExecutionEngine engine(backend, tool_registry, 10);

    // Simulate a response with a 3-step plan
    nlohmann::json plan_response = {
        {"plan",
         {{{"tool_name", "dummy_tool"}, {"arguments", nlohmann::json::object()}},
          {{"tool_name", "dummy_tool"}, {"arguments", nlohmann::json::object()}},
          {{"tool_name", "dummy_tool"}, {"arguments", nlohmann::json::object()}}}}};

    backend->set_script("multi-plan-task", {plan_response.dump(), "Done"});
    assert(engine.run("multi-plan-task").is_ok());
    assert(executed == 3);

    auto context_manager = std::make_unique<ContextManager>(backend, 5);

    // Verify history growth
    context_manager->add_turn("user", "query 1");
    context_manager->add_turn("assistant", "response 1");
    context_manager->add_turn("system", "output 1");
    context_manager->add_turn("user", "query 2");
    context_manager->add_turn("assistant", "response 2");
    context_manager->add_turn("system", "output 2");

    assert(context_manager->get_history().size() == 6);

    // Trigger summary (max_history = 5, current = 6)
    context_manager->update();

    // History should now be: [turn 0] + [last 3 turns]
    // Turn 0: ("user", "query 1")
    // Last 3: ("user", "query 2"), ("assistant", "response 2"), ("system", "output 2")
    auto history = context_manager->get_history();
    assert(history.size() == 4);
    assert(history[0].first == "user");
    assert(history[0].second == "query 1");
    assert(history[1].first == "user");
    assert(history[1].second == "query 2");
    assert(history[2].first == "assistant");
    assert(history[2].second == "response 2");
    assert(history[3].first == "system");
    assert(history[3].second == "output 2");

    std::cout << "test_multi_step_plan_execution passed!" << std::endl;
}

} // namespace platypus::core

int main() {
    platypus::core::test_multi_step_plan_execution();
    return 0;
}
