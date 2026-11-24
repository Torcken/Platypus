#ifndef PLATYPUS_CORE_PLAN_STORE_HPP
#define PLATYPUS_CORE_PLAN_STORE_HPP

#include "../util/result.hpp"
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace platypus::core {

struct PlanTask {
    std::string id;
    std::string description;
    bool complete = false;
};

struct PlanMilestone {
    std::string title;
    std::vector<PlanTask> tasks;
    std::string baseline_snapshot;
};

using ProjectPlan = std::vector<PlanMilestone>;

/// Reads and atomically writes the constrained, project-local PLAN.md format.
class PlanStore {
  public:
    explicit PlanStore(std::filesystem::path project_root);

    util::Result<ProjectPlan> load() const;
    util::Result<util::Void> save(const ProjectPlan &plan) const;
    util::Result<util::Void> add_milestone(std::string title) const;
    util::Result<util::Void> set_milestone_baseline(std::size_t milestone_index,
                                                    const std::string &snapshot_id) const;
    util::Result<std::string> add_task(std::size_t milestone_index, std::string description) const;
    util::Result<util::Void> set_task_complete(const std::string &task_id, bool complete) const;
    /// Append an imported plan, assigning fresh task IDs before writing it.
    util::Result<util::Void> merge_import(std::string_view markdown) const;
    /// Read and preview a project-relative Markdown file before merging its plan.
    util::Result<util::Void>
    import_file(const std::filesystem::path &relative_path,
                const std::function<bool(const std::string &)> &confirm) const;

    static util::Result<ProjectPlan> parse(std::string_view markdown);
    static util::Result<std::string> render(const ProjectPlan &plan);

  private:
    std::filesystem::path root_;
};

} // namespace platypus::core

#endif // PLATYPUS_CORE_PLAN_STORE_HPP
