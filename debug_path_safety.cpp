#include <iostream>
#include <filesystem>
#include <string>
#include <algorithm>
#include <vector>

namespace fs = std::filesystem;

bool is_path_safe(const std::string& path_str) {
    try {
        fs::path workspace_root = fs::absolute("./test_workspace_1");
        fs::path target_path = fs::absolute(path_str);

        std::cout << "Workspace Root: " << workspace_root << std::endl;
        std::cout << "Target Path: " << target_path << std::endl;

        auto [it_root, it_target] = std::mismatch(workspace_root.begin(), workspace_root.end(), target_path.begin());
        
        std::cout << "Mismatch Root Iterator: " << (it_root == workspace_root.end() ? "end" : "not end") << std::endl;
        
        // Let's print the components to be sure
        std::cout << "Workspace Components: ";
        for (const auto& c : workspace_root) std::cout << c << " ";
        std::cout << std::endl;
        std::cout << "Target Components: ";
        for (const auto& c : target_path) std::cout << c << " ";
        std::cout << std::endl;

        return it_root == workspace_root.end();
    } catch (...) {
        return false;
    }
}

int main() {
    std::cout << "--- Test 1 (/etc/passwd) ---" << std::endl;
    std::cout << "Result: " << (is_path_safe("/etc/passwd") ? "SAFE" : "UNSAFE") << std::endl;
    std::cout << "--- Test 2 (./test_workspace_1/file.txt) ---" << std::endl;
    std::cout << "Result: " << (is_path_safe("./test_workspace_1/file.txt") ? "SAFE" : "UNSAFE") << std::endl;
    return 0;
}
