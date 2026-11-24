// Six bounded tools share workspace resolution, approvals and durable snapshots.
// The registry converts exceptions and truncates output before the loop appends it to history.
#include "platform/process.hpp"
#include "registry.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
namespace platypus::tools {
namespace fs = std::filesystem;
using Return = tools::ToolResult;
namespace {
std::string schema(nlohmann::json properties, nlohmann::json required) {
    return nlohmann::json{{"type", "object"},
                          {"properties", properties},
                          {"required", required},
                          {"additionalProperties", false}}
        .dump();
}
std::string read_text(const fs::path &path) {
    if (!fs::is_regular_file(path))
        throw std::runtime_error("Not a regular file");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}
std::vector<fs::path> entries(safety::SnapshotStore &sm, const std::string &path, bool recursive) {
    auto resolved = sm.resolve(path);
    if (!resolved.is_ok())
        throw std::runtime_error(resolved.error());
    if (fs::is_regular_file(resolved.value()))
        return {resolved.value()};
    std::vector<fs::path> paths;
    auto consider = [&](const fs::path &p) {
        auto checked = sm.resolve(p.string());
        if (checked.is_ok())
            paths.push_back(p);
        if (paths.size() > 10000)
            throw std::runtime_error("Too many files; narrow the requested path");
    };
    if (recursive) {
        for (auto it = fs::recursive_directory_iterator(resolved.value());
             it != fs::recursive_directory_iterator(); ++it) {
            if (it->path().filename() == ".git" || it->path().filename() == ".platypus") {
                it.disable_recursion_pending();
                continue;
            }
            consider(it->path());
        }
    } else
        for (const auto &entry : fs::directory_iterator(resolved.value()))
            consider(entry.path());
    std::sort(paths.begin(), paths.end());
    return paths;
}
std::string replace(std::string content, const std::string &old, const std::string &replacement) {
    if (old.empty())
        throw std::runtime_error("old_string is required");
    std::size_t cursor = 0;
    bool matched = false;
    while ((cursor = content.find(old, cursor)) != std::string::npos) {
        content.replace(cursor, old.size(), replacement);
        cursor += replacement.size();
        matched = true;
    }
    if (!matched)
        throw std::runtime_error("Could not find the specified string in the file");
    return content;
}
} // namespace
void register_filesystem_tools(ToolRegistry &registry) {
    registry.register_tool({"read", "Read numbered lines from a file.",
                            schema({{"path", {{"type", "string"}}},
                                    {"start_line", {{"type", "integer"}}},
                                    {"end_line", {{"type", "integer"}}}},
                                   {"path"}),
                            [](const nlohmann::json &args, tools::ToolContext &context) -> Return {
                                auto &sm = context.snapshots;
                                auto path = sm.resolve(args.at("path").get<std::string>());
                                if (!path.is_ok())
                                    return Return::fail(path.error());
                                auto start = args.value("start_line", 1),
                                     end = args.value("end_line", 2147483647);
                                if (start < 1 || end < start)
                                    return Return::fail("Invalid line range");
                                std::ifstream input(path.value());
                                if (!input || !fs::is_regular_file(path.value()))
                                    return Return::fail("Cannot read file");
                                std::string result, line;
                                int number = 0;
                                while (std::getline(input, line) && ++number <= end) {
                                    if (number >= start)
                                        result += std::to_string(number) + ": " + line + "\n";
                                    if (result.size() > context.max_output_bytes)
                                        break;
                                }
                                return Return::ok(result);
                            }});
    registry.register_tool(
        {"list", "List files, optionally recursively with a glob pattern.",
         schema({{"path", {{"type", "string"}}},
                 {"recursive", {{"type", "boolean"}}},
                 {"pattern", {{"type", "string"}}}},
                {}),
         [](const nlohmann::json &args, tools::ToolContext &context) -> Return {
             auto &sm = context.snapshots;
             std::string expression;
             for (char c : args.value("pattern", std::string("*"))) {
                 if (c == '*')
                     expression += ".*";
                 else if (c == '?')
                     expression += '.';
                 else {
                     if (std::string(".\\+()[]{}^$|").find(c) != std::string::npos)
                         expression += '\\';
                     expression += c;
                 }
             }
             std::regex pattern(expression);
             std::string result;
             for (const auto &path :
                  entries(sm, args.value("path", "."), args.value("recursive", false))) {
                 if (std::regex_match(path.filename().string(), pattern))
                     result += fs::relative(path, sm.root()).string() + "\n";
                 if (result.size() > context.max_output_bytes)
                     break;
             }
             return Return::ok(result);
         }});
    registry.register_tool(
        {"write", "Create or replace a file after diff approval and snapshot.",
         schema({{"path", {{"type", "string"}}}, {"content", {{"type", "string"}}}},
                {"path", "content"}),
         [](const nlohmann::json &args, tools::ToolContext &context) -> Return {
             auto &sm = context.snapshots;
             auto result = sm.write(args.at("path").get<std::string>(),
                                    args.at("content").get<std::string>());
             return result.is_ok() ? Return::ok(result.value()) : Return::fail(result.error());
         }});
    registry.register_tool(
        {"edit", "Replace exact text in one file or a directory after approval.",
         schema({{"path", {{"type", "string"}}},
                 {"old_string", {{"type", "string"}}},
                 {"new_string", {{"type", "string"}}},
                 {"use_regex", {{"type", "boolean"}}},
                 {"recursive", {{"type", "boolean"}}}},
                {"path", "old_string", "new_string"}),
         [](const nlohmann::json &args, tools::ToolContext &context) -> Return {
             auto &sm = context.snapshots;
             auto target = sm.resolve(args.at("path").get<std::string>());
             if (!target.is_ok())
                 return Return::fail(target.error());
             std::vector<std::pair<std::string, std::string>> changes;
             auto old = args.at("old_string").get<std::string>();
             if (old.empty())
                 return Return::fail("old_string is required");
             bool directory = fs::is_directory(target.value());
             for (const auto &path :
                  entries(sm, target.value().string(), args.value("recursive", false))) {
                 if (!fs::is_regular_file(path))
                     continue;
                 auto before = read_text(path);
                 std::string after;
                 if (args.value("use_regex", false)) {
                     std::regex pattern(old);
                     if (!std::regex_search(before, pattern)) {
                         if (directory)
                             continue;
                         return Return::fail("No matching text");
                     }
                     after = std::regex_replace(before, pattern,
                                                args.at("new_string").get<std::string>());
                 } else {
                     if (before.find(old) == std::string::npos && directory)
                         continue;
                     after = replace(before, old, args.at("new_string").get<std::string>());
                 }
                 changes.emplace_back(fs::relative(path, sm.root()).string(), after);
             }
             if (changes.empty())
                 return Return::fail("No matching text");
             std::string result;
             for (const auto &[path, content] : changes) {
                 auto written = sm.write(path, content);
                 if (!written.is_ok())
                     return Return::fail(written.error());
                 result += written.value() + "\n";
             }
             return Return::ok(result);
         }});
}
void register_search_tools(ToolRegistry &registry) {
    registry.register_tool(
        {"grep", "Search files with a regular expression; returns path:line:text.",
         schema({{"path", {{"type", "string"}}},
                 {"pattern", {{"type", "string"}}},
                 {"recursive", {{"type", "boolean"}}}},
                {"pattern"}),
         [](const nlohmann::json &args, tools::ToolContext &context) -> Return {
             auto &sm = context.snapshots;
             auto path = sm.resolve(args.value("path", std::string(".")));
             if (!path.is_ok())
                 return Return::fail(path.error());
             auto relative = path.value().lexically_relative(sm.root()).string();
             std::vector<std::string> command{"rg",
                                              "--line-number",
                                              "--color",
                                              "never",
                                              "--hidden",
                                              "--glob",
                                              "!.git/**",
                                              "--glob",
                                              "!.platypus/**",
                                              "--",
                                              args.at("pattern").get<std::string>(),
                                              relative.empty() ? "." : relative};
             if (!args.value("recursive", false))
                 command.insert(command.end() - 3, {"--maxdepth", "1"});
             auto result = platform::run_process(command, sm.root(), std::chrono::seconds(30),
                                                 context.max_output_bytes, sm.stop_token());
             if (!result.is_ok())
                 return Return::fail("ripgrep failed: " + result.error());
             if (result.value().exit_code == 1)
                 return Return::ok("");
             if (result.value().exit_code != 0)
                 return Return::fail("ripgrep failed: " + result.value().output);
             return {result.value().output, false, result.value().truncated};
         }});
}
void register_shell_tools(ToolRegistry &registry) {
    registry.register_tool(
        {"run", "Run an explicitly approved shell command with timeout and output limit.",
         schema({{"command", {{"type", "string"}}}, {"timeout_s", {{"type", "integer"}}}},
                {"command"}),
         [](const nlohmann::json &args, tools::ToolContext &context) -> Return {
             auto &sm = context.snapshots;
             auto command = args.at("command").get<std::string>();
             int timeout = args.value("timeout_s", 30);
             if (command.empty() || timeout < 1 || timeout > 300)
                 return Return::fail("Invalid command or timeout (1..300 seconds)");
             auto preview = "Command: " + command + "\nDirectory: " + sm.root().string() +
                            "\nTimeout: " + std::to_string(timeout) +
                            " seconds\nOutput cap: " + std::to_string(context.max_output_bytes) +
                            " bytes\n";
             if (!sm.confirm(preview))
                 return Return::fail("Permission denied");
             auto saved = sm.snapshot("Before command: " + command);
             if (!saved.is_ok())
                 return Return::fail(saved.error());
             std::ofstream log(sm.root() / ".platypus/actions.jsonl", std::ios::app);
             log << nlohmann::json{{"action", "run"}, {"command", command}, {"timeout_s", timeout}}
                        .dump()
                 << '\n';
             auto result = platform::run_process({"/bin/sh", "-c", command}, sm.root(),
                                                 std::chrono::seconds(timeout),
                                                 context.max_output_bytes, sm.stop_token());
             if (!result.is_ok())
                 return Return::fail(result.error());
             if (result.value().exit_code != 0)
                 return {"Exit code " + std::to_string(result.value().exit_code) + "\n" +
                             result.value().output,
                         true, result.value().truncated};
             return {result.value().output, false, result.value().truncated};
         }});
}
void register_default_tools(ToolRegistry &registry) {
    register_filesystem_tools(registry);
    register_search_tools(registry);
    register_shell_tools(registry);
}
} // namespace platypus::tools
