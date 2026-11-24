// Runs bounded child processes for tools and snapshot Git operations.
// Platform details stay here; callers receive output and an exit status through Result.
#pragma once
#include "util/result.hpp"
#include <chrono>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>
namespace platypus::platform {
/// Captured combined output, exit code, and whether the output was capped.
struct ProcessResult {
    std::string output;
    int exit_code;
    bool truncated;
};
/// Executes an argv vector in cwd, terminating the process group on timeout/cancellation.
util::Result<ProcessResult>
run_process(const std::vector<std::string> &argv, const std::filesystem::path &cwd,
            std::chrono::milliseconds timeout = std::chrono::seconds(30),
            std::size_t output_limit = 6000, std::stop_token stop = {},
            bool clean_git_environment = false);
} // namespace platypus::platform
