// Interactive plan commands share validation and approval rules across CLI and TUI.
#include "plan_commands.hpp"
#include "plan_store.hpp"
#include "platform/clipboard.hpp"
#include "project_checks.hpp"
#include "review_store.hpp"
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <iomanip>
#include <sstream>

namespace platypus::core {
namespace {
constexpr std::size_t kMaxReviewBytes = std::size_t{64} * 1024;

std::optional<std::string> after_prefix(const std::string &input, std::string_view prefix) {
    if (!input.starts_with(prefix))
        return std::nullopt;
    return input.substr(prefix.size());
}

std::string failure(const std::string &message) { return "Plan: " + message + "\n"; }

bool contains_control(const std::string &text) {
    return std::any_of(text.begin(), text.end(),
                       [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

bool valid_decision_text(const std::string &text) {
    return !text.empty() && text.size() <= 1024 && !contains_control(text);
}
} // namespace

std::optional<std::string> handle_plan_command(
    const std::string &input, safety::SnapshotStore &workspace,
    const std::function<util::Result<std::string>(const std::string &)> &remote_review) {
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
    if (auto decision = after_prefix(input, "/decision ")) {
        if (!valid_decision_text(*decision))
            return failure("decision must be a non-empty, single-line value up to 1024 bytes");
        auto loaded = plan.load();
        if (!loaded.is_ok())
            return failure(loaded.error());
        if (loaded.value().empty() || loaded.value().back().baseline_snapshot.empty())
            return failure("create a milestone with a review baseline before recording decisions");
        if (!workspace.confirm("Record this milestone decision?\n- " + *decision))
            return "Decision recording declined.\n";
        ReviewStore reviews(workspace.root());
        auto result = reviews.add_decision(loaded.value().back().baseline_snapshot, *decision);
        return result.is_ok() ? "Recorded milestone decision.\n" : failure(result.error());
    }
    if (auto path = after_prefix(input, "/review-import ")) {
        auto loaded = plan.load();
        if (!loaded.is_ok())
            return failure(loaded.error());
        if (loaded.value().empty())
            return failure("create a milestone before importing review comments");
        auto result =
            plan.import_review_comments(*path, loaded.value().size() - 1, [&](const auto &preview) {
                return workspace.confirm(preview);
            });
        if (!result.is_ok())
            return failure(result.error());
        std::string response = "Imported review comments as plan tasks:\n";
        for (const auto &id : result.value())
            response += "- " + id + "\n";
        return response;
    }
    if (input == "/review" || input == "/review --copy" || input == "/review --remote" ||
        input.starts_with("/review ")) {
        const bool copy_bundle = input == "/review --copy";
        const bool ask_remote = input == "/review --remote";
        auto loaded = plan.load();
        if (!loaded.is_ok())
            return failure(loaded.error());
        if (loaded.value().empty())
            return failure("create a milestone before requesting a review");
        std::size_t index = loaded.value().size() - 1;
        if (input != "/review" && !copy_bundle && !ask_remote) {
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
        bundle += "\n## Checks from the latest task\n\n";
        auto check_records = ProjectChecks::load_results(workspace.root());
        if (!check_records.is_ok()) {
            bundle += "Check results unavailable: " + check_records.error() + "\n";
        } else if (check_records.value().empty()) {
            bundle += "No project check results are stored yet.\n";
        } else {
            for (const auto &record : check_records.value()) {
                std::ostringstream quoted_name;
                quoted_name << std::quoted(record.name);
                bundle += "- " + quoted_name.str() + ": " + record.status;
                if (record.has_exit_code)
                    bundle += " (exit " + std::to_string(record.exit_code) + ")";
                bundle += "\n";
            }
        }
        bundle += "\n## Decisions\n\n";
        ReviewStore reviews(workspace.root());
        auto decisions = reviews.decisions(milestone.baseline_snapshot);
        if (!decisions.is_ok()) {
            bundle += "Decisions unavailable: " + decisions.error() + "\n";
        } else if (decisions.value().empty()) {
            bundle += "No milestone decisions are recorded yet.\n";
        } else {
            for (const auto &decision : decisions.value())
                bundle += "- " + decision + "\n";
        }
        if (bundle.size() > kMaxReviewBytes) {
            bundle.resize(kMaxReviewBytes - 32);
            auto last_line = bundle.find_last_of('\n');
            if (last_line != std::string::npos)
                bundle.resize(last_line + 1);
            bundle += "\n[Review bundle truncated]\n";
        }
        if (copy_bundle) {
            if (!workspace.confirm("Copy this milestone review bundle to the system clipboard?"))
                return bundle + "\nClipboard copy declined.\n";
            auto copied = platform::copy_text(bundle);
            if (!copied.is_ok())
                return bundle + "\nClipboard copy failed: " + copied.error() + "\n";
            bundle += "\nCopied review bundle to clipboard.\n";
        }
        if (ask_remote) {
            if (!remote_review)
                return failure("remote review is unavailable");
            auto feedback = remote_review(bundle);
            if (!feedback.is_ok())
                return bundle + "\nRemote review failed: " + feedback.error() + "\n";
            auto bounded = feedback.value();
            if (bounded.size() > kMaxReviewBytes) {
                bounded.resize(kMaxReviewBytes);
                bounded += "\n[Remote feedback truncated]\n";
            }
            bundle += "\n## Remote review feedback\n\n" + bounded + "\n";
        }
        return bundle;
    }
    return std::nullopt;
}

util::Result<std::optional<std::vector<std::string>>>
parse_plan_decision(const std::string &input, const std::vector<std::string> &proposal) {
    using Return = util::Result<std::optional<std::vector<std::string>>>;
    auto trim = [](const std::string &value) {
        auto first = value.find_first_not_of(" \t");
        if (first == std::string::npos)
            return std::string{};
        auto last = value.find_last_not_of(" \t");
        return value.substr(first, last - first + 1);
    };
    auto decision = trim(input);
    if (decision == "y" || decision == "yes")
        return Return::ok(proposal);
    if (decision == "n" || decision == "no")
        return Return::ok(std::nullopt);
    if (!decision.starts_with("e:"))
        return Return::fail("Enter y to approve, n to reject, or e: step | step to edit");
    std::vector<std::string> edited;
    std::string_view remaining(decision);
    remaining.remove_prefix(2);
    while (true) {
        auto separator = remaining.find('|');
        auto step = trim(std::string(remaining.substr(0, separator)));
        if (step.empty() || step.size() > 240 || contains_control(step))
            return Return::fail("Edited steps must be non-empty and at most 240 characters");
        edited.push_back(std::move(step));
        if (edited.size() > 12)
            return Return::fail("An approved plan can contain at most 12 steps");
        if (separator == std::string_view::npos)
            break;
        remaining.remove_prefix(separator + 1);
    }
    return Return::ok(std::move(edited));
}

} // namespace platypus::core
