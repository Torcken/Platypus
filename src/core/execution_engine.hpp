#ifndef PLATYPUS_CORE_EXECUTION_ENGINE_HPP
#define PLATYPUS_CORE_EXECUTION_ENGINE_HPP

#include "tool.hpp"
#include "context_manager.hpp"
#include "../llm/backend.hpp"
#include "../llm/chat_formatter.hpp"
#include "../util/result.hpp"
#include "../util/logger.hpp"
#include <vector>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace platypus::core {

/**
 * @brief Represents a single step in the agent's plan.
 */
struct PlanStep {
    std::string tool_name;
    nlohmann::json arguments;
};

/**
 * @brief Represents a single action detected in an LLM response.
 */
struct Action {
    std::string tool_name;
    nlohmann::json arguments;
};

/**
 * @brief The ExecutionEngine manages the agent's interaction loop.
 */
class ExecutionEngine {
public:
    /**
     * @brief Constructs the ExecutionEngine.
     * @param backend The LLM backend to use.
     * @param tool_registry The registry of available tools.
     * @param max_history The maximum number of history turns to keep.
     */
    ExecutionEngine(std::shared_ptr<llm::LlmBackend> backend, 
                    std::shared_ptr<ToolRegistry> tool_registry,
                    size_t max_history = 20);

    /**
     * @brief Runs the agent's reasoning loop for a given query.
     * @param query The user's input query.
     * @param max_steps The maximum number of reasoning steps allowed.
     * @param headless Whether to run in headless mode.
     * @return A Result containing the final response from the agent.
     */
    util::Result<std::string> run(const std::string& query, int max_steps = 10, bool headless = false);

private:
    /**
     * @brief Helper to format the current state into a prompt for the LLM.
     */
    std::string build_prompt(const std::vector<std::pair<std::string, std::string>>& history);

    std::shared_ptr<llm::LlmBackend> backend_;
    std::shared_ptr<ToolRegistry> tool_registry_;
    std::unique_ptr<ContextManager> context_manager_;

    std::vector<PlanStep> current_plan_;
    size_t active_plan_index_ = 0;

};

} // namespace platypus::core

#endif // PLATYPUS_CORE_EXECUTION_ENGINE_HPP
