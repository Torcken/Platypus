#include "core/execution_engine.hpp"
#include "core/tool.hpp"
#include "llm/fake_backend.hpp"
#include "util/logger.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>

using namespace platypus::core;
using namespace platypus::llm;

namespace fs = std::filesystem;

void setup_test_workspace(const std::string& root) {
    if (fs::exists(root)) {
        fs::remove_all(root);
    }
    fs::create_directories(root);
}

void run_e2e_test(const std::string& name, const std::string& task, const std::string& workspace, const std::string& expected_file, const std::string& expected_content) {
    std::cout << "Running test: " << name << "..." << std::endl;
    
    setup_test_workspace(workspace);

    auto backend = std::make_shared<FakeBackend>();
    
    backend->set_script("write_file_task", {
        "{\"tool_call\": {\"name\": \"write_file\", \"arguments\": {\"path\": \"./test_workspace_1/test_file.txt\", \"content\": \"Hello E2E\"}}}"
    });

    auto tool_registry = std::make_shared<ToolRegistry>();
    register_filesystem_tools(*tool_registry);
    register_search_tools(*tool_registry);
    register_shell_tools(*tool_registry);

    ExecutionEngine engine(backend, tool_registry);
    
    auto result = engine.run(task);
    
    assert(result.is_ok());
    std::cout << "Task completed successfully." << std::endl;

    fs::path test_file(expected_file);
    assert(fs::exists(test_file));
    
    std::ifstream ifs(test_file);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    assert(content.find(expected_content) != std::string::npos);
    
    std::cout << "Test " << name << " PASSED!" << std::endl;
}

int main() {
    // Test 1: Simple File Creation
    run_e2e_test(
        "Simple File Creation",
        "write_file_task",
        "./test_workspace_1",
        "./test_workspace_1/test_file.txt",
        "Hello E2E"
    );

    // Test 2: Path Containment Safety (Negative Test)
    std::cout << "Running test: Path Containment Safety..." << std::endl;
    
    auto backend_safety = std::make_shared<FakeBackend>();
    backend_safety->set_script("forbidden_task", {
        "{\"tool_call\": {\"name\": \"read_file\", \"arguments\": {\"path\": \"/etc/passwd\"}}}"
    });

    auto tool_registry_safety = std::make_shared<ToolRegistry>();
    register_filesystem_tools(*tool_registry_safety);
    register_search_tools(*tool_registry_safety);
    register_shell_tools(*tool_registry_safety);

    ExecutionEngine engine_safety(backend_safety, tool_registry_safety);
    
    auto result_safety = engine_safety.run("forbidden_task");
    
    // The tool should fail because /etc/passwd is outside ./test_workspace_1
    assert(!result_safety.is_ok());
    std::cout << "Task failed as expected for forbidden path." << std::endl;
    std::cout << "Test Path Containment Safety PASSED!" << std::endl;

    // Test 3: Grep Search
    std::cout << "Running test: Grep Search..." << std::endl;
    
    setup_test_workspace("./test_workspace_1");
    std::ofstream test_file("./test_workspace_1/search_target.txt");
    test_file << "This is a test line.\nThis is another line.\nSearch me!";
    test_file.close();

    auto backend_grep = std::make_shared<FakeBackend>();
    backend_grep->set_script("grep_task", {
        "{\"tool_call\": {\"name\": \"grep\", \"arguments\": {\"path\": \"./test_workspace_1\", \"pattern\": \"Search me!\", \"recursive\": false}}}"
    });

    auto tool_registry_grep = std::make_shared<ToolRegistry>();
    register_filesystem_tools(*tool_registry_grep);
    register_search_tools(*tool_registry_grep);
    register_shell_tools(*tool_registry_grep);

    ExecutionEngine engine_grep(backend_grep, tool_registry_grep);
    
    auto result_grep = engine_grep.run("grep_task");
    
    assert(result_grep.is_ok());
    assert(result_grep.value().find("search_target.txt:3: Search me!") != std::string::npos);
    std::cout << "Test Grep Search PASSED!" << std::endl;

    return 0;
}
