#ifndef PLATYPUS_CORE_TOOL_HPP
#define PLATYPUS_CORE_TOOL_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "../util/result.hpp"
#include <nlohmann/json.hpp>

namespace platypus::core {

/**
 * @brief Represents a tool available to the agent.
 */
struct Tool {
    std::string name;
    std::string description;
    // A map of parameter names to their descriptions and types.
    // For now, we'll use a simple JSON string to describe the schema.
    std::string schema_json;

    /**
     * @brief The execution logic for the tool.
     * @param arguments A JSON object containing the tool's arguments.
     * @return A Result containing the tool's output.
     */
    std::function<util::Result<std::string>(const nlohmann::json&)> execute;
};

/**
 * @brief Registry for all tools available to the agent.
 */
class ToolRegistry {
public:
    /**
     * @brief Registers a new tool in the registry.
     * @param tool The tool to register.
     */
    void register_tool(const Tool& tool) {
        tools_[tool.name] = tool;
    }

    /**
     * @brief Retrieves a tool from the registry by name.
     * @param name The name of the tool.
     * @return A pointer to the tool, or nullptr if not found.
     */
    const Tool* get_tool(const std::string& name) const {
        auto it = tools_.find(name);
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
        for (const auto& [name, tool] : tools_) {
            all_tools.push_back(tool);
        }
        return all_tools;
    }

private:
    std::map<std::string, Tool> tools_;
};

// Tool Registration Functions
void register_filesystem_tools(ToolRegistry& registry);
void register_search_tools(ToolRegistry& registry);
void register_shell_tools(ToolRegistry& registry);
void register_default_tools(ToolRegistry& registry);

} // namespace platypus::core

#endif // PLATYPUS_CORE_TOOL_HPP
