#include "../util/result.hpp"
#include "core/tool.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace platypus::core {

void test_find_tool() {
    ToolRegistry registry(std::filesystem::current_path(), [](const auto &) { return true; });
    register_filesystem_tools(registry);

    const Tool *find_tool = registry.get_tool("find");
    assert(find_tool != nullptr);

    // Test existing path
    nlohmann::json args1 = {{"path", "."}, {"pattern", "*.cpp"}};
    auto res1 = registry.execute("find", args1);
    assert(res1.is_ok());
    std::cout << "test_find_tool (path=.) - OK" << std::endl;

    // Test non-existent path
    nlohmann::json args2 = {{"path", "/non/existent/path/12345"}, {"pattern", "*.cpp"}};
    auto res2 = registry.execute("find", args2);
    // Note: the current find tool implementation uses `popen` and `find` command.
    // If path doesn't exist, `find` will return non-zero and the tool returns fail.
    assert(res2.is_err());
    std::cout << "test_find_tool (non-existent path) - OK" << std::endl;
}

void test_multi_replace_tool() {
    ToolRegistry registry(std::filesystem::current_path(), [](const auto &) { return true; });
    register_filesystem_tools(registry);

    // Create a dummy file for testing
    std::string test_file = "test_replace_file.txt";
    std::ofstream ofs(test_file);
    ofs << "Hello World\nReplace this.\n";
    ofs.close();

    const Tool *replace_tool = registry.get_tool("multi_replace_text");
    assert(replace_tool != nullptr);

    nlohmann::json args = {{"path", "."},
                           {"old_string", "Replace this."},
                           {"new_string", "Success!"},
                           {"recursive", false}};

    auto res = registry.execute("multi_replace_text", args);
    assert(res.is_ok());

    // Verify content
    std::ifstream ifs(test_file);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    assert(content.find("Success!") != std::string::npos);
    assert(content.find("Replace this.") == std::string::npos);

    std::filesystem::remove(test_file);
    std::cout << "test_multi_replace_tool - OK" << std::endl;
}

void test_grep_tool() {
    ToolRegistry registry(std::filesystem::current_path(), [](const auto &) { return true; });
    register_search_tools(registry);

    const Tool *grep_tool = registry.get_tool("grep");
    assert(grep_tool != nullptr);

    // Test regex matching while leaving files outside the worktree ignored.
    auto root = std::filesystem::current_path();
    nlohmann::json args = {{"path", "."}, {"pattern", "^platypus$"}, {"recursive", false}};

    std::ofstream(root / "grep_fixture.txt") << "platypus\n";
    auto res = registry.execute("grep", args);
    assert(res.is_ok());
    assert(res.value().find("grep_fixture.txt:1:platypus") != std::string::npos);
    std::filesystem::remove(root / "grep_fixture.txt");
    std::cout << "test_grep_tool - OK" << std::endl;
}

} // namespace platypus::core

int main() {
    platypus::core::test_find_tool();
    platypus::core::test_multi_replace_tool();
    platypus::core::test_grep_tool();
    std::cout << "All tool tests passed!" << std::endl;
    return 0;
}
