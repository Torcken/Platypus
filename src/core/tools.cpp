#include "tool.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>
#include <cstdio>
#include "../util/logger.hpp"

namespace platypus::core {

namespace fs = std::filesystem;

// --- Safety Helpers ---

/**
 * @brief Verifies that a path is contained within the allowed workspace.
 * @param path_str The path to check.
 * @return true if safe, false otherwise.
 */
bool is_path_safe(const std::string& path_str) {
    try {
        fs::path workspace_root = fs::absolute(fs::weakly_canonical("./test_workspace_1"));
        fs::path target_path = fs::absolute(fs::weakly_canonical(path_str));

        std::cerr << "[DEBUG] Safety Check" << std::endl;
        std::cerr << "  Workspace Root: " << workspace_root << std::endl;
        std::cerr << "  Target Path:    " << target_path << std::endl;
        
        std::cerr << "  Workspace Components: ";
        for (const auto& comp : workspace_root) std::cerr << "[" << comp.string() << "] ";
        std::cerr << std::endl;

        std::cerr << "  Target Components:    ";
        for (const auto& comp : target_path) std::cerr << "[" << comp.string() << "] ";
        std::cerr << std::endl;

        auto [it_root, it_target] = std::mismatch(workspace_root.begin(), workspace_root.end(), target_path.begin());
        
        bool safe = (it_root == workspace_root.end());
        std::cerr << "  Result: " << (safe ? "SAFE" : "UNSAFE") << std::endl;
        std::cerr << "  it_root matched at: " << (it_root == workspace_root.end() ? "end" : "component " + std::to_string(std::distance(workspace_root.begin(), it_root))) << std::endl;

        return safe;
    } catch (const std::exception& e) {
        std::cerr << "[DEBUG] Safety Check Exception: " << e.what() << std::endl;
        return false;
    } catch (...) {
        return false;
    }
}

// --- Filesystem Tools ---

void register_filesystem_tools(ToolRegistry& registry) {
    // List Directory
    registry.register_tool({
        "list_directory",
        "List the contents of a directory.",
        R"({"path": "string", "recursive": "boolean"})",
        [](const nlohmann::json& args) -> util::Result<std::string> {
            std::string path_str = args.value("path", ".");
            if (!is_path_safe(path_str)) return util::Result<std::string>::fail("Safety Error: Path is outside workspace.");
            
            bool recursive = args.value("recursive", false);
            
            try {
                fs::path p(path_str);
                if (!fs::exists(p)) return util::Result<std::string>::fail("Path does not exist: " + path_str);
                if (!fs::is_directory(p)) return util::Result<std::string>::fail("Path is not a directory: " + path_str);

                std::stringstream ss;
                if (recursive) {
                    for (const auto& entry : fs::recursive_directory_iterator(p)) {
                        ss << (entry.is_directory() ? "[DIR] " : "[FILE] ") << entry.path().string() << "\n";
                    }
                } else {
                    for (const auto& entry : fs::directory_iterator(p)) {
                        ss << (entry.is_directory() ? "[DIR] " : "[FILE] ") << entry.path().string() << "\n";
                    }
                }
                return util::Result<std::string>::ok(ss.str());
            } catch (const std::exception& e) {
                return util::Result<std::string>::fail(std::string("Filesystem error: ") + e.what());
            }
        }
    });

    // Find Files
    registry.register_tool({
        "find",
        "Find files and directories based on a name pattern using std::filesystem.",
        R"({"path": "string", "pattern": "string"})",
        [](const nlohmann::json& args) -> util::Result<std::string> {
            std::string path_str = args.value("path", ".");
            if (!is_path_safe(path_str)) return util::Result<std::string>::fail("Safety Error: Path is outside workspace.");
            
            std::string pattern = args.value("pattern", "");
            if (path_str.empty()) return util::Result<std::string>::fail("Path is required.");
            if (pattern.empty()) return util::Result<std::string>::fail("Pattern is required.");

            try {
                fs::path root(path_str);
                if (!fs::exists(root)) return util::Result<std::string>::fail("Path does not exist: " + path_str);

                std::stringstream ss;
                for (const auto& entry : fs::recursive_directory_iterator(root)) {
                    if (entry.path().filename().string().find(pattern) != std::string::npos) {
                        ss << (entry.is_directory() ? "[DIR] " : "[FILE] ") << entry.path().string() << "\n";
                    }
                }

                return util::Result<std::string>::ok(ss.str().empty() ? "No files found matching pattern." : ss.str());
            } catch (const std::exception& e) {
                return util::Result<std::string>::fail(std::string("Find error: ") + e.what());
            }
        }
    });

    // Read File
    registry.register_tool({
        "read_file",
        "Read the content of a file.",
        R"({"path": "string"})",
        [](const nlohmann::json& args) -> util::Result<std::string> {
            std::string path_str = args.value("path", "");
            if (!is_path_safe(path_str)) return util::Result<std::string>::fail("Safety Error: Path is outside workspace.");

            try {
                std::ifstream file(path_str);
                if (!file.is_open()) return util::Result<std::string>::fail("Could not open file: " + path_str);

                std::stringstream ss;
                ss << file.rdbuf();
                return util::Result<std::string>::ok(ss.str());
            } catch (const std::exception& e) {
                return util::Result<std::string>::fail(std::string("Read error: ") + e.what());
            }
        }
    });

    // Multi Replace Text
    registry.register_tool({
        "multi_replace_text",
        "Replace a string across multiple files in a directory.",
        R"({"path": "string", "old_string": "string", "new_string": "string", "recursive": "boolean"})",
        [](const nlohmann::json& args) -> util::Result<std::string> {
            std::string path_str = args.value("path", "");
            if (!is_path_safe(path_str)) return util::Result<std::string>::fail("Safety Error: Path is outside workspace.");
            
            std::string old_str = args.value("old_string", "");
            std::string new_str = args.value("new_string", "");
            bool recursive = args.value("recursive", false);

            if (path_str.empty()) return util::Result<std::string>::fail("Path is required.");
            if (old_str.empty()) return util::Result<std::string>::fail("old_string is required.");

            try {
                fs::path root_path(path_str);
                if (!fs::exists(root_path)) return util::Result<std::string>::fail("Path does not exist: " + path_str);

                std::stringstream summary;
                summary << "Starting multi-replace in " << path_str << "...\n";

                auto process_file = [&](const fs::path& p) {
                    if (fs::is_regular_file(p)) {
                        std::ifstream file(p, std::ios::binary);
                        if (!file.is_open()) return;

                        std::stringstream ss;
                        ss << file.rdbuf();
                        std::string content = ss.str();
                        file.close();

                        size_t pos = content.find(old_str);
                        if (pos != std::string::npos) {
                            size_t first_pos;
                            while ((first_pos = content.find(old_str, first_pos)) != std::string::npos) {
                                content.replace(first_pos, old_str.length(), new_str);
                                first_pos += new_str.length();
                            }

                            std::ofstream out_file(p, std::ios::trunc);
                            if (!out_file.is_open()) return;
                            out_file << content;
                            out_file.close();
                            summary << "Updated: " << p.string() << "\n";
                        }
                    }
                };

                if (recursive) {
                    for (const auto& entry : fs::recursive_directory_iterator(root_path)) {
                        process_file(entry.path());
                    }
                } else {
                    for (const auto& entry : fs::directory_iterator(root_path)) {
                        process_file(entry.path());
                    }
                }

                return util::Result<std::string>::ok(summary.str());
            } catch (const std::exception& e) {
                return util::Result<std::string>::fail(std::string("Multi-replace error: ") + e.what());
            }
        }
    });

    // Write File

    registry.register_tool({
        "write_file",
        "Write content to a file. Overwrites if exists.",
        R"({"path": "string", "content": "string"})",
        [](const nlohmann::json& args) -> util::Result<std::string> {
            std::string path_str = args.value("path", "");
            if (!is_path_safe(path_str)) return util::Result<std::string>::fail("Safety Error: Path is outside workspace.");
            
            std::string content = args.value("content", "");
            if (path_str.empty()) return util::Result<std::string>::fail("Path is required.");

            try {
                std::ofstream file(path_str, std::ios::trunc);
                if (!file.is_open()) return util::Result<std::string>::fail("Could not open file for writing: " + path_str);

                file << content;
                return util::Result<std::string>::ok("File written successfully to " + path_str);
            } catch (const std::exception& e) {
                return util::Result<std::string>::fail(std::string("Write error: ") + e.what());
            }
        }
    });

    // Replace Text
    registry.register_tool({
        "replace_text",
        "Replace a string in a file with another string. Provides a more precise way to edit files than overwriting the whole file.",
        R"({"path": "string", "old_string": "string", "new_string": "string"})",
        [](const nlohmann::json& args) -> util::Result<std::string> {
            std::string path_str = args.value("path", "");
            if (!is_path_safe(path_str)) return util::Result<std::string>::fail("Safety Error: Path is outside workspace.");
            
            std::string old_str = args.value("old_string", "");
            std::string new_str = args.value("new_string", "");
            
            if (path_str.empty()) return util::Result<std::string>::fail("Path is required.");
            if (old_str.empty()) return util::Result<std::string>::fail("old_string is required.");

            try {
                std::ifstream file(path_str, std::ios::binary);
                if (!file.is_open()) return util::Result<std::string>::fail("Could not open file: " + path_str);

                std::stringstream ss;
                ss << file.rdbuf();
                std::string content = ss.str();
                file.close();

                size_t pos = content.find(old_str);
                if (pos == std::string::npos) {
                    return util::Result<std::string>::fail("Could not find the specified string in the file.");
                }

                content.replace(pos, old_str.length(), new_str);

                std::ofstream out_file(path_str, std::ios::trunc);
                if (!out_file.is_open()) return util::Result<std::string>::fail("Could not open file for writing: " + path_str);

                out_file << content;
                out_file.close();

                return util::Result<std::string>::ok("Successfully replaced the text in " + path_str);
            } catch (const std::exception& e) {
                return util::Result<std::string>::fail(std::string("Replace error: ") + e.what());
            }
        }
    });
}

// --- Shell Tools ---

void register_shell_tools(ToolRegistry& registry) {
    // Run Command
    registry.register_tool({
        "run_command",
        "Run a shell command and return the output.",
        R"({"command": "string"})",
        [](const nlohmann::json& args) -> util::Result<std::string> {
            std::string cmd = args.value("command", "");
            if (cmd.empty()) return util::Result<std::string>::fail("Command is required.");

            // Basic command validation: ensure the command doesn't involve paths outside workspace
            // and doesn't contain characters that would allow escaping (simplistic check)
            if (cmd.find("..") != std::string::npos) return util::Result<std::string>::fail("Safety Error: Command contains directory traversal.");

            // Using popen for simplicity in this phase, 
            // keeping in mind security implications of raw command execution.
            char buffer[128];
            std::string result;
            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe) return util::Result<std::string>::fail("Failed to run command: " + cmd);

            while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
                result += buffer;
            }

            int status = pclose(pipe);
            if (status != 0) {
                return util::Result<std::string>::fail("Command failed with exit code " + std::to_string(status));
            }

            return util::Result<std::string>::ok(result);
        }
    });
}

