// Desktop clipboard adapters start argv-only helpers with bounded plain-text input.
#include "clipboard.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char **environ;

namespace platypus::platform {
namespace {
constexpr std::size_t kMaxClipboardBytes = std::size_t{64} * 1024;
std::mutex child_mutex;
std::vector<pid_t> clipboard_children;

void reap_finished_children() {
    std::lock_guard lock(child_mutex);
    auto current = clipboard_children.begin();
    while (current != clipboard_children.end()) {
        int status = 0;
        auto result = ::waitpid(*current, &status, WNOHANG);
        if (result == *current || (result < 0 && errno == ECHILD))
            current = clipboard_children.erase(current);
        else
            ++current;
    }
}

bool successful_exit(int status) { return WIFEXITED(status) && WEXITSTATUS(status) == 0; }
} // namespace

util::Result<util::Void> copy_text_with_command(const std::vector<std::string> &argv,
                                                const std::string &text) try {
    using Return = util::Result<util::Void>;
    if (argv.empty() || argv.front().empty() || text.size() > kMaxClipboardBytes)
        return Return::fail("Invalid clipboard command or text exceeds 64 KiB");
    reap_finished_children();
    auto temporary =
        (std::filesystem::temp_directory_path() / "platypus-clipboard-XXXXXX").string();
    int input = ::mkstemp(temporary.data());
    if (input < 0)
        return Return::fail("Cannot create private clipboard input: " +
                            std::string(std::strerror(errno)));
    (void)::unlink(temporary.c_str());
    if (input <= STDERR_FILENO) {
        auto replacement = ::fcntl(input, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
        ::close(input);
        if (replacement < 0)
            return Return::fail("Cannot prepare clipboard input: " +
                                std::string(std::strerror(errno)));
        input = replacement;
    }
    (void)::fcntl(input, F_SETFD, FD_CLOEXEC);
    if (::fchmod(input, 0600) != 0) {
        auto message = std::string(std::strerror(errno));
        ::close(input);
        return Return::fail("Cannot secure clipboard input: " + message);
    }
    std::size_t offset = 0;
    while (offset < text.size()) {
        auto written = ::write(input, text.data() + offset, text.size() - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            auto message = std::string(std::strerror(errno));
            ::close(input);
            return Return::fail("Cannot write clipboard input: " + message);
        }
        offset += static_cast<std::size_t>(written);
    }
    if (::lseek(input, 0, SEEK_SET) < 0) {
        auto message = std::string(std::strerror(errno));
        ::close(input);
        return Return::fail("Cannot rewind clipboard input: " + message);
    }

    posix_spawn_file_actions_t actions;
    auto error = ::posix_spawn_file_actions_init(&actions);
    if (error != 0) {
        ::close(input);
        return Return::fail("Cannot initialize clipboard process: " +
                            std::string(std::strerror(error)));
    }
    error = ::posix_spawn_file_actions_adddup2(&actions, input, STDIN_FILENO);
    if (error == 0)
        error =
            ::posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    if (error == 0)
        error = ::posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    if (error == 0 && input != STDIN_FILENO)
        error = ::posix_spawn_file_actions_addclose(&actions, input);

    posix_spawnattr_t attributes;
    auto attr_error = ::posix_spawnattr_init(&attributes);
    const bool attributes_initialized = attr_error == 0;
    if (attr_error == 0)
        attr_error = ::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (attr_error == 0)
        attr_error = ::posix_spawnattr_setpgroup(&attributes, 0);
    if (error == 0)
        error = attr_error;

    std::vector<char *> arguments;
    arguments.reserve(argv.size() + 1);
    for (const auto &argument : argv)
        arguments.push_back(const_cast<char *>(argument.c_str()));
    arguments.push_back(nullptr);
    pid_t child = -1;
    if (error == 0)
        error =
            ::posix_spawnp(&child, arguments[0], &actions, &attributes, arguments.data(), environ);
    ::posix_spawn_file_actions_destroy(&actions);
    if (attributes_initialized)
        ::posix_spawnattr_destroy(&attributes);
    ::close(input);
    if (error != 0)
        return Return::fail("Cannot start clipboard provider: " +
                            std::string(std::strerror(error)));

    // Providers such as xclip remain alive as clipboard owners; short-lived providers exit here.
    for (int attempt = 0; attempt < 50; ++attempt) {
        int status = 0;
        auto waited = ::waitpid(child, &status, WNOHANG);
        if (waited == child)
            return successful_exit(status)
                       ? Return::ok()
                       : Return::fail("Clipboard provider rejected the review bundle");
        if (waited < 0 && errno != EINTR)
            return Return::fail("Cannot inspect clipboard provider status");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    {
        std::lock_guard lock(child_mutex);
        clipboard_children.push_back(child);
    }
    return Return::ok();
} catch (const std::exception &error) {
    return util::Result<util::Void>::fail("Cannot copy review bundle: " +
                                          std::string(error.what()));
}

util::Result<util::Void> copy_text(const std::string &text) {
    if (text.size() > kMaxClipboardBytes)
        return util::Result<util::Void>::fail("Review bundle exceeds the 64 KiB clipboard limit");
#ifdef __APPLE__
    return copy_text_with_command({"pbcopy"}, text);
#else
    auto wayland = copy_text_with_command({"wl-copy"}, text);
    if (wayland.is_ok())
        return wayland;
    auto xclip = copy_text_with_command({"xclip", "-selection", "clipboard", "-i"}, text);
    if (xclip.is_ok())
        return xclip;
    auto xsel = copy_text_with_command({"xsel", "--clipboard", "--input"}, text);
    if (xsel.is_ok())
        return xsel;
    return util::Result<util::Void>::fail("No clipboard provider succeeded (tried wl-copy, xclip, "
                                          "and xsel): " +
                                          xsel.error());
#endif
}

} // namespace platypus::platform
