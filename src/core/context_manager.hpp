#ifndef PLATYPUS_CORE_CONTEXT_MANAGER_HPP
#define PLATYPUS_CORE_CONTEXT_MANAGER_HPP

#include "../llm/backend.hpp"
#include "../util/result.hpp"
#include <memory>
#include <string>
#include <vector>

namespace platypus::core {

/**
 * @brief Manages the conversation history, sliding window, and summarization.
 *
 * This class ensures that the conversation history remains within reasonable limits
 * by using a sliding window and summarizing older turns when the limit is exceeded.
 */
class ContextManager {
  public:
    /**
     * @brief Constructs the ContextManager.
     * @param backend The LLM backend to use for summarization.
     * @param max_history The maximum number of history turns to keep in the sliding window.
     */
    ContextManager(std::shared_ptr<llm::LlmBackend> backend, size_t max_history = 20);

    /**
     * @brief Adds a new turn to the conversation history.
     * @param role The role of the turn (e.g., "user", "assistant", "system").
     * @param content The content of the turn.
     */
    void add_turn(const std::string &role, const std::string &content);

    /**
     * @brief Compacts at an explicit task boundary; the execution loop never calls this mid-task.
     */
    void update();

    /**
     * @brief Returns the current conversation history.
     */
    std::vector<std::pair<std::string, std::string>> get_history() const;

    /**
     * @brief Returns the current context summary.
     */
    std::string get_summary() const;

  private:
    /**
     * @brief Generates a summary of the intermediate conversation turns.
     * @return A result indicating success and the summary string.
     */
    util::Result<std::string> generate_summary();

    std::shared_ptr<llm::LlmBackend> backend_;
    std::vector<std::pair<std::string, std::string>> history_;
    size_t max_history_;
    std::string context_summary_;
};

} // namespace platypus::core

#endif // PLATYPUS_CORE_CONTEXT_MANAGER_HPP