// --- Search Tools ---

void register_search_tools(ToolRegistry& registry) {
    // Grep
    registry.register_tool({
        "grep",
        "Search for a string in a file or directory using regex.",
        R"({"path": "string", "pattern": "string", "recursive": "boolean"})",
        [](const nlohmann::json& args) -> util::Result<std::string> {
            std::string path_str = args.value("path", ".");
            if (!is_path_safe(path_str)) return util::Result<std::string>::fail("Safety Error: Path is outside workspace.");
            
            std::string pattern_str = args.value("pattern", "");
            bool recursive = args.value("recursive", false);

            try {
                std::stringstream ss;
                fs::path p(path_str);
                if (!fs::exists(p)) return util::Result<std::string>::fail("Path does not exist: " + path_str);

                std::regex pattern(pattern_str);

                auto process_path = [&](const fs::path& entry_path) {
                    if (fs::is_regular_file(entry_path)) {
                        std::ifstream file(entry_path);
                        std::string line;
                        int line_num = 0;
                        while (std::getline(file, line)) {
                            line_num++;
                            if (std::regex_search(line, pattern)) {
                                ss << entry_path.string() << ":" << line_num << ": " << line << "\n";
                            }
                        }
                    }
                };

                if (recursive) {
                    for (const auto& entry : fs::recursive_directory_iterator(p)) {
                        process_path(entry.path());
                    }
                } else {
                    for (const auto& entry : fs::directory_iterator(p)) {
                        process_path(entry.path());
                    }
                }

                return util::Result<std::string>::ok(ss.str());
            } catch (const std::regex_error& e) {
                return util::Result<std::string>::fail(std::string("Regex error: ") + e.what());
            } catch (const std::exception& e) {
                return util::Result<std::string>::fail(std::string("Grep error: ") + e.what());
            }
        }
    });
}

void register_default_tools(ToolRegistry& registry) {
    register_filesystem_tools(registry);
    register_search_tools(registry);
    register_shell_tools(registry);
}

} // namespace platypus::core
