// Persistent shadow-Git snapshots and explicit approvals for tool mutations.
// This compatibility facade is shared by the current core tools and execution engine.
#pragma once
#include "result.hpp"
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>
namespace platypus::util {
/// Owns workspace containment, approval, and a shadow repository independent of user Git.
class SnapshotManager {
  public:
    using Confirmer = std::function<bool(const std::string &)>;
    /// The confirmer must outlive calls; an absent confirmer denies all mutations/commands.
    explicit SnapshotManager(std::filesystem::path root = std::filesystem::current_path(),
                             Confirmer confirmer = {});
    /// Resolves a root-relative path; rejects escapes and internal state directories.
    Result<std::filesystem::path> resolve(const std::string &path) const;
    /// Captures the full workspace including untracked files, executable modes and symlinks.
    Result<bool> take_snapshot(const std::string &reason);
    /// Produces the diff from the latest snapshot to current files.
    Result<std::string> generate_diff(const std::string &unused = "");
    /// Previews and restores the most recent pre-mutation snapshot after confirmation.
    Result<bool> rollback(const std::string &unused = "");
    /// Shows a proposed file diff, asks permission, snapshots, then atomically replaces the file.
    Result<std::string> write(const std::string &path, const std::string &content);
    /// Presents an exact action; returns false when denied or no confirmer is installed.
    bool confirm(const std::string &action) const;
    void set_confirmer(Confirmer confirmer) { confirmer_ = std::move(confirmer); }
    const std::filesystem::path &root() const { return root_; }
    void set_stop_token(std::stop_token stop) { stop_ = stop; }
    std::stop_token stop_token() const { return stop_; }

  private:
    Result<std::string> git(const std::vector<std::string> &args) const;
    Result<bool> initialize();
    std::filesystem::path root_;
    Confirmer confirmer_;
    std::stop_token stop_;
};
} // namespace platypus::util
