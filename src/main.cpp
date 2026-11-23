#include "core/execution_engine.hpp"
#include "core/tool.hpp"
#include "llm/llm_backend_factory.hpp"
#include "llm/model_profile.hpp"
#include "util/logger.hpp"
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    using namespace platypus::core;
    using namespace platypus::llm;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " \"<task_description>\" [--headless]" << std::endl;
        return 1;
    }

    std::string task_description = argv[1];
    bool headless = false;
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--headless") {
            headless = true;
            break;
        }
    }
    
    platypus::util::Logger::get_instance().info("Starting Platypus agent...");
    platypus::util::Logger::get_instance().info("Task: " + task_description);

    try {
        // Initialize components
        auto tool_registry = std::make_shared<ToolRegistry>();
        
        // Register default tools
        register_default_tools(*tool_registry);

        // Setup backend (mocked for now or using a config file)
        // In a real app, we'd load model_id from a config file or argument
        auto backend = LlmBackendFactory::create_backend("fake_backend"); 
        
        // Initialize engine
        ExecutionEngine engine(std::move(backend), tool_registry);

        // Execute task
        auto result = engine.run(task_description, headless);

        if (result.is_ok()) {
            if (!headless) {
                platypus::util::Logger::get_instance().info("Task completed successfully!");
                std::cout << result.value() << std::endl;
            } else {
                std::cout << result.value() << std::endl;
            }
        } else {
            if (!headless) {
                platypus::util::Logger::get_instance().error("Task failed: " + result.error());
                std::cerr << result.error() << std::endl;
            } else {
                std::cerr << result.error() << std::endl;
            }
            return 1;
        }

    } catch (const std::exception& e) {
        platypus::util::Logger::get_instance().error("Fatal error: " + std::string(e.what()));
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
