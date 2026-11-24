// Workspace-owned registry of the six canonical Tool definitions.
// The execution engine dispatches here; Tool handles validation and bounded result text.
#ifndef PLATYPUS_TOOLS_REGISTRY_HPP
#define PLATYPUS_TOOLS_REGISTRY_HPP

#include "../util/result.hpp"
#include "../util/snapshot.hpp"
#include "tools/tool.hpp"
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace platypus::tools {

/**
 * @brief Registry for all tools available to the agent.
 */
class ToolRegistry {
  public:
    explicit ToolRegistry(std::filesystem::path root = std::filesystem::current_path(),
                          util::SnapshotManager::Confirmer confirmer = {})
        : snapshots_(std::move(root), std::move(confirmer)) {}
    util::SnapshotManager &snapshots() { return snapshots_; }
    /// Execute through the canonical API, retaining explicit error/truncation flags.
    tools::ToolResult execute_tool(const std::string &name, const nlohmann::json &args) {
        auto tool = get_tool(name);
        if (!tool)
            return tools::ToolResult::fail("Unknown tool; use one of the declared tools");
        tools::ToolContext context{snapshots_};
        return tool->execute(args, context);
    }
    /// Compatibility adapter for existing callers; the agent uses execute_tool().
    util::Result<std::string> execute(const std::string &name, const nlohmann::json &args) {
        auto result = execute_tool(name, args);
        return result.is_error ? util::Result<std::string>::fail(result.content)
                               : util::Result<std::string>::ok(result.content);
    }
    /**
     * @brief Registers a new tool in the registry.
     * @param tool The tool to register.
     */
    void register_tool(const Tool &tool) { tools_.insert_or_assign(tool.name(), tool); }

    /**
     * @brief Retrieves a tool from the registry by name.
     * @param name The name of the tool.
     * @return A pointer to the tool, or nullptr if not found.
     */
    const Tool *get_tool(const std::string &name) const {
        static const std::map<std::string, std::string> aliases{
            {"read_file", "read"},   {"list_directory", "list"}, {"find", "list"},
            {"write_file", "write"}, {"replace_text", "edit"},   {"multi_replace_text", "edit"},
            {"run_command", "run"}};
        auto alias = aliases.find(name);
        auto it = tools_.find(alias == aliases.end() ? name : alias->second);
        if (it != tools_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    /**
     * @brief Returns all registered tools.
     * @return A list of all registered tools.
     */
    std::vector<Tool> get_all_tools() const {
        std::vector<Tool> all_tools;
        for (const auto &[name, tool] : tools_) {
            all_tools.push_back(tool);
        }
        return all_tools;
    }

  private:
    std::map<std::string, Tool> tools_;
    util::SnapshotManager snapshots_;
};

// Tool Registration Functions
void register_filesystem_tools(ToolRegistry &registry);
void register_search_tools(ToolRegistry &registry);
void register_shell_tools(ToolRegistry &registry);
void register_default_tools(ToolRegistry &registry);

} // namespace platypus::tools

#endif // PLATYPUS_TOOLS_REGISTRY_HPP
