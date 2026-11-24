// Implements durable snapshots under .platypus/snapshots, without touching user Git.
// Tools call write(); the CLI calls rollback() for an approved undo operation.
#include "snapshot.hpp"
#include "platform/process.hpp"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
namespace platypus::util {
namespace fs = std::filesystem;
namespace {
nlohmann::json undo_stack(const fs::path &root) {
    auto file = root / ".platypus/undo.json";
    if (!fs::exists(file))
        return nlohmann::json::array();
    std::ifstream input(file);
    auto result = nlohmann::json::parse(input);
    if (!result.is_array())
        throw std::runtime_error("Invalid undo stack");
    return result;
}
void save_stack(const fs::path &root, const nlohmann::json &stack) {
    auto file = root / ".platypus/undo.json";
    auto temporary = root / ".platypus/undo.json.tmp";
    if (fs::is_symlink(file) || fs::is_symlink(temporary))
        throw std::runtime_error("Unsafe undo metadata");
    std::ofstream output(temporary);
    output << stack.dump();
    output.close();
    if (!output)
        throw std::runtime_error("Cannot persist undo stack");
    fs::rename(temporary, file);
}
// Git stores executable bits but not all POSIX permissions or empty directories.
// A sidecar restores these properties without changing the user's repository metadata.
nlohmann::json capture_modes(const fs::path &root) {
    nlohmann::json modes{{"files", nlohmann::json::object()},
                         {"directories", nlohmann::json::object()}};
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator();
         ++it) {
        if (it->path().filename() == ".git" || it->path().filename() == ".platypus") {
            it.disable_recursion_pending();
            continue;
        }
        auto status = it->symlink_status();
        if (fs::is_symlink(status))
            continue;
        auto key = it->path().lexically_relative(root).generic_string();
        if (fs::is_regular_file(status) || fs::is_directory(status))
            modes[fs::is_directory(status) ? "directories" : "files"][key] =
                static_cast<unsigned>(status.permissions());
    }
    return modes;
}
void save_modes(const fs::path &root, const std::string &revision) {
    auto modes = capture_modes(root);
    auto directory = root / ".platypus/snapshot-modes";
    if (fs::is_symlink(directory))
        throw std::runtime_error("Unsafe snapshot metadata directory");
    fs::create_directories(directory);
    auto file = directory / (revision + ".json");
    if (fs::is_symlink(file))
        throw std::runtime_error("Unsafe snapshot mode metadata");
    std::ofstream output(file);
    output << modes.dump();
    output.close();
    if (!output)
        throw std::runtime_error("Cannot save snapshot permissions");
}
void restore_modes(const fs::path &root, const std::string &revision) {
    std::ifstream input(root / ".platypus/snapshot-modes" / (revision + ".json"));
    auto modes = nlohmann::json::parse(input);
    auto safe = [&](const std::string &relative) {
        auto path = fs::path(relative);
        if (path.empty() || path.is_absolute())
            throw std::runtime_error("Invalid snapshot mode path");
        for (const auto &component : path)
            if (component == ".." || component == ".git" || component == ".platypus")
                throw std::runtime_error("Unsafe snapshot mode path");
        auto resolved = fs::weakly_canonical(root / path);
        auto inside = resolved.lexically_relative(root);
        if (inside.empty() || *inside.begin() == "..")
            throw std::runtime_error("Snapshot mode symlink escape");
        return root / path;
    };
    std::vector<fs::path> directories;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator();
         ++it) {
        if (it->path().filename() == ".git" || it->path().filename() == ".platypus") {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory() && !it->is_symlink())
            directories.push_back(it->path());
    }
    std::sort(directories.rbegin(), directories.rend());
    for (const auto &directory : directories)
        if (!modes.at("directories").contains(directory.lexically_relative(root).generic_string()))
            fs::remove(
                directory); // Refuses nonempty directories rather than losing unexpected work.
    for (auto it = modes.at("directories").begin(); it != modes.at("directories").end(); ++it)
        fs::create_directories(safe(it.key()));
    for (auto it = modes.at("files").begin(); it != modes.at("files").end(); ++it) {
        auto path = safe(it.key());
        if (!fs::is_regular_file(path) || fs::is_symlink(path))
            throw std::runtime_error("Snapshot file type changed");
        fs::permissions(path, static_cast<fs::perms>(it.value().get<unsigned>()));
    }
    for (auto it = modes.at("directories").rbegin(); it != modes.at("directories").rend(); ++it)
        fs::permissions(safe(it.key()), static_cast<fs::perms>(it.value().get<unsigned>()));
}
} // namespace
SnapshotManager::SnapshotManager(fs::path root, Confirmer confirmer)
    : root_(fs::canonical(std::move(root))), confirmer_(std::move(confirmer)) {}
