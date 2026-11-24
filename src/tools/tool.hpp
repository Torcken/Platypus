// Canonical tool API shared by the registry, built-in tools and execution engine.
// Each execution validates flat arguments and returns bounded text plus explicit status flags.
// ToolContext borrows the safety store; it must outlive the synchronous execute() call.
#ifndef PLATYPUS_TOOLS_TOOL_HPP
#define PLATYPUS_TOOLS_TOOL_HPP

#include "safety/snapshot_store.hpp"
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace platypus::tools {
/// A model-visible observation; errors and truncation never depend on parsing its prose.
struct ToolResult {
    std::string content;
    bool is_error = false;
    bool truncated = false;
    /// Create a successful observation; execute() applies the output cap afterwards.
    static ToolResult ok(std::string content) { return {std::move(content), false, false}; }
    /// Create an error observation that the model may recover from on its next turn.
    static ToolResult fail(std::string content) { return {std::move(content), true, false}; }
};

/// Per-call workspace permissions and output limit; owned and used by the agent worker only.
struct ToolContext {
    safety::SnapshotStore &snapshots;
    std::size_t max_output_bytes = 6000;
};

/// A tool definition with one synchronous handler and common validation/output processing.
class Tool {
  public:
    using Handler = std::function<ToolResult(const nlohmann::json &, ToolContext &)>;
    /// Construct a fixed declaration. A schema JSON string is accepted for legacy definitions.
    Tool(std::string name, std::string description, nlohmann::json schema, Handler handler);
    const std::string &name() const { return name_; }
    const std::string &description() const { return description_; }
    const nlohmann::json &json_schema() const { return schema_; }
    /// Validate and execute once; catch handler exceptions and sanitize/cap UTF-8 observations.
    /// Arguments are never coerced. Truncation applies to successful and failed observations.
    ToolResult execute(const nlohmann::json &args, ToolContext &context) const;

  private:
    std::string name_;
    std::string description_;
    nlohmann::json schema_;
    Handler handler_;
};
} // namespace platypus::tools
#endif
