// Interactive plan commands share validation and approval rules across CLI and TUI.
#include "plan_commands.hpp"
#include "plan_store.hpp"
#include <charconv>
#include <cstddef>

namespace platypus::core {
namespace {
constexpr std::size_t kMaxReviewBytes = std::size_t{64} * 1024;

std::optional<std::string> after_prefix(const std::string &input, std::string_view prefix) {
    if (!input.starts_with(prefix))
        return std::nullopt;
    return input.substr(prefix.size());
}

std::string failure(const std::string &message) { return "Plan: " + message + "\n"; }
} // namespace

std::optional<std::string> handle_plan_command(const std::string &input,
                                               safety::SnapshotStore &workspace) {
    PlanStore plan(workspace.root());
    if (input == "/plan") {
        auto loaded = plan.load();
        if (!loaded.is_ok())
            return failure(loaded.error());
        if (loaded.value().empty())
            return "No project plan. Create a milestone with /milestone <title>.\n";
        auto rendered = PlanStore::render(loaded.value());
        return rendered.is_ok() ? rendered.value() : failure(rendered.error());
    }
    if (auto title = after_prefix(input, "/milestone ")) {
        if (title->empty())
            return failure("milestone title is required");
        if (!workspace.confirm("Create project milestone?\n## Milestone: " + *title))
            return "Milestone creation declined.\n";
        auto result = plan.add_milestone(*title);
        if (!result.is_ok())
            return failure(result.error());
        auto baseline = workspace.snapshot("Milestone baseline: " + *title);
        if (!baseline.is_ok())
            return "Added milestone, but could not capture its review baseline: " +
                   baseline.error() + "\n";
        auto current = plan.load();
        if (!current.is_ok() || current.value().empty())
            return "Added milestone, but could not attach its review baseline.\n";
        auto attached = plan.set_milestone_baseline(current.value().size() - 1, baseline.value());
        if (!attached.is_ok())
            return "Added milestone, but could not attach its review baseline: " +
                   attached.error() + "\n";
        return "Added milestone and captured its review baseline: " + *title + "\n";
    }
    if (auto task = after_prefix(input, "/task ")) {
        auto separator = task->find(' ');
        if (separator == std::string::npos)
            return failure("usage: /task <milestone-number> <description>");
        std::size_t index = 0;
        auto parsed = std::from_chars(task->data(), task->data() + separator, index);
        if (parsed.ec != std::errc{} || parsed.ptr != task->data() + separator || index == 0)
            return failure("milestone number must be a positive integer");
        auto description = task->substr(separator + 1);
        if (!workspace.confirm("Add plan task to milestone " + std::to_string(index) + "?\n- [ ] " +
                               description))
            return "Task creation declined.\n";
        auto result = plan.add_task(index - 1, std::move(description));
        return result.is_ok() ? "Added " + result.value() + "\n" : failure(result.error());
    }
    if (auto id = after_prefix(input, "/done ")) {
        if (!workspace.confirm("Mark plan task complete?\n" + *id))
            return "Task update declined.\n";
        auto result = plan.set_task_complete(*id, true);
        return result.is_ok() ? "Completed " + *id + "\n" : failure(result.error());
    }
    if (auto path = after_prefix(input, "/import ")) {
        auto result = plan.import_file(
            *path, [&](const auto &preview) { return workspace.confirm(preview); });
        return result.is_ok() ? "Imported plan tasks into PLAN.md\n" : failure(result.error());
    }
    if (input == "/review" || input.starts_with("/review ")) {
        auto loaded = plan.load();
        if (!loaded.is_ok())
            return failure(loaded.error());
        if (loaded.value().empty())
            return failure("create a milestone before requesting a review");
        std::size_t index = loaded.value().size() - 1;
        if (input != "/review") {
            auto number = input.substr(8);
            std::size_t selected = 0;
            auto parsed = std::from_chars(number.data(), number.data() + number.size(), selected);
            if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() ||
                selected == 0 || selected > loaded.value().size())
                return failure("usage: /review [milestone-number]");
            if (selected != loaded.value().size())
                return failure("only the latest milestone has a current review baseline");
            index = selected - 1;
        }
        const auto &milestone = loaded.value()[index];
        if (milestone.baseline_snapshot.empty())
            return failure("the latest milestone has no review baseline; create a new milestone");
        auto diff = workspace.generate_diff(milestone.baseline_snapshot);
        if (!diff.is_ok())
            return failure(diff.error());
        auto plan_excerpt = PlanStore::render({milestone});
        if (!plan_excerpt.is_ok())
            return failure(plan_excerpt.error());
        std::string bundle = "# Milestone Review: " + milestone.title + "\n\n" + "## Plan\n\n" +
                             plan_excerpt.value() +
                             "\n## Workspace diff since milestone baseline\n\n";
        if (diff.value().empty())
            bundle += "No workspace changes since the milestone baseline.\n";
        else
            bundle += diff.value();
        bundle += "\n## Checks\n\nNo check results are stored in this plan yet.\n";
        bundle += "\n## Decisions\n\nNo milestone decisions are recorded yet.\n";
        if (bundle.size() > kMaxReviewBytes) {
            bundle.resize(kMaxReviewBytes - 32);
            auto last_line = bundle.find_last_of('\n');
            if (last_line != std::string::npos)
                bundle.resize(last_line + 1);
            bundle += "\n[Review bundle truncated]\n";
        }
        return bundle;
    }
    return std::nullopt;
}

} // namespace platypus::core
