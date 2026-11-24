// Append-only conversation history and per-profile context budgeting for the agent loop.
// LlmBackend supplies prompt sizes; ExecutionEngine checks the budget before each generation.
// Explicit legacy compaction remains available only at task boundaries.
#ifndef PLATYPUS_AGENT_CONTEXT_MANAGER_HPP
#define PLATYPUS_AGENT_CONTEXT_MANAGER_HPP

#include "../llm/backend.hpp"
#include "../util/result.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace platypus::agent {

/// Prompt usage and output reservation for the next generation, never server timing metrics.
struct ContextUsage {
    std::size_t prompt_tokens;
    std::size_t output_tokens;
    std::size_t context_tokens;
    bool tokenized;
    /// A plain-language status that distinguishes tokenizer counts from estimates.
    std::string describe() const;
};

/**
 * @brief Owns append-only history and its input/output token budget.
 * Only the agent worker may use this object; UI callbacks receive copied usage values.
 * History is never compacted implicitly during a task.
 */
class ContextManager {
  public:
    /**
     * @brief Constructs the ContextManager.
     * @param backend The LLM backend to use for summarization.
     * @param max_history The maximum number of history turns to keep in the sliding window.
     */
    ContextManager(std::shared_ptr<llm::LlmBackend> backend, size_t max_history = 20);

    /// Size the next prompt and reserve up to 2048 output tokens (a quarter of small contexts).
    /// Fails on cancellation, tokenizer errors or a full context, without changing any turns.
    util::Result<ContextUsage> prepare(std::stop_token stop = {});
    /// Size a transient prompt without appending its extra context to durable history.
    util::Result<ContextUsage>
    prepare(const std::vector<std::pair<std::string, std::string>> &prompt_messages,
            std::stop_token stop = {});
    /// The latest sizing result, also available when prepare() refuses a full context.
    const std::optional<ContextUsage> &usage() const { return usage_; }

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
    std::size_t context_tokens_;
    std::size_t output_tokens_;
    std::optional<ContextUsage> usage_;
};

} // namespace platypus::agent

#endif // PLATYPUS_AGENT_CONTEXT_MANAGER_HPP
