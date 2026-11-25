// Constrained Markdown plans keep task state reviewable without executing imported content.
#include "plan_store.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace platypus::core {
namespace {
constexpr std::size_t kMaxPlanBytes = std::size_t{1024} * 1024;
constexpr std::size_t kMaxMilestones = 100;
constexpr std::size_t kMaxTasks = 1000;
constexpr std::size_t kMaxTextBytes = 500;
constexpr std::string_view kHeader = "# Platypus Plan";

bool valid_text(const std::string &text) {
    return !text.empty() && text.size() <= kMaxTextBytes &&
           std::none_of(text.begin(), text.end(),
                        [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

bool valid_task_id(const std::string &id) {
    if (!id.starts_with("TASK-") || id.size() < 9 || id.size() > 13)
        return false;
    for (std::size_t i = 5; i < id.size(); ++i)
        if (id[i] < '0' || id[i] > '9')
            return false;
    return true;
}

bool valid_snapshot_id(const std::string &id) {
    return (id.size() == 40 || id.size() == 64) &&
           std::all_of(id.begin(), id.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

util::Result<std::size_t> next_task_number(const ProjectPlan &plan) {
    std::size_t next = 1;
    for (const auto &milestone : plan)
        for (const auto &task : milestone.tasks)
            try {
                auto number = static_cast<std::size_t>(std::stoull(task.id.substr(5)));
                next = std::max(next, number + 1);
            } catch (const std::exception &) {
                return util::Result<std::size_t>::fail("Existing plan has an invalid task ID");
            }
    return util::Result<std::size_t>::ok(next);
}

std::string task_id(std::size_t number) {
    std::ostringstream id;
    id << "TASK-" << std::setw(4) << std::setfill('0') << number;
    return id.str();
}
} // namespace

PlanStore::PlanStore(std::filesystem::path project_root) : root_(std::move(project_root)) {}

util::Result<ProjectPlan> PlanStore::parse(std::string_view markdown) try {
    using Return = util::Result<ProjectPlan>;
    if (markdown.size() > kMaxPlanBytes)
        return Return::fail("Plan exceeds the 1 MiB limit");
    std::istringstream input{std::string(markdown)};
    std::string line;
    if (!std::getline(input, line) || line != kHeader)
        return Return::fail("Plan must begin with '# Platypus Plan'");
    ProjectPlan plan;
    std::set<std::string> ids;
    std::size_t task_count = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        constexpr std::string_view milestone_prefix = "## Milestone: ";
        constexpr std::string_view baseline_prefix = "<!-- baseline: ";
        constexpr std::string_view open_task_prefix = "- [ ] ";
        constexpr std::string_view done_task_prefix = "- [x] ";
        if (line.starts_with(milestone_prefix)) {
            auto title = line.substr(milestone_prefix.size());
            if (!valid_text(title) || plan.size() >= kMaxMilestones)
                return Return::fail("Invalid milestone title or too many milestones");
            plan.push_back({std::move(title), {}, {}});
            continue;
        }
        if (line.starts_with(baseline_prefix) && line.ends_with(" -->")) {
            auto id = line.substr(baseline_prefix.size(), line.size() - baseline_prefix.size() - 4);
            if (plan.empty() || !plan.back().baseline_snapshot.empty() || !valid_snapshot_id(id))
                return Return::fail("Invalid milestone review baseline");
            plan.back().baseline_snapshot = std::move(id);
            continue;
        }
        bool complete = false;
        std::string_view rest;
        if (line.starts_with(open_task_prefix))
            rest = std::string_view(line).substr(open_task_prefix.size());
        else if (line.starts_with(done_task_prefix)) {
            complete = true;
            rest = std::string_view(line).substr(done_task_prefix.size());
        } else
            return Return::fail("Invalid plan line: " + line);
        if (plan.empty())
            return Return::fail("A task must follow a milestone heading");
        auto separator = rest.find(": ");
        if (separator == std::string_view::npos)
            return Return::fail("Plan tasks require an ID and description");
        std::string id(rest.substr(0, separator));
        std::string description(rest.substr(separator + 2));
        if (!valid_task_id(id) || !valid_text(description) || !ids.insert(id).second ||
            ++task_count > kMaxTasks)
            return Return::fail("Invalid or duplicate task ID, description, or task limit");
        plan.back().tasks.push_back({std::move(id), std::move(description), complete});
    }
    return Return::ok(std::move(plan));
} catch (const std::exception &error) {
    return util::Result<ProjectPlan>::fail("Cannot parse plan: " + std::string(error.what()));
}

util::Result<std::string> PlanStore::render(const ProjectPlan &plan) try {
    using Return = util::Result<std::string>;
    if (plan.size() > kMaxMilestones)
        return Return::fail("Too many milestones");
    std::ostringstream output;
    output << kHeader << "\n\n";
    std::set<std::string> ids;
    std::size_t task_count = 0;
    for (const auto &milestone : plan) {
        if (!valid_text(milestone.title))
            return Return::fail("Invalid milestone title");
        output << "## Milestone: " << milestone.title << "\n";
        if (!milestone.baseline_snapshot.empty()) {
            if (!valid_snapshot_id(milestone.baseline_snapshot))
                return Return::fail("Invalid milestone review baseline");
            output << "<!-- baseline: " << milestone.baseline_snapshot << " -->\n";
        }
        for (const auto &task : milestone.tasks) {
            if (!valid_task_id(task.id) || !valid_text(task.description) ||
                !ids.insert(task.id).second || ++task_count > kMaxTasks)
                return Return::fail("Invalid or duplicate task ID, description, or task limit");
            output << "- [" << (task.complete ? 'x' : ' ') << "] " << task.id << ": "
                   << task.description << "\n";
        }
        output << "\n";
    }
    auto rendered = output.str();
    if (rendered.size() > kMaxPlanBytes)
        return Return::fail("Rendered plan exceeds the 1 MiB limit");
    return Return::ok(std::move(rendered));
} catch (const std::exception &error) {
    return util::Result<std::string>::fail("Cannot render plan: " + std::string(error.what()));
}

util::Result<ProjectPlan> PlanStore::load() const try {
    using Return = util::Result<ProjectPlan>;
    auto root = std::filesystem::canonical(root_);
    if (!std::filesystem::is_directory(root))
        return Return::fail("Project plan root is not a directory");
    auto path = root / "PLAN.md";
    std::error_code error;
    auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return Return::ok({});
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
        return Return::fail("PLAN.md must be a regular file inside the project");
    auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaxPlanBytes)
        return Return::fail("Cannot read PLAN.md or plan exceeds the 1 MiB limit");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return Return::fail("Cannot open PLAN.md");
    std::string contents(static_cast<std::size_t>(size), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(contents.size()))
        return Return::fail("PLAN.md changed while being read");
    return parse(contents);
} catch (const std::exception &error) {
    return util::Result<ProjectPlan>::fail("Cannot load PLAN.md: " + std::string(error.what()));
}

util::Result<util::Void> PlanStore::save(const ProjectPlan &plan) const try {
    using Return = util::Result<util::Void>;
    auto rendered = render(plan);
    if (!rendered.is_ok())
        return Return::fail(rendered.error());
    auto root = std::filesystem::canonical(root_);
    if (!std::filesystem::is_directory(root))
        return Return::fail("Project plan root is not a directory");
    auto destination = root / "PLAN.md";
    std::error_code error;
    auto destination_status = std::filesystem::symlink_status(destination, error);
    if (!error && (std::filesystem::is_symlink(destination_status) ||
                   (std::filesystem::exists(destination_status) &&
                    !std::filesystem::is_regular_file(destination_status))))
        return Return::fail("Refusing to replace a non-regular PLAN.md");
    if (error != std::errc::no_such_file_or_directory && error)
        return Return::fail("Cannot inspect PLAN.md: " + error.message());

    auto temporary = (root / ".PLAN.md.tmp.XXXXXX").string();
    int descriptor = ::mkstemp(temporary.data());
    if (descriptor < 0)
        return Return::fail("Cannot create temporary PLAN.md: " +
                            std::string(std::strerror(errno)));
    (void)::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
    if (::fchmod(descriptor, 0644) != 0) {
        auto permission_error = std::string(std::strerror(errno));
        (void)::close(descriptor);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Return::fail("Cannot set PLAN.md permissions: " + permission_error);
    }
    std::size_t offset = 0;
    while (offset < rendered.value().size()) {
        auto written =
            ::write(descriptor, rendered.value().data() + offset, rendered.value().size() - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            auto write_error = std::string(std::strerror(errno));
            (void)::close(descriptor);
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return Return::fail("Cannot write temporary PLAN.md: " + write_error);
        }
        offset += static_cast<std::size_t>(written);
    }
    if (::fsync(descriptor) != 0) {
        auto sync_error = std::string(std::strerror(errno));
        (void)::close(descriptor);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Return::fail("Cannot sync temporary PLAN.md: " + sync_error);
    }
    if (::close(descriptor) != 0) {
        auto close_error = std::string(std::strerror(errno));
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Return::fail("Cannot close temporary PLAN.md: " + close_error);
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        auto rename_error = error.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Return::fail("Cannot atomically replace PLAN.md: " + rename_error);
    }
    return Return::ok();
} catch (const std::exception &error) {
    return util::Result<util::Void>::fail("Cannot save PLAN.md: " + std::string(error.what()));
}

util::Result<util::Void> PlanStore::add_milestone(std::string title) const try {
    using Return = util::Result<util::Void>;
    if (!valid_text(title))
        return Return::fail("Milestone title must contain 1 to 500 single-line characters");
    auto plan = load();
    if (!plan.is_ok())
        return Return::fail(plan.error());
    auto updated = plan.value();
    updated.push_back({std::move(title), {}, {}});
    return save(updated);
} catch (const std::exception &error) {
    return util::Result<util::Void>::fail("Cannot add milestone: " + std::string(error.what()));
}

util::Result<util::Void> PlanStore::set_milestone_baseline(std::size_t milestone_index,
                                                           const std::string &snapshot_id) const {
    using Return = util::Result<util::Void>;
    if (!valid_snapshot_id(snapshot_id))
        return Return::fail("Invalid milestone review baseline ID");
    auto plan = load();
    if (!plan.is_ok())
        return Return::fail(plan.error());
    auto updated = plan.value();
    if (milestone_index >= updated.size())
        return Return::fail("Milestone index is outside the plan");
    updated[milestone_index].baseline_snapshot = snapshot_id;
    return save(updated);
}

util::Result<std::string> PlanStore::add_task(std::size_t milestone_index,
                                              std::string description) const try {
    using Return = util::Result<std::string>;
    if (!valid_text(description))
        return Return::fail("Task description must contain 1 to 500 single-line characters");
    auto plan = load();
    if (!plan.is_ok())
        return Return::fail(plan.error());
    auto updated = plan.value();
    if (milestone_index >= updated.size())
        return Return::fail("Milestone index is outside the plan");
    auto next = next_task_number(updated);
    if (!next.is_ok())
        return Return::fail(next.error());
    auto id = task_id(next.value());
    updated[milestone_index].tasks.push_back({id, std::move(description), false});
    auto saved = save(updated);
    if (!saved.is_ok())
        return Return::fail(saved.error());
    return Return::ok(std::move(id));
} catch (const std::exception &error) {
    return util::Result<std::string>::fail("Cannot add task: " + std::string(error.what()));
}

util::Result<std::vector<std::string>>
PlanStore::add_tasks(std::size_t milestone_index,
                     const std::vector<std::string> &descriptions) const try {
    using Return = util::Result<std::vector<std::string>>;
    if (descriptions.empty() || descriptions.size() > 12)
        return Return::fail("Task batch must contain between 1 and 12 tasks");
    for (const auto &description : descriptions)
        if (!valid_text(description))
            return Return::fail("Task description must contain 1 to 500 single-line characters");
    auto loaded = load();
    if (!loaded.is_ok())
        return Return::fail(loaded.error());
    auto updated = loaded.value();
    if (milestone_index >= updated.size())
        return Return::fail("Milestone index is outside the plan");
    auto next = next_task_number(updated);
    if (!next.is_ok())
        return Return::fail(next.error());
    auto next_number = next.value();
    std::vector<std::string> ids;
    ids.reserve(descriptions.size());
    for (const auto &description : descriptions) {
        auto id = task_id(next_number++);
        updated[milestone_index].tasks.push_back({id, description, false});
        ids.push_back(std::move(id));
    }
    auto saved = save(updated);
    if (!saved.is_ok())
        return Return::fail(saved.error());
    return Return::ok(std::move(ids));
} catch (const std::exception &error) {
    return util::Result<std::vector<std::string>>::fail("Cannot add task batch: " +
                                                        std::string(error.what()));
}

util::Result<util::Void> PlanStore::set_task_complete(const std::string &task_id,
                                                      bool complete) const try {
    using Return = util::Result<util::Void>;
    auto plan = load();
    if (!plan.is_ok())
        return Return::fail(plan.error());
    auto updated = plan.value();
    for (auto &milestone : updated)
        for (auto &task : milestone.tasks)
            if (task.id == task_id) {
                task.complete = complete;
                return save(updated);
            }
    return Return::fail("Unknown plan task: " + task_id);
} catch (const std::exception &error) {
    return util::Result<util::Void>::fail("Cannot update task: " + std::string(error.what()));
}

util::Result<util::Void>
PlanStore::import_file(const std::filesystem::path &relative_path,
                       const std::function<bool(const std::string &)> &confirm) const try {
    using Return = util::Result<util::Void>;
    if (relative_path.empty() || relative_path.is_absolute())
        return Return::fail("Import path must be relative to the project");
    auto root = std::filesystem::canonical(root_);
    auto source = root;
    for (const auto &part : relative_path) {
        if (part == "." || part == "..")
            return Return::fail("Import path cannot contain dot segments");
        source /= part;
        std::error_code status_error;
        auto status = std::filesystem::symlink_status(source, status_error);
        if (status_error || std::filesystem::is_symlink(status))
            return Return::fail("Import path contains a missing or symbolic link component");
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error)
        return Return::fail("Import source must be a regular project file");
    auto size = std::filesystem::file_size(source, error);
    if (error || size > kMaxPlanBytes)
        return Return::fail("Import source is unreadable or exceeds the 1 MiB limit");
    std::ifstream input(source, std::ios::binary);
    if (!input)
        return Return::fail("Cannot open import source");
    std::string markdown(static_cast<std::size_t>(size), '\0');
    input.read(markdown.data(), static_cast<std::streamsize>(markdown.size()));
    if (input.gcount() != static_cast<std::streamsize>(markdown.size()))
        return Return::fail("Import source changed while being read");
    auto parsed = parse(markdown);
    if (!parsed.is_ok())
        return Return::fail("Imported plan rejected: " + parsed.error());
    if (!confirm)
        return Return::fail("Import requires explicit user approval");
    auto preview_size = std::min<std::size_t>(markdown.size(), 4096);
    auto preview = "Import plan from " + relative_path.generic_string() + "?\n" +
                   markdown.substr(0, preview_size) +
                   (preview_size < markdown.size() ? "\n[preview truncated]" : "");
    if (!confirm(preview))
        return Return::fail("Plan import declined");
    return merge_import(markdown);
} catch (const std::exception &error) {
    return util::Result<util::Void>::fail("Cannot import plan file: " + std::string(error.what()));
}

util::Result<std::vector<std::string>> PlanStore::import_review_comments(
    const std::filesystem::path &relative_path, std::size_t milestone_index,
    const std::function<bool(const std::string &)> &confirm) const try {
    using Return = util::Result<std::vector<std::string>>;
    constexpr std::size_t max_comment_file_bytes = std::size_t{16} * 1024;
    if (relative_path.empty() || relative_path.is_absolute())
        return Return::fail("Review-comment path must be project-relative");
    auto root = std::filesystem::canonical(root_);
    auto source = root;
    for (const auto &part : relative_path) {
        if (part == "." || part == "..")
            return Return::fail("Review-comment path cannot contain dot segments");
        source /= part;
        std::error_code status_error;
        auto status = std::filesystem::symlink_status(source, status_error);
        if (status_error || std::filesystem::is_symlink(status))
            return Return::fail(
                "Review-comment path contains a missing or symbolic link component");
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error)
        return Return::fail("Review comments must come from a regular project file");
    auto size = std::filesystem::file_size(source, error);
    if (error || size > max_comment_file_bytes)
        return Return::fail("Review-comment file is unreadable or exceeds 16 KiB");
    std::ifstream input(source, std::ios::binary);
    if (!input)
        return Return::fail("Cannot open review-comment file");
    std::string contents(static_cast<std::size_t>(size), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(contents.size()))
        return Return::fail("Review-comment file changed while being read");

    std::vector<std::string> comments;
    std::istringstream lines(contents);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#')
            continue;
        auto text = line.substr(first);
        if (text.starts_with("- ") || text.starts_with("* "))
            text.erase(0, 2);
        else {
            auto dot = text.find(". ");
            bool numbered =
                dot != std::string::npos && dot > 0 &&
                std::all_of(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(dot),
                            [](char c) { return c >= '0' && c <= '9'; });
            if (numbered)
                text.erase(0, dot + 2);
            else
                return Return::fail("Review comments must be one Markdown bullet per line");
        }
        auto last = text.find_last_not_of(" \t");
        if (last == std::string::npos)
            return Return::fail("Review comments cannot be empty");
        text.resize(last + 1);
        if (!valid_text(text))
            return Return::fail("Review comment must contain at most 500 single-line characters");
        comments.push_back(std::move(text));
        if (comments.size() > 12)
            return Return::fail("At most 12 review comments can be imported at once");
    }
    if (comments.empty())
        return Return::fail("Review-comment file contains no Markdown bullet comments");
    if (!confirm)
        return Return::fail("Review-comment import requires explicit user approval");
    std::string preview = "Import these review comments as plan tasks?\n";
    for (const auto &comment : comments)
        preview += "- [ ] " + comment + "\n";
    if (!confirm(preview))
        return Return::fail("Review-comment import declined");
    return add_tasks(milestone_index, comments);
} catch (const std::exception &error) {
    return util::Result<std::vector<std::string>>::fail("Cannot import review comments: " +
                                                        std::string(error.what()));
}

util::Result<util::Void> PlanStore::merge_import(std::string_view markdown) const try {
    using Return = util::Result<util::Void>;
    auto imported = parse(markdown);
    if (!imported.is_ok())
        return Return::fail("Imported plan rejected: " + imported.error());
    auto current = load();
    if (!current.is_ok())
        return Return::fail(current.error());
    ProjectPlan merged = current.value();
    auto next = next_task_number(merged);
    if (!next.is_ok())
        return Return::fail(next.error());
    auto next_id = next.value();
    for (const auto &source_milestone : imported.value()) {
        PlanMilestone milestone{source_milestone.title, {}, {}};
        for (const auto &source_task : source_milestone.tasks) {
            milestone.tasks.push_back({task_id(next_id++), source_task.description, false});
        }
        merged.push_back(std::move(milestone));
    }
    return save(merged);
} catch (const std::exception &error) {
    return util::Result<util::Void>::fail("Cannot import plan: " + std::string(error.what()));
}

} // namespace platypus::core
