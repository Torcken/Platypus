// Interactive plan commands share validation and approval rules across CLI and TUI.
#include "plan_commands.hpp"
#include "plan_store.hpp"
#include <charconv>

namespace platypus::core {
namespace {
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
        return result.is_ok() ? "Added milestone: " + *title + "\n" : failure(result.error());
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
    return std::nullopt;
}

} // namespace platypus::core
