#include "../core/execution_engine.hpp"
#include "../core/tool.hpp"
#include "../llm/fake_backend.hpp"
#include "../util/logger.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

using namespace platypus::core;
using namespace platypus::llm;

void test_execution_engine_loop() {
    // Setup
    auto backend = std::make_unique<FakeBackend>();
    auto tool_registry = std::make_shared<ToolRegistry>();

    // Define a simple tool
    Tool echo_tool;
    echo_tool.name = "echo";
    echo_tool.description = "Echoes the input string.";
    echo_tool.schema_json = "{\"type\": \"object\", \"properties\": {\"message\": {\"type\": \"string\"}}}";
    echo_tool.execute = [](const nlohmann::json& args) -> util::Result<std::string> {
        return util::Result<std::string>::Ok(args["message"].get<std::string>());
    };
    tool_registry->register_tool(echo_tool);

    ExecutionEngine engine(std::move(backend), tool_registry);

    // Test scenario: LLM returns a tool call, engine executes it, then LLM returns final answer.
    // Note: This is hard to test perfectly with FakeBackend without mocking the specific 
    // response string for a specific turn, but we can verify the loop handles a valid tool call.
    
    // Since FakeBackend currently returns a static "Mock response", 
    // we can't easily simulate a multi-turn tool call without modifying FakeBackend.
    // For now, we verify the engine starts and runs.
    
    auto result = engine.run("Hello world");
    assert(result.is_ok());
    std::cout << "ExecutionEngine loop test passed (basic run)!" << std::endl;
}

int main() {
    try {
        test_execution_engine_loop();
        std::cout << "All ExecutionEngine tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
