// SQLite append-only conversation log for resume, independent of snapshot Git.
// The execution engine records each accepted turn before adding it to live context.
#pragma once
#include "util/result.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
struct sqlite3;
namespace platypus::session {
using History = std::vector<std::pair<std::string, std::string>>;
/// Owns one serialized SQLite connection; use only from the agent thread.
class Store {
  public:
    explicit Store(const std::filesystem::path &root);
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    /// Create an empty session and return its positive integer ID as a string.
    util::Result<std::string> create(const std::string &title);
    /// Append one turn atomically; existing log entries are never rewritten.
    util::Result<bool> append(const std::string &id, const std::string &role,
                              const std::string &text);
    /// Load an existing session; an empty ID selects the most recent session.
    util::Result<std::pair<std::string, History>> load(const std::string &id);

  private:
    sqlite3 *database_ = nullptr;
};
} // namespace platypus::session
