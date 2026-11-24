#ifndef PLATYPUS_CORE_TOOL_HPP
#define PLATYPUS_CORE_TOOL_HPP

#include "../util/result.hpp"
#include "../util/snapshot.hpp"
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

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
     * @param sm A reference to the shared SnapshotManager.
     * @return A Result containing the tool's output.
     */
    std::function<util::Result<std::string>(const nlohmann::json &,
                                            platypus::util::SnapshotManager &)>
        execute;
};

/**
 * @brief Registry for all tools available to the agent.
 */
class ToolRegistry {
  public:
    explicit ToolRegistry(std::filesystem::path root = std::filesystem::current_path(),
                          util::SnapshotManager::Confirmer confirmer = {})
        : snapshots_(std::move(root), std::move(confirmer)) {}
    util::SnapshotManager &snapshots() { return snapshots_; }
    /// Validates flat JSON arguments and caps outputs at approximately 1500 tokens.
    util::Result<std::string> execute(const std::string &name, const nlohmann::json &args) {
        auto tool = get_tool(name);
        if (!tool)
            return util::Result<std::string>::fail("Unknown tool: " + name);
        try {
            if (!args.is_object())
                return util::Result<std::string>::fail("Arguments must be an object");
            auto schema = nlohmann::json::parse(tool->schema_json);
            for (const auto &required : schema.value("required", nlohmann::json::array()))
                if (!args.contains(required.get<std::string>()))
                    return util::Result<std::string>::fail("Missing required argument: " +
                                                           required.get<std::string>());
            auto properties = schema.value("properties", nlohmann::json::object());
            for (auto it = args.begin(); it != args.end(); ++it) {
                if (!properties.contains(it.key())) {
                    if (schema.value("additionalProperties", true) == false)
                        return util::Result<std::string>::fail("Unknown argument: " + it.key());
                    continue;
                }
                auto type = properties[it.key()].value("type", std::string{});
                const auto &value = it.value();
                bool valid = type.empty() || (type == "string" && value.is_string()) ||
                             (type == "integer" && value.is_number_integer()) ||
                             (type == "number" && value.is_number()) ||
                             (type == "boolean" && value.is_boolean()) ||
                             (type == "object" && value.is_object()) ||
                             (type == "array" && value.is_array());
                if (!valid)
                    return util::Result<std::string>::fail("Wrong type for argument: " + it.key());
            }
            auto result = tool->execute(args, snapshots_);
            if (result.is_ok() && result.value().size() > 6000)
                return util::Result<std::string>::ok(result.value().substr(0, 6000) +
                                                     "\n[output truncated]");
            return result;
        } catch (const std::exception &e) {
            return util::Result<std::string>::fail(e.what());
        }
    }
    /**
     * @brief Registers a new tool in the registry.
     * @param tool The tool to register.
     */
    void register_tool(const Tool &tool) { tools_[tool.name] = tool; }

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

} // namespace platypus::core

#endif // PLATYPUS_CORE_TOOL_HPP
