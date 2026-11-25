#ifndef PLATYPUS_CORE_PROJECT_CHECKS_HPP
#define PLATYPUS_CORE_PROJECT_CHECKS_HPP

#include "../platform/process.hpp"
#include "../util/result.hpp"
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

namespace platypus::core {

struct ProjectCheck {
    std::string name;
    std::vector<std::string> argv;
};

struct ProjectCheckResult {
    bool approved = false;
    platform::ProcessResult process{"", 0, false};
};

struct ProjectCheckRecord {
    std::string name;
    std::string status;
    int exit_code = 0;
    bool has_exit_code = false;
};

/// Detect project checks or load argv-only overrides from `.platypus.toml`.
class ProjectChecks {
  public:
    using Confirmer = std::function<bool(const std::string &)>;

    static util::Result<std::vector<ProjectCheck>> discover(const std::filesystem::path &root);
    static util::Result<ProjectCheckResult> run(const ProjectCheck &check,
                                                const std::filesystem::path &root,
                                                const Confirmer &confirm,
                                                std::stop_token stop = {});
    static util::Result<util::Void> store_results(const std::filesystem::path &root,
                                                  const std::vector<ProjectCheckRecord> &records);
    static util::Result<std::vector<ProjectCheckRecord>>
    load_results(const std::filesystem::path &root);
};

} // namespace platypus::core

#endif // PLATYPUS_CORE_PROJECT_CHECKS_HPP
