// Project checks use direct argv execution and explicit approval, never an implicit shell.
#include "project_checks.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <sys/stat.h>
#include <toml++/toml.hpp>
#include <unistd.h>

namespace platypus::core {
namespace {
constexpr std::size_t kMaxCheckCount = 16;
constexpr std::size_t kMaxArgCount = 32;
constexpr std::size_t kMaxStringBytes = 4096;
constexpr std::size_t kMaxConfigBytes = std::size_t{256} * 1024;
constexpr std::size_t kMaxStoredResults = kMaxCheckCount * 3;
constexpr std::size_t kMaxResultsBytes = std::size_t{512} * 1024;

bool safe_text(const std::string &value, std::size_t max = kMaxStringBytes) {
    if (value.empty() || value.size() > max)
        return false;
    for (unsigned char character : value)
        if (character < 0x20 || character == 0x7f)
            return false;
    return true;
}

util::Result<std::vector<ProjectCheck>> configured_checks(const std::filesystem::path &path,
                                                          bool &override_configured) {
    using Return = util::Result<std::vector<ProjectCheck>>;
    try {
        auto config = toml::parse_file(path.string());
        auto checks = config.get_as<toml::table>("checks");
        override_configured = checks != nullptr;
        if (!checks)
            return Return::ok({});
        auto commands = checks->get_as<toml::array>("commands");
        if (!commands)
            return Return::fail(".platypus.toml [checks].commands must be an array");
        if (commands->size() > kMaxCheckCount)
            return Return::fail("Too many configured project checks");
        std::vector<ProjectCheck> result;
        for (const auto &node : *commands) {
            auto table = node.as_table();
            if (!table)
                return Return::fail("Each configured check must be a table");
            auto name_node = table->get("name");
            auto name = name_node ? name_node->value<std::string>() : std::nullopt;
            auto argv = table->get_as<toml::array>("argv");
            if (!name || !argv || !safe_text(*name) || argv->empty() || argv->size() > kMaxArgCount)
                return Return::fail("Each check requires a name and a bounded argv array");
            ProjectCheck check{*name, {}};
            for (const auto &argument : *argv) {
                auto value = argument.value<std::string>();
                if (!value || !safe_text(*value))
                    return Return::fail("Check argv items must be bounded, non-empty strings");
                check.argv.push_back(*value);
            }
            result.push_back(std::move(check));
        }
        return Return::ok(std::move(result));
    } catch (const std::exception &error) {
        return Return::fail("Cannot parse .platypus.toml checks: " + std::string(error.what()));
    }
}

bool regular_file(const std::filesystem::path &path) {
    std::error_code error;
    auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_regular_file(status) &&
           !std::filesystem::is_symlink(status);
}

bool configured_preset(const nlohmann::json &presets, const char *section,
                       std::string_view preset_name) {
    if (!presets.contains(section) || !presets[section].is_array())
        return false;
    for (const auto &preset : presets[section])
        if (preset.is_object() && preset.value("name", "") == preset_name)
            return true;
    return false;
}

util::Result<std::string> bounded_file(const std::filesystem::path &path) {
    using Return = util::Result<std::string>;
    std::error_code error;
    auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaxConfigBytes)
        return Return::fail("Build manifest is unreadable or too large");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return Return::fail("Cannot open build manifest");
    std::string contents(static_cast<std::size_t>(size), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(contents.size()))
        return Return::fail("Build manifest changed while reading");
    return Return::ok(std::move(contents));
}

} // namespace

