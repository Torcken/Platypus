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

/// Detect project checks or load argv-only overrides from `.platypus.toml`.
class ProjectChecks {
  public:
    using Confirmer = std::function<bool(const std::string &)>;

    static util::Result<std::vector<ProjectCheck>> discover(const std::filesystem::path &root);
    static util::Result<ProjectCheckResult> run(const ProjectCheck &check,
                                                const std::filesystem::path &root,
                                                const Confirmer &confirm,
                                                std::stop_token stop = {});
};

} // namespace platypus::core

#endif // PLATYPUS_CORE_PROJECT_CHECKS_HPP
