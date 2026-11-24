#ifndef PLATYPUS_CORE_EXECUTION_ENGINE_HPP
#define PLATYPUS_CORE_EXECUTION_ENGINE_HPP

#include "../llm/backend.hpp"
#include "../llm/chat_formatter.hpp"
#include "../util/logger.hpp"
#include "../util/result.hpp"
#include "agent/context_manager.hpp"
#include "session/store.hpp"
#include "tool.hpp"
#include <memory>
#include <nlohmann/json.hpp>
#include <stop_token>
#include <string>
#include <vector>

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
                    std::shared_ptr<ToolRegistry> tool_registry, size_t max_history = 20);

    /**
     * @brief Runs the agent's reasoning loop for a given query.
     * @param query The user's input query.
     * @param max_steps The maximum number of reasoning steps allowed.
     * @param headless Whether to run in headless mode.
     * @return A Result containing the final response from the agent.
     */
    util::Result<std::string> run(const std::string &query, int max_steps = 25,
                                  bool headless = false, std::stop_token stop = {});

    /// Attach a session log; history is loaded without appending duplicate records.
    util::Result<bool> attach_session(std::shared_ptr<session::Store> store, const std::string &id);
    void set_token_callback(llm::TokenCallback callback) { on_token_ = std::move(callback); }
    /// Synchronous worker callback; consumers must copy usage before returning.
    void set_context_callback(std::function<void(const agent::ContextUsage &)> callback) {
        on_context_ = std::move(callback);
    }

  private:
    llm::TokenCallback on_token_;
    std::function<void(const agent::ContextUsage &)> on_context_;
    void append_turn(const std::string &role, const std::string &text);
    std::shared_ptr<session::Store> session_store_;
    std::string session_id_;
    /**
     * @brief Helper to format the current state into a prompt for the LLM.
     */
    std::string build_prompt(const std::vector<std::pair<std::string, std::string>> &history);

    std::shared_ptr<llm::LlmBackend> backend_;
    std::shared_ptr<ToolRegistry> tool_registry_;
    std::unique_ptr<agent::ContextManager> context_manager_;

    std::vector<PlanStep> current_plan_;
    size_t active_plan_index_ = 0;
};

} // namespace platypus::core

#endif // PLATYPUS_CORE_EXECUTION_ENGINE_HPP