util::Result<std::vector<ProjectCheck>>
ProjectChecks::discover(const std::filesystem::path &project_root) try {
    using Return = util::Result<std::vector<ProjectCheck>>;
    auto root = std::filesystem::canonical(project_root);
    if (!std::filesystem::is_directory(root))
        return Return::fail("Project check root is not a directory");
    auto config_path = root / ".platypus.toml";
    std::error_code error;
    auto config_status = std::filesystem::symlink_status(config_path, error);
    if (!error && (std::filesystem::is_symlink(config_status) ||
                   !std::filesystem::is_regular_file(config_status)))
        return Return::fail(".platypus.toml must be a regular project file");
    if (error != std::errc::no_such_file_or_directory && error)
        return Return::fail("Cannot inspect .platypus.toml: " + error.message());
    if (!error) {
        bool override_configured = false;
        auto configured = configured_checks(config_path, override_configured);
        if (!configured.is_ok() || override_configured)
            return configured;
    }

    std::vector<ProjectCheck> result;
    if (regular_file(root / "CMakePresets.json")) {
        auto contents = bounded_file(root / "CMakePresets.json");
        if (!contents.is_ok())
            return Return::fail(contents.error());
        auto presets = nlohmann::json::parse(contents.value());
        bool has_configure_dev = configured_preset(presets, "configurePresets", "dev");
        bool has_build_dev = configured_preset(presets, "buildPresets", "dev");
        bool has_test_dev = configured_preset(presets, "testPresets", "dev");
        bool configured = regular_file(root / "build/dev/CMakeCache.txt");
        if (has_build_dev && has_configure_dev && !configured)
            result.push_back({"CMake configure (dev)", {"cmake", "--preset", "dev"}});
        if (has_build_dev) {
            result.push_back({"CMake build (dev)", {"cmake", "--build", "--preset", "dev"}});
            if (has_test_dev)
                result.push_back(
                    {"CMake tests (dev)", {"ctest", "--preset", "dev", "--output-on-failure"}});
        }
        if (!has_build_dev && regular_file(root / "CMakeLists.txt")) {
            bool generic_configured = regular_file(root / "build/CMakeCache.txt");
            if (!generic_configured)
                result.push_back({"CMake configure", {"cmake", "-S", ".", "-B", "build"}});
            result.push_back({"CMake build", {"cmake", "--build", "build"}});
            result.push_back(
                {"CMake tests", {"ctest", "--test-dir", "build", "--output-on-failure"}});
        }
    } else if (regular_file(root / "CMakeLists.txt")) {
        bool configured = regular_file(root / "build/CMakeCache.txt");
        if (!configured)
            result.push_back({"CMake configure", {"cmake", "-S", ".", "-B", "build"}});
        result.push_back({"CMake build", {"cmake", "--build", "build"}});
        result.push_back({"CMake tests", {"ctest", "--test-dir", "build", "--output-on-failure"}});
    }

    if (regular_file(root / "Cargo.toml"))
        result.push_back({"Cargo tests", {"cargo", "test"}});

    if (regular_file(root / "package.json")) {
        auto contents = bounded_file(root / "package.json");
        if (!contents.is_ok())
            return Return::fail(contents.error());
        auto package = nlohmann::json::parse(contents.value());
        if (package.contains("scripts") && package["scripts"].is_object() &&
            package["scripts"].contains("test") && package["scripts"]["test"].is_string())
            result.push_back({"npm test", {"npm", "test"}});
    }

    if (regular_file(root / "go.mod"))
        result.push_back({"Go tests", {"go", "test", "./..."}});

    bool pytest = regular_file(root / "pytest.ini") || regular_file(root / "tox.ini");
    if (!pytest && regular_file(root / "pyproject.toml")) {
        auto contents = bounded_file(root / "pyproject.toml");
        if (!contents.is_ok())
            return Return::fail(contents.error());
        pytest = contents.value().find("[tool.pytest") != std::string::npos;
    }
    if (pytest)
        result.push_back({"Pytest", {"python3", "-m", "pytest"}});

    if (result.size() > kMaxCheckCount)
        return Return::fail("Detected too many project checks; configure a smaller set explicitly");
    return Return::ok(std::move(result));
} catch (const std::exception &error) {
    return util::Result<std::vector<ProjectCheck>>::fail("Cannot detect project checks: " +
                                                         std::string(error.what()));
}

util::Result<ProjectCheckResult> ProjectChecks::run(const ProjectCheck &check,
                                                    const std::filesystem::path &project_root,
                                                    const Confirmer &confirm,
                                                    std::stop_token stop) try {
    using Return = util::Result<ProjectCheckResult>;
    if (!safe_text(check.name) || check.argv.empty() || check.argv.size() > kMaxArgCount)
        return Return::fail("Invalid project check");
    for (const auto &argument : check.argv)
        if (!safe_text(argument))
            return Return::fail("Invalid project check argv");
    if (stop.stop_requested())
        return Return::fail("Cancelled");
    std::ostringstream preview;
    preview << "Run project check: " << check.name << "\nCommand:";
    for (const auto &argument : check.argv)
        preview << ' ' << std::quoted(argument);
    if (!confirm || !confirm(preview.str()))
        return Return::ok({false, {"Check declined", 0, false}});
    auto root = std::filesystem::canonical(project_root);
    auto process = platform::run_process(check.argv, root, std::chrono::minutes(5),
                                         std::size_t{16} * 1024, stop);
    if (!process.is_ok())
        return Return::fail(process.error());
    return Return::ok({true, process.value()});
} catch (const std::exception &error) {
    return util::Result<ProjectCheckResult>::fail("Cannot run project check: " +
                                                  std::string(error.what()));
}