Result<fs::path> SnapshotManager::resolve(const std::string &path) const {
    try {
        if (path.empty())
            return Result<fs::path>::fail("Path is required");
        auto input = fs::path(path);
        auto resolved = fs::weakly_canonical(input.is_absolute() ? input : root_ / input);
        auto relative = resolved.lexically_relative(root_);
        if (relative.empty() || *relative.begin() == ".." || relative.is_absolute())
            return Result<fs::path>::fail("Safety Error: Path is outside workspace");
        for (const auto &part : relative)
            if (part == ".git" || part == ".platypus")
                return Result<fs::path>::fail("Safety Error: Protected state path");
        return Result<fs::path>::ok(resolved);
    } catch (const std::exception &e) {
        return Result<fs::path>::fail(e.what());
    }
}
bool SnapshotManager::confirm(const std::string &action) const {
    return !stop_.stop_requested() && confirmer_ && confirmer_(action);
}
Result<std::string> SnapshotManager::git(const std::vector<std::string> &args) const {
    std::vector<std::string> command{"git",
                                     "--git-dir=" + (root_ / ".platypus/snapshots").string(),
                                     "--work-tree=" + root_.string(),
                                     "-c",
                                     "core.hooksPath=/dev/null",
                                     "-c",
                                     "commit.gpgSign=false",
                                     "-c",
                                     "core.autocrlf=false",
                                     "-c",
                                     "core.fsmonitor=false",
                                     "-c",
                                     "user.name=Platypus",
                                     "-c",
                                     "user.email=platypus@localhost"};
    command.insert(command.end(), args.begin(), args.end());
    auto result =
        platform::run_process(command, root_, std::chrono::seconds(30), 1024U * 1024U, stop_, true);
    if (!result.is_ok())
        return Result<std::string>::fail(result.error());
    if (result.value().exit_code != 0 || result.value().truncated)
        return Result<std::string>::fail("Snapshot Git failed: " + result.value().output);
    return Result<std::string>::ok(result.value().output);
}
Result<bool> SnapshotManager::initialize() {
    try {
        if (fs::is_symlink(root_ / ".platypus") || fs::is_symlink(root_ / ".platypus/snapshots"))
            return Result<bool>::fail("Snapshot storage cannot be a symlink");
        fs::create_directories(root_ / ".platypus/snapshots");
        fs::permissions(root_ / ".platypus", fs::perms::owner_all);
        if (!fs::exists(root_ / ".platypus/snapshots/HEAD")) {
            auto initialized = git({"init", "--quiet"});
            if (!initialized.is_ok())
                return Result<bool>::fail(initialized.error());
        }
        return Result<bool>::ok(true);
    } catch (const std::exception &e) {
        return Result<bool>::fail(e.what());
    }
}
Result<bool> SnapshotManager::take_snapshot(const std::string &reason) {
    auto initialized = initialize();
    if (!initialized.is_ok())
        return initialized;
    auto staged =
        git({"add", "--all", "--force", "--", ".", ":(exclude).git", ":(exclude).platypus"});
    if (!staged.is_ok())
        return Result<bool>::fail(staged.error());
    auto commit = git({"commit", "--quiet", "--allow-empty", "-m", reason});
    if (!commit.is_ok())
        return Result<bool>::fail(commit.error());
    auto revision = git({"rev-parse", "HEAD"});
    if (!revision.is_ok())
        return Result<bool>::fail(revision.error());
    try {
        auto id = revision.value();
        id.erase(id.find_last_not_of("\r\n") + 1);
        save_modes(root_, id);
        auto stack = undo_stack(root_);
        stack.push_back(id);
        save_stack(root_, stack);
        return Result<bool>::ok(true);
    } catch (const std::exception &e) {
        return Result<bool>::fail(e.what());
    }
}
Result<std::string> SnapshotManager::generate_diff(const std::string &) {
    auto staged =
        git({"add", "--all", "--force", "--", ".", ":(exclude).git", ":(exclude).platypus"});
    if (!staged.is_ok())
        return Result<std::string>::fail(staged.error());
    return git({"diff", "--cached", "--no-ext-diff", "--no-textconv", "HEAD", "--", ".",
                ":(exclude).platypus", ":(exclude).git"});
}
Result<std::string> SnapshotManager::write(const std::string &path, const std::string &content) {
    auto resolved = resolve(path);
    if (!resolved.is_ok())
        return Result<std::string>::fail(resolved.error());
    auto target = resolved.value();
    try {
        if (!fs::is_directory(target.parent_path()))
            return Result<std::string>::fail("Parent directory does not exist");
        std::string old;
        if (fs::exists(target)) {
            if (!fs::is_regular_file(target))
                return Result<std::string>::fail("Target is not a regular file");
            std::ifstream input(target, std::ios::binary);
            if (!input)
                return Result<std::string>::fail("Cannot read target");
            old.assign(std::istreambuf_iterator<char>(input), {});
        }
        if (old == content && fs::exists(target))
            return Result<std::string>::ok("No change");
        std::string preview = "--- " + path + "\n+++ " + path + "\n";
        auto count_lines = [](const std::string &text) {
            return std::count(text.begin(), text.end(), '\n') +
                   (!text.empty() && text.back() != '\n' ? 1 : 0);
        };
        preview += "@@ -" + std::string(old.empty() ? "0," : "1,") +
                   std::to_string(count_lines(old)) + " +" +
                   std::string(content.empty() ? "0," : "1,") +
                   std::to_string(count_lines(content)) + " @@\n";
        for (auto [text, prefix] : {std::pair{old, '-'}, std::pair{content, '+'}}) {
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line))
                preview += prefix + line + "\n";
            if (!text.empty() && text.back() != '\n')
                preview += "\\ No newline at end of file\n";
        }
        if (!confirm(preview))
            return Result<std::string>::fail("Permission denied");
        auto saved = take_snapshot("Before writing " + path);
        if (!saved.is_ok())
            return Result<std::string>::fail(saved.error());
        // Re-check after the approval callback, which can run arbitrary UI code.
        auto checked = resolve(path);
        if (!checked.is_ok() || checked.value() != target)
            return Result<std::string>::fail("Path changed during approval");
        auto temporary = target;
        temporary += ".platypus-tmp";
        if (fs::exists(temporary) || fs::is_symlink(temporary))
            return Result<std::string>::fail("Temporary path already exists");
        std::ofstream output(temporary, std::ios::binary);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.close();
        if (!output) {
            fs::remove(temporary);
            return Result<std::string>::fail("Write failed");
        }
        if (fs::exists(target))
            fs::permissions(temporary, fs::status(target).permissions());
        fs::rename(temporary, target);
        std::ofstream log(root_ / ".platypus/actions.jsonl", std::ios::app);
        log << nlohmann::json{{"action", "write"}, {"path", path}}.dump() << '\n';
        return Result<std::string>::ok("File written: " + path);
    } catch (const std::exception &e) {
        return Result<std::string>::fail(e.what());
    }
}
Result<bool> SnapshotManager::rollback(const std::string &) {
    try {
        auto stack = undo_stack(root_);
        if (stack.empty())
            return Result<bool>::fail("No snapshot available");
        auto revision = stack.back().get<std::string>();
        auto staged =
            git({"add", "--all", "--force", "--", ".", ":(exclude).git", ":(exclude).platypus"});
        if (!staged.is_ok())
            return Result<bool>::fail(staged.error());
        auto diff = git({"diff", "--cached", "--no-ext-diff", "--no-textconv", "-R", revision});
        if (!diff.is_ok())
            return Result<bool>::fail(diff.error());
        std::ifstream stored(root_ / ".platypus/snapshot-modes" / (revision + ".json"));
        auto target_modes = nlohmann::json::parse(stored);
        auto current_modes = capture_modes(root_);
        std::string metadata_preview;
        for (const auto &group : {"files", "directories"}) {
            auto keys = current_modes.at(group);
            keys.update(target_modes.at(group));
            for (auto it = keys.begin(); it != keys.end(); ++it) {
                auto before = current_modes.at(group).value(it.key(), nlohmann::json(nullptr));
                auto after = target_modes.at(group).value(it.key(), nlohmann::json(nullptr));
                if (before != after)
                    metadata_preview += std::string(group) + " " + it.key() + ": " + before.dump() +
                                        " -> " + after.dump() +
                                        " (permission bits; null means absent)\n";
            }
        }
        if (!confirm("Restore snapshot?\n" + diff.value() + metadata_preview))
            return Result<bool>::fail("Permission denied");
        auto backup = take_snapshot("Before undo");
        if (!backup.is_ok())
            return backup;
        auto restored = git({"read-tree", "--reset", "-u", revision});
        if (restored.is_ok()) {
            try {
                restore_modes(root_, revision);
            } catch (const std::exception &e) {
                restored = Result<std::string>::fail(e.what());
            }
        }
        if (!restored.is_ok()) {
            auto recovery = git({"read-tree", "--reset", "-u", "HEAD"});
            if (recovery.is_ok()) {
                auto backup_revision = git({"rev-parse", "HEAD"});
                try {
                    if (!backup_revision.is_ok())
                        throw std::runtime_error(backup_revision.error());
                    auto id = backup_revision.value();
                    id.erase(id.find_last_not_of("\r\n") + 1);
                    restore_modes(root_, id);
                } catch (const std::exception &e) {
                    recovery = Result<std::string>::fail(e.what());
                }
            }
            save_stack(root_, stack);
            return Result<bool>::fail(
                restored.error() + (recovery.is_ok() ? "; original state restored"
                                                     : "; recovery failed: " + recovery.error()));
        }
        auto recorded = git({"commit", "--quiet", "--allow-empty", "-m", "Undo"});
        if (!recorded.is_ok())
            return Result<bool>::fail(recorded.error());
        stack.erase(stack.size() - 1);
        save_stack(root_, stack);
        return Result<bool>::ok(true);
    } catch (const std::exception &e) {
        return Result<bool>::fail(e.what());
    }
}
} // namespace platypus::util
