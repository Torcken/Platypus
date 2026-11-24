// POSIX subprocess implementation for Linux and macOS.
// Uses posix_spawn process groups so cancellation also reaches shell children.
#include "process.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
namespace platypus::platform {
util::Result<ProcessResult> run_process(const std::vector<std::string> &argv,
                                        const std::filesystem::path &cwd,
                                        std::chrono::milliseconds timeout, std::size_t output_limit,
                                        std::stop_token stop, bool clean_git_environment) {
    using Return = util::Result<ProcessResult>;
    if (argv.empty() || timeout.count() <= 0)
        return Return::fail("Invalid process arguments");
    if (stop.stop_requested())
        return Return::fail("Cancelled");
    int pipes[2];
    if (pipe(pipes) != 0)
        return Return::fail(std::strerror(errno));
    for (int fd : pipes)
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipes[1], STDERR_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addclose(&actions, pipes[0]);
    posix_spawn_file_actions_addclose(&actions, pipes[1]);
    auto cwd_string = cwd.string();
    int directory_error = posix_spawn_file_actions_addchdir_np(&actions, cwd_string.c_str());
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    std::vector<char *> arguments;
    for (const auto &arg : argv)
        arguments.push_back(const_cast<char *>(arg.c_str()));
    arguments.push_back(nullptr);
    std::vector<std::string> environment;
    std::vector<char *> environment_pointers;
    if (clean_git_environment) {
        for (auto entry = environ; *entry; ++entry)
            if (!std::string_view(*entry).starts_with("GIT_"))
                environment.emplace_back(*entry);
        environment.emplace_back("GIT_CONFIG_GLOBAL=/dev/null");
        environment.emplace_back("GIT_CONFIG_NOSYSTEM=1");
        environment.emplace_back("GIT_TERMINAL_PROMPT=0");
        for (auto &value : environment)
            environment_pointers.push_back(value.data());
        environment_pointers.push_back(nullptr);
    }
    pid_t child = -1;
    int error = directory_error != 0
                    ? directory_error
                    : posix_spawnp(&child, arguments[0], &actions, &attributes, arguments.data(),
                                   clean_git_environment ? environment_pointers.data() : environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(pipes[1]);
    if (error != 0) {
        close(pipes[0]);
        return Return::fail(std::strerror(error));
    }
    fcntl(pipes[0], F_SETFL, O_NONBLOCK);
    ProcessResult result{"", 0, false};
    auto deadline = std::chrono::steady_clock::now() + timeout;
    bool exited = false;
    bool eof = false;
    int status = 0;
    while (!exited || !eof) {
        if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
            kill(-child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            close(pipes[0]);
            return Return::fail(stop.stop_requested() ? "Cancelled" : "Command timed out");
        }
        std::array<char, 4096> buffer{};
        ssize_t count;
        while ((count = read(pipes[0], buffer.data(), buffer.size())) > 0) {
            auto available = output_limit - result.output.size();
            auto length = static_cast<std::size_t>(count);
            result.output.append(buffer.data(), std::min(length, available));
            result.truncated = result.truncated || length > available;
        }
        eof = count == 0;
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            kill(-child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            close(pipes[0]);
            return Return::fail("Failed to read process output");
        }
        if (!exited)
            exited = waitpid(child, &status, WNOHANG) == child;
        if (!exited || !eof) {
            pollfd descriptor{pipes[0], POLLIN, 0};
            poll(&descriptor, 1, 20);
        }
    }
    kill(-child, SIGKILL); // Do not leave background descendants after the direct child exits.
    close(pipes[0]);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    if (result.truncated)
        result.output += "\n[output truncated]\n";
    return Return::ok(std::move(result));
}
} // namespace platypus::platform