util::Result<util::Void>
ProjectChecks::store_results(const std::filesystem::path &project_root,
                             const std::vector<ProjectCheckRecord> &records) try {
    using Return = util::Result<util::Void>;
    if (records.size() > kMaxStoredResults)
        return Return::fail("Too many project check results");
    nlohmann::json serialized = nlohmann::json::array();
    for (const auto &record : records) {
        if (!safe_text(record.name) || (record.status != "passed" && record.status != "failed" &&
                                        record.status != "declined" && record.status != "error"))
            return Return::fail("Invalid project check result");
        serialized.push_back({{"name", record.name},
                              {"status", record.status},
                              {"exit_code", record.has_exit_code ? nlohmann::json(record.exit_code)
                                                                 : nlohmann::json(nullptr)}});
    }
    auto contents = nlohmann::json{{"version", 1}, {"checks", std::move(serialized)}}.dump(2);
    if (contents.size() > kMaxResultsBytes)
        return Return::fail("Project check results exceed the storage limit");
    auto root = std::filesystem::canonical(project_root);
    auto state = root / ".platypus";
    std::error_code error;
    auto state_status = std::filesystem::symlink_status(state, error);
    if (error == std::errc::no_such_file_or_directory) {
        error.clear();
        std::filesystem::create_directory(state, error);
        if (error)
            return Return::fail("Cannot create .platypus check storage: " + error.message());
        std::filesystem::permissions(state, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, error);
        if (error)
            return Return::fail("Cannot secure .platypus check storage: " + error.message());
        state_status = std::filesystem::symlink_status(state, error);
    }
    if (error || std::filesystem::is_symlink(state_status) ||
        !std::filesystem::is_directory(state_status))
        return Return::fail("Unsafe .platypus check storage");
    auto destination = state / "check-results.json";
    auto destination_status = std::filesystem::symlink_status(destination, error);
    if (!error && (std::filesystem::is_symlink(destination_status) ||
                   !std::filesystem::is_regular_file(destination_status)))
        return Return::fail("Unsafe project check result file");
    if (error != std::errc::no_such_file_or_directory && error)
        return Return::fail("Cannot inspect project check results: " + error.message());

    auto temporary = (state / ".check-results.tmp.XXXXXX").string();
    int descriptor = ::mkstemp(temporary.data());
    if (descriptor < 0)
        return Return::fail("Cannot create project check result file: " +
                            std::string(std::strerror(errno)));
    (void)::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
    auto cleanup = [&] {
        (void)::close(descriptor);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    };
    if (::fchmod(descriptor, 0600) != 0) {
        auto message = std::string(std::strerror(errno));
        cleanup();
        return Return::fail("Cannot secure check result file: " + message);
    }
    std::size_t offset = 0;
    while (offset < contents.size()) {
        auto written = ::write(descriptor, contents.data() + offset, contents.size() - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            auto message = std::string(std::strerror(errno));
            cleanup();
            return Return::fail("Cannot write check result file: " + message);
        }
        offset += static_cast<std::size_t>(written);
    }
    if (::fsync(descriptor) != 0) {
        auto message = std::string(std::strerror(errno));
        cleanup();
        return Return::fail("Cannot sync check result file: " + message);
    }
    if (::close(descriptor) != 0) {
        descriptor = -1;
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Return::fail("Cannot close check result file");
    }
    descriptor = -1;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        auto message = error.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Return::fail("Cannot replace check result file: " + message);
    }
    return Return::ok();
} catch (const std::exception &error) {
    return util::Result<util::Void>::fail("Cannot store project check results: " +
                                          std::string(error.what()));
}

util::Result<std::vector<ProjectCheckRecord>>
ProjectChecks::load_results(const std::filesystem::path &project_root) try {
    using Return = util::Result<std::vector<ProjectCheckRecord>>;
    auto root = std::filesystem::canonical(project_root);
    auto state = root / ".platypus";
    std::error_code error;
    auto state_status = std::filesystem::symlink_status(state, error);
    if (error == std::errc::no_such_file_or_directory)
        return Return::ok({});
    if (error || std::filesystem::is_symlink(state_status) ||
        !std::filesystem::is_directory(state_status))
        return Return::fail("Unsafe .platypus check storage");
    auto path = state / "check-results.json";
    auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return Return::ok({});
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
        return Return::fail("Unsafe project check result file");
    auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaxResultsBytes)
        return Return::fail("Project check result file exceeds the storage limit");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return Return::fail("Cannot read project check results");
    std::string contents(static_cast<std::size_t>(size), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(contents.size()))
        return Return::fail("Project check results changed while being read");
    auto document = nlohmann::json::parse(contents);
    if (!document.is_object() || document.value("version", 0) != 1 ||
        !document.contains("checks") || !document["checks"].is_array() ||
        document["checks"].size() > kMaxStoredResults)
        return Return::fail("Invalid project check result metadata");
    std::vector<ProjectCheckRecord> records;
    for (const auto &item : document["checks"]) {
        if (!item.is_object() || !item.contains("name") || !item["name"].is_string() ||
            !item.contains("status") || !item["status"].is_string() || !item.contains("exit_code"))
            return Return::fail("Invalid project check result record");
        auto name = item["name"].get<std::string>();
        auto state_text = item["status"].get<std::string>();
        if (!safe_text(name) || (state_text != "passed" && state_text != "failed" &&
                                 state_text != "declined" && state_text != "error"))
            return Return::fail("Invalid project check result fields");
        ProjectCheckRecord record{std::move(name), std::move(state_text), 0, false};
        if (!item["exit_code"].is_null()) {
            if (!item["exit_code"].is_number_integer())
                return Return::fail("Invalid project check exit status");
            record.exit_code = item["exit_code"].get<int>();
            record.has_exit_code = true;
        }
        records.push_back(std::move(record));
    }
    return Return::ok(std::move(records));
} catch (const std::exception &error) {
    return util::Result<std::vector<ProjectCheckRecord>>::fail(
        "Cannot load project check results: " + std::string(error.what()));
}

} // namespace platypus::core
