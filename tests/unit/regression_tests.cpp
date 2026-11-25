// Regression coverage for audit findings: safety, retries, parsing and subprocess lifetimes.
#include "agent/repository_map.hpp"
#include "core/codec.hpp"
#include "core/execution_engine.hpp"
#include "core/plan_commands.hpp"
#include "core/plan_store.hpp"
#include "core/project_checks.hpp"
#include "llm/chat_formatter.hpp"
#include "llm/fake_backend.hpp"
#include "llm/tool_grammar.hpp"
#include "platform/process.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <unistd.h>
namespace fs = std::filesystem;
using namespace platypus;
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size);
extern "C" int LLVMFuzzerEditFormatTestOneInput(const std::uint8_t *data, std::size_t size);
namespace {
struct Workspace {
    fs::path root;
    Workspace() {
        std::string pattern = (fs::temp_directory_path() / "platypus-test-XXXXXX").string();
        auto made = mkdtemp(pattern.data());
        if (!made)
            throw std::runtime_error("mkdtemp failed");
        root = made;
    }
    ~Workspace() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};
std::string read(const fs::path &p) {
    std::ifstream in(p);
    return {std::istreambuf_iterator<char>(in), {}};
}
} // namespace
TEST_CASE("JSON formatter escapes quotes and newlines") {
    auto json = nlohmann::json::parse(llm::ChatFormatter::to_json({{"user", "\"quoted\"\nline"}}));
    REQUIRE(json[0]["content"] == "\"quoted\"\nline");
}
TEST_CASE("Tool parser repairs trailing commas and incomplete braces") {
    auto calls = core::Codec::parse_tool_calls(
        R"(Here: {"tool_call":{"name":"read","arguments":{"path":"x",},}} suffix)");
    REQUIRE(calls.size() == 1);
    REQUIRE(calls[0].tool_name == "read");
    REQUIRE(
        core::Codec::parse_tool_calls(R"({"tool_call":{"name":"read","arguments":{"path":"x"}})")
            .size() == 1);
    REQUIRE(
        core::Codec::parse_tool_calls(R"({"tool_call":{"name":"read","arguments":[]}})").empty());
}
TEST_CASE("Unapproved tools cannot mutate files or run commands") {
    Workspace workspace;
    core::ToolRegistry tools(workspace.root);
    core::register_default_tools(tools);
    REQUIRE_FALSE(tools.execute("write", {{"path", "x"}, {"content", "bad"}}).is_ok());
    REQUIRE_FALSE(fs::exists(workspace.root / "x"));
    REQUIRE_FALSE(tools.execute("run", {{"command", "touch executed"}}).is_ok());
    REQUIRE_FALSE(fs::exists(workspace.root / "executed"));
}
TEST_CASE("Project plans round trip with stable task IDs and completion states") {
    Workspace workspace;
    core::PlanStore plans(workspace.root);
    core::ProjectPlan plan{
        {"Foundation",
         {{"TASK-0001", "Add the parser", true}, {"TASK-0002", "Add parser tests", false}},
         std::string(40, 'a')}};
    REQUIRE(plans.save(plan).is_ok());
    auto loaded = plans.load();
    REQUIRE(loaded.is_ok());
    REQUIRE(loaded.value().size() == 1);
    REQUIRE(loaded.value()[0].title == "Foundation");
    REQUIRE(loaded.value()[0].baseline_snapshot == std::string(40, 'a'));
    REQUIRE(loaded.value()[0].tasks[0].complete);
    REQUIRE(loaded.value()[0].tasks[1].id == "TASK-0002");
    auto rendered = core::PlanStore::render(loaded.value());
    REQUIRE(rendered.is_ok());
    auto reparsed = core::PlanStore::parse(rendered.value());
    REQUIRE(reparsed.is_ok());
    REQUIRE(reparsed.value()[0].tasks[1].description == "Add parser tests");
}
TEST_CASE("Plan parser rejects malformed and duplicate tasks") {
    REQUIRE_FALSE(core::PlanStore::parse("# Other Plan\n").is_ok());
    REQUIRE_FALSE(
        core::PlanStore::parse("# Platypus Plan\n\n## Milestone: First\n<!-- baseline: bad -->\n")
            .is_ok());
    REQUIRE_FALSE(
        core::PlanStore::parse("# Platypus Plan\n\n## Milestone: First\n- [ ] TASK-0001: One\n"
                               "- [x] TASK-0001: Duplicate\n")
            .is_ok());
    REQUIRE_FALSE(core::PlanStore::parse("# Platypus Plan\n\n## Milestone: First\n- [ ] TASK-0001: "
                                         "bad\nsecond line\n")
                      .is_ok());
}
TEST_CASE("Imported plans merge as new unchecked tasks with fresh IDs") {
    Workspace workspace;
    core::PlanStore plans(workspace.root);
    REQUIRE(plans.save({{"Existing", {{"TASK-0001", "Keep this task", true}}, {}}}).is_ok());
    const std::string imported =
        "# Platypus Plan\n\n## Milestone: External work\n- [x] TASK-0001: Imported one\n"
        "- [ ] TASK-0002: Imported two\n";
    REQUIRE(plans.merge_import(imported).is_ok());
    auto loaded = plans.load();
    REQUIRE(loaded.is_ok());
    REQUIRE(loaded.value().size() == 2);
    REQUIRE(loaded.value()[1].tasks[0].id == "TASK-0002");
    REQUIRE_FALSE(loaded.value()[1].tasks[0].complete);
    REQUIRE(loaded.value()[1].tasks[1].id == "TASK-0003");
}
TEST_CASE("Plan store creates milestones and tasks through validated updates") {
    Workspace workspace;
    core::PlanStore plans(workspace.root);
    REQUIRE(plans.add_milestone("Core work").is_ok());
    auto task = plans.add_task(0, "Implement and verify plan storage");
    REQUIRE(task.is_ok());
    REQUIRE(task.value() == "TASK-0001");
    REQUIRE(plans.set_task_complete(task.value(), true).is_ok());
    auto loaded = plans.load();
    REQUIRE(loaded.is_ok());
    REQUIRE(loaded.value()[0].tasks[0].complete);
    REQUIRE_FALSE(plans.add_task(1, "Invalid milestone").is_ok());
    REQUIRE_FALSE(plans.add_task(0, "two lines\nare rejected").is_ok());
}
TEST_CASE("Plan file import previews and requires explicit approval") {
    Workspace workspace, outside;
    const std::string imported =
        "# Platypus Plan\n\n## Milestone: Imported\n- [ ] TASK-0001: Review change\n";
    std::ofstream(workspace.root / "incoming.md") << imported;
    std::ofstream(outside.root / "incoming.md") << imported;
    fs::create_symlink(outside.root / "incoming.md", workspace.root / "external.md");
    core::PlanStore plans(workspace.root);
    bool saw_preview = false;
    auto denied = plans.import_file("incoming.md", [&](const std::string &preview) {
        saw_preview = preview.find("Review change") != std::string::npos;
        return false;
    });
    REQUIRE_FALSE(denied.is_ok());
    REQUIRE(saw_preview);
    REQUIRE(plans.load().value().empty());
    REQUIRE(plans.import_file("incoming.md", [](const auto &) { return true; }).is_ok());
    REQUIRE(plans.load().value()[0].tasks[0].id == "TASK-0001");
    REQUIRE_FALSE(plans.import_file("external.md", [](const auto &) { return true; }).is_ok());
    REQUIRE_FALSE(plans.import_file("../incoming.md", [](const auto &) { return true; }).is_ok());
}
TEST_CASE("Interactive plan commands validate and confirm plan mutations") {
    Workspace workspace;
    std::size_t approvals = 0;
    safety::SnapshotStore store(workspace.root, [&](const auto &) {
        ++approvals;
        return true;
    });
    auto no_plan = core::handle_plan_command("/plan", store);
    REQUIRE(no_plan.has_value());
    REQUIRE(no_plan->find("No project plan") != std::string::npos);
    REQUIRE(core::handle_plan_command("/milestone Implement", store)->find("Added milestone") !=
            std::string::npos);
    REQUIRE(core::handle_plan_command("/task 1 Add tests", store)->find("TASK-0001") !=
            std::string::npos);
    std::ofstream(workspace.root / "implementation.cpp") << "int implementation = 1;\n";
    auto review = core::handle_plan_command("/review", store);
    REQUIRE(review.has_value());
    REQUIRE(review->find("Milestone Review: Implement") != std::string::npos);
    REQUIRE(review->find("implementation.cpp") != std::string::npos);
    REQUIRE(review->find("No project check results are stored") != std::string::npos);
    REQUIRE(core::handle_plan_command("/done TASK-0001", store)->find("Completed") !=
            std::string::npos);
    REQUIRE(approvals == 3);
    auto rendered = core::handle_plan_command("/plan", store);
    REQUIRE(rendered->find("[x] TASK-0001") != std::string::npos);
    REQUIRE_FALSE(core::handle_plan_command("/task 0 invalid", store)->empty());
    REQUIRE_FALSE(core::handle_plan_command("do ordinary task", store).has_value());
}
TEST_CASE("Plan storage rejects symlink destinations") {
    Workspace workspace, outside;
    std::ofstream(outside.root / "PLAN.md") << "outside data";
    fs::create_symlink(outside.root / "PLAN.md", workspace.root / "PLAN.md");
    core::PlanStore plans(workspace.root);
    REQUIRE_FALSE(plans.load().is_ok());
    REQUIRE_FALSE(plans.save({{"Unsafe", {{"TASK-0001", "Do not overwrite", false}}, {}}}).is_ok());
    REQUIRE(read(outside.root / "PLAN.md") == "outside data");
}
TEST_CASE("Project checks detect supported manifests and run approved argv directly") {
    Workspace workspace;
    fs::create_directories(workspace.root / "build/dev");
    std::ofstream(workspace.root / "CMakeLists.txt") << "project(sample)\n";
    std::ofstream(workspace.root / "CMakePresets.json")
        << R"({"version": 6, "configurePresets": [{"name": "dev"}], "buildPresets": [{"name": "dev"}], "testPresets": [{"name": "dev"}]})";
    std::ofstream(workspace.root / "build/dev/CMakeCache.txt") << "configured";
    std::ofstream(workspace.root / "Cargo.toml") << "[package]\nname='sample'\nversion='0.1.0'\n";
    std::ofstream(workspace.root / "package.json") << R"({"scripts":{"test":"echo ok"}})";
    std::ofstream(workspace.root / "go.mod") << "module sample\n";
    std::ofstream(workspace.root / "pytest.ini") << "[pytest]\n";
    auto detected = core::ProjectChecks::discover(workspace.root);
    REQUIRE(detected.is_ok());
    REQUIRE(detected.value().size() == 6);
    REQUIRE(detected.value()[0].name == "CMake build (dev)");

    std::ofstream(workspace.root / ".platypus.toml")
        << "[checks]\ncommands = [{name='unit', argv=['python3', '-c', "
           "\"print('check-ok')\"]}]\n";
    auto overridden = core::ProjectChecks::discover(workspace.root);
    REQUIRE(overridden.is_ok());
    REQUIRE(overridden.value().size() == 1);
    REQUIRE(overridden.value()[0].name == "unit");
    std::string approval;
    auto result = core::ProjectChecks::run(overridden.value()[0], workspace.root,
                                           [&](const std::string &preview) {
                                               approval = preview;
                                               return true;
                                           });
    REQUIRE(result.is_ok());
    REQUIRE(result.value().approved);
    REQUIRE(result.value().process.exit_code == 0);
    REQUIRE(result.value().process.output.find("check-ok") != std::string::npos);
    REQUIRE(approval.find("python3") != std::string::npos);

    auto declined = core::ProjectChecks::run(overridden.value()[0], workspace.root,
                                             [](const auto &) { return false; });
    REQUIRE(declined.is_ok());
    REQUIRE_FALSE(declined.value().approved);

    std::ofstream(workspace.root / ".platypus.toml") << "[checks]\ncommands=[]\n";
    auto disabled = core::ProjectChecks::discover(workspace.root);
    REQUIRE(disabled.is_ok());
    REQUIRE(disabled.value().empty());
}
TEST_CASE("Project check overrides reject malformed and symlinked configuration") {
    Workspace workspace, outside;
    std::ofstream(workspace.root / ".platypus.toml")
        << "[checks]\ncommands = [{name='bad', argv='not-an-array'}]\n";
    REQUIRE_FALSE(core::ProjectChecks::discover(workspace.root).is_ok());
    fs::remove(workspace.root / ".platypus.toml");
    std::ofstream(outside.root / ".platypus.toml") << "[checks]\ncommands=[]\n";
    fs::create_symlink(outside.root / ".platypus.toml", workspace.root / ".platypus.toml");
    REQUIRE_FALSE(core::ProjectChecks::discover(workspace.root).is_ok());
}
TEST_CASE("Agent returns approved check failures for up to three repair attempts") {
    Workspace workspace;
    std::ofstream(workspace.root / ".platypus.toml") << R"toml([checks]
commands = [{name = "unit", argv = ["python3", "-c", "import sys; print('check-failure'); sys.exit(7)"]}]
)toml";
    struct CapturingFake : llm::FakeBackend {
        std::vector<std::vector<std::pair<std::string, std::string>>> prompts;
        util::Result<std::vector<llm::ChatChunk>>
        generate_chat(const std::vector<std::pair<std::string, std::string>> &messages, float temp,
                      int max_tokens) override {
            prompts.push_back(messages);
            return llm::FakeBackend::generate_chat(messages, temp, max_tokens);
        }
    };
    auto backend = std::make_shared<CapturingFake>();
    backend->set_script("check-task", {"Working", "Still fixing", "Still fixing"});
    std::size_t approvals = 0;
    auto registry = std::make_shared<core::ToolRegistry>(workspace.root, [&](const auto &) {
        ++approvals;
        return true;
    });
    core::ExecutionEngine engine(backend, registry);
    auto result = engine.run("check-task", 5);
    REQUIRE_FALSE(result.is_ok());
    REQUIRE(result.error().find("after three attempts") != std::string::npos);
    REQUIRE(approvals == 3);
    auto records = core::ProjectChecks::load_results(workspace.root);
    REQUIRE(records.is_ok());
    REQUIRE(records.value().size() == 3);
    REQUIRE(records.value().back().status == "failed");
    REQUIRE(records.value().back().has_exit_code);
    REQUIRE(records.value().back().exit_code == 7);
    bool saw_untrusted_failure = false;
    for (const auto &prompt : backend->prompts)
        for (const auto &[role, content] : prompt)
            if (role == "user" &&
                content.find("Untrusted project-check output") != std::string::npos &&
                content.find("check-failure") != std::string::npos)
                saw_untrusted_failure = true;
    REQUIRE(saw_untrusted_failure);
}
TEST_CASE("Workspace escape and symlink targets are rejected") {
    Workspace workspace, outside;
    fs::create_symlink(outside.root, workspace.root / "link");
    util::SnapshotManager snapshots(workspace.root);
    REQUIRE_FALSE(snapshots.resolve("../outside").is_ok());
    REQUIRE_FALSE(snapshots.resolve("link/secret").is_ok());
    REQUIRE_FALSE(snapshots.resolve(".git/config").is_ok());
}
TEST_CASE("Durable snapshot restores files and removes newly created files") {
    Workspace workspace;
    std::ofstream(workspace.root / "existing") << "before";
    {
        util::SnapshotManager snapshots(workspace.root, [](const auto &) { return true; });
        REQUIRE(snapshots.write("existing", "after").is_ok());
    }
    util::SnapshotManager snapshots(workspace.root, [](const auto &) { return true; });
    REQUIRE(snapshots.rollback().is_ok());
    REQUIRE(read(workspace.root / "existing") == "before");
    REQUIRE(snapshots.write("new", "created").is_ok());
    REQUIRE(snapshots.rollback().is_ok());
    REQUIRE_FALSE(fs::exists(workspace.root / "new"));
}
TEST_CASE("Tool failure and invalid call can be recovered") {
    Workspace workspace;
    auto backend = std::make_shared<llm::FakeBackend>();
    backend->set_script(
        "recovery-task",
        {R"({"tool_call":{"name":"read","arguments":[]}})",
         R"({"tool_call":{"name":"read","arguments":{"path":"missing"}}})",
         R"({"tool_call":{"name":"write","arguments":{"path":"new","content":"fixed"}}})", "Done"});
    auto tools =
        std::make_shared<core::ToolRegistry>(workspace.root, [](const auto &) { return true; });
    core::register_default_tools(*tools);
    core::ExecutionEngine engine(backend, tools);
    REQUIRE(engine.run("recovery-task").is_ok());
    REQUIRE(read(workspace.root / "new") == "fixed");
}
TEST_CASE("Loop is bounded and cancellation returns an error") {
    Workspace workspace;
    auto backend = std::make_shared<llm::FakeBackend>();
    backend->set_script("loop-task", std::vector<std::string>(
                                         30, R"({"tool_call":{"name":"list","arguments":{}}})"));
    auto tools = std::make_shared<core::ToolRegistry>(workspace.root);
    core::register_default_tools(*tools);
    core::ExecutionEngine engine(backend, tools);
    REQUIRE_FALSE(engine.run("loop-task", 4).is_ok());
    std::stop_source source;
    source.request_stop();
    REQUIRE_FALSE(engine.run("cancel", 25, true, source.get_token()).is_ok());
}
TEST_CASE("Processes have bounded output, timeout and correct exit status") {
    Workspace workspace;
    auto result = platform::run_process({"/bin/sh", "-c", "printf abcdef"}, workspace.root,
                                        std::chrono::seconds(1), 3);
    REQUIRE(result.is_ok());
    REQUIRE(result.value().truncated);
    REQUIRE(result.value().output.starts_with("abc"));
    REQUIRE_FALSE(platform::run_process({"/bin/sh", "-c", "sleep 10"}, workspace.root,
                                        std::chrono::milliseconds(50))
                      .is_ok());
    auto failed = platform::run_process({"/bin/sh", "-c", "exit 7"}, workspace.root);
    REQUIRE(failed.is_ok());
    REQUIRE(failed.value().exit_code == 7);
}

TEST_CASE("Undo walks multiple snapshots and preserves modes and symlinks") {
    Workspace workspace;
    std::ofstream(workspace.root / "file") << "original";
    fs::permissions(workspace.root / "file", fs::perms::owner_all);
    fs::create_symlink("file", workspace.root / "link");
    util::SnapshotManager snapshots(workspace.root, [](const auto &) { return true; });
    REQUIRE(snapshots.write("file", "first").is_ok());
    REQUIRE(snapshots.write("file", "second").is_ok());
    REQUIRE(snapshots.rollback().is_ok());
    REQUIRE(read(workspace.root / "file") == "first");
    REQUIRE(snapshots.rollback().is_ok());
    REQUIRE(read(workspace.root / "file") == "original");
    REQUIRE(fs::is_symlink(workspace.root / "link"));
    REQUIRE(fs::status(workspace.root / "file").permissions() == fs::perms::owner_all);
}

TEST_CASE("Profiles load TOML and reject unknown syntax") {
    Workspace workspace;
    auto file = workspace.root / "profile.toml";
    std::ofstream(file) << "name='test'\ntool_syntax='json_tagged'\ncontext_tokens=16384\n"
                           "[sampling]\ntop_k=17\ntop_p=0.91\nseed=42\n";
    auto loaded = llm::ModelProfileManager::read_profile(file.string());
    REQUIRE(loaded.is_ok());
    REQUIRE(loaded.value().sampling.at("top_k") == 17);
    REQUIRE(loaded.value().sampling.at("top_p") == 0.91);
    REQUIRE(loaded.value().sampling.at("seed") == 42);
    std::ofstream(file) << "name='test'\ntool_syntax='unknown'\n";
    REQUIRE_FALSE(llm::ModelProfileManager::read_profile(file.string()).is_ok());
    REQUIRE_FALSE(
        llm::ModelProfileManager::read_profile((workspace.root / "missing").string()).is_ok());
}
TEST_CASE("Profiles reject mistyped settings and invalid budgets instead of silently defaulting") {
    Workspace workspace;
    auto file = workspace.root / "profile.toml";
    for (const auto *setting :
         {"context_tokens='16384'", "context_tokens=1023", "context_tokens=16384.5",
          "context_tokens=4294983680", "temperature='cold'", "temperature=nan",
          "stop_strings='stop'", "stop_strings=[1]", "tool_syntax=3", "template=false",
          "[template]\nturn_prefix='missing role'", "[template]\nturn_suffix=4",
          "[reasoning]\npreserve='false'", "[reasoning]\nopen='<think>'", "[sampling]\ntop_k=1.5",
          "[sampling]\ntop_p=1.1", "[sampling]\nseed=-2", "[sampling]\nunknown=1"}) {
        INFO(setting);
        std::ofstream(file) << "name='fixture'\n" << setting << '\n';
        REQUIRE_FALSE(llm::ModelProfileManager::read_profile(file.string()).is_ok());
    }
}

TEST_CASE("Native tool syntax families retain string and numeric arguments") {
    auto xml = core::Codec::parse_tool_calls(R"(<tool_call><function=read><parameter=path>
file.txt
</parameter><parameter=start_line>2</parameter></function></tool_call>)",
                                             "qwen_xml");
    REQUIRE(xml.size() == 1);
    REQUIRE(xml[0].arguments["path"] == "file.txt");
    REQUIRE(xml[0].arguments["start_line"] == 2);
    auto gemma = core::Codec::parse_tool_calls(
        R"(<|tool_call>call:read{path:<|"|>file.txt<|"|>,start_line:2}<tool_call|>)", "gemma");
    REQUIRE(gemma.size() == 1);
    REQUIRE(gemma[0].tool_name == "read");
    REQUIRE(gemma[0].arguments["path"] == "file.txt");
}
TEST_CASE("Client-side template preserves the prompt prefix across consecutive turns") {
    llm::ModelProfile profile;
    profile.turn_prefix = "<turn>{role}\n";
    profile.turn_suffix = "</turn>\n";
    profile.generation_suffix = "<thinking></thinking>";
    std::vector<std::pair<std::string, std::string>> history{{"system", "fixed"},
                                                             {"user", "request"}};
    auto first = llm::ChatFormatter::render(history, profile);
    history.emplace_back("assistant", "response");
    history.emplace_back("user", "observation");
    REQUIRE(llm::ChatFormatter::render(history, profile).starts_with(first));
}

TEST_CASE("Sessions preserve exact text and resume after reconnect") {
    Workspace workspace;
    std::string id;
    {
        session::Store store(workspace.root);
        auto created = store.create("title");
        REQUIRE(created.is_ok());
        id = created.value();
        REQUIRE(store.append(id, "user", "quote'\nnext").is_ok());
    }
    session::Store store(workspace.root);
    auto loaded = store.load("");
    REQUIRE(loaded.is_ok());
    REQUIRE(loaded.value().first == id);
    REQUIRE(loaded.value().second.size() == 1);
    REQUIRE(loaded.value().second[0].second == "quote'\nnext");
    REQUIRE_FALSE(store.load("999999").is_ok());
}

TEST_CASE("Cancellation interrupts a running command and no background child survives") {
    Workspace workspace;
    std::stop_source stop;
    std::jthread cancel([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        stop.request_stop();
    });
    auto result = platform::run_process({"/bin/sh", "-c", "sleep 30"}, workspace.root,
                                        std::chrono::seconds(5), 1000, stop.get_token());
    REQUIRE_FALSE(result.is_ok());
    REQUIRE(result.error() == "Cancelled");
    auto background = platform::run_process(
        {"/bin/sh", "-c", "(sleep 0.2; touch late) >/dev/null 2>&1 &"}, workspace.root);
    REQUIRE(background.is_ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    REQUIRE_FALSE(fs::exists(workspace.root / "late"));
}

TEST_CASE("Undo restores empty directories and removes new empty directories") {
    Workspace workspace;
    fs::create_directory(workspace.root / "original-empty");
    util::SnapshotManager snapshots(workspace.root, [](const auto &) { return true; });
    REQUIRE(snapshots.take_snapshot("before directory changes").is_ok());
    fs::remove(workspace.root / "original-empty");
    fs::create_directory(workspace.root / "new-empty");
    REQUIRE(snapshots.rollback().is_ok());
    REQUIRE(fs::is_directory(workspace.root / "original-empty"));
    REQUIRE_FALSE(fs::exists(workspace.root / "new-empty"));
}

TEST_CASE("FakeBackend replays Phase 0 JSONL recordings and rejects malformed input") {
    Workspace workspace;
    auto file = workspace.root / "recording.jsonl";
    std::ofstream output(file);
    output << nlohmann::json{{"endpoint", "/health"}, {"response", {{"status", "ok"}}}}.dump()
           << '\n';
    output
        << nlohmann::json{{"endpoint", "/completion"}, {"response", {{"content", "First"}}}}.dump()
        << '\n';
    output << nlohmann::json{{"endpoint", "/v1/chat/completions"},
                             {"response",
                              {{"choices",
                                nlohmann::json::array({{{"message", {{"content", "Second"}}}}})}}}}
                  .dump()
           << '\n';
    output.close();
    llm::FakeBackend backend;
    REQUIRE(backend.load_recording(file.string()).is_ok());
    REQUIRE(backend.generate_chat({{"user", "query"}}).value()[0].content == "First");
    REQUIRE(backend.generate_chat({{"user", "query"}}).value()[0].content == "Second");
    REQUIRE_FALSE(backend.generate_chat({{"user", "query"}}).is_ok());
    std::ofstream(file) << "invalid";
    REQUIRE_FALSE(backend.load_recording(file.string()).is_ok());
}

TEST_CASE("Qwen tool observations use native response blocks and preserve the cached prefix") {
    llm::ModelProfile profile;
    profile.tool_syntax = "qwen_xml";
    profile.turn_prefix = "<|im_start|>{role}\n";
    profile.turn_suffix = "<|im_end|>\n";
    profile.generation_suffix = "<think>\n\n</think>\n\n";
    std::vector<std::pair<std::string, std::string>> history{{"system", "fixed"},
                                                             {"user", "query"}};
    auto before = llm::ChatFormatter::render(history, profile);
    auto stable_prefix = llm::ChatFormatter::stable_prefix(history, profile);
    REQUIRE(stable_prefix == "<|im_start|>system\nfixed<|im_end|>\n");
    std::string call =
        "<tool_call><function=read><parameter=path>x</parameter></function></tool_call>";
    history.emplace_back("assistant", call);
    history.emplace_back("tool", llm::ChatFormatter::tool_observation("read", "one", false));
    history.emplace_back("tool", llm::ChatFormatter::tool_observation("read", "missing", true));
    auto after = llm::ChatFormatter::render(history, profile);
    REQUIRE(llm::ChatFormatter::stable_prefix(history, profile) == stable_prefix);
    REQUIRE(after.starts_with(before + call));
    REQUIRE(after.find(
                "<|im_start|>user\n<tool_response>\none\n</tool_response>\n<tool_response>\nError: "
                "missing\n</tool_response><|im_end|>\n") != std::string::npos);
    REQUIRE(after.find("<|im_start|>tool") == std::string::npos);
}

TEST_CASE("Gemma tool results continue one model turn without an extra generation header") {
    llm::ModelProfile profile;
    profile.tool_syntax = "gemma";
    profile.assistant_role = "model";
    profile.turn_prefix = "<|turn>{role}\n";
    profile.turn_suffix = "<turn|>\n";
    profile.generation_suffix = "<|channel>thought\n<channel|>";
    std::vector<std::pair<std::string, std::string>> history{{"user", "query"}};
    auto before = llm::ChatFormatter::render(history, profile);
    std::string call = "<|tool_call>call:read{path:<|\"|>x<|\"|>}<tool_call|>";
    history.emplace_back("assistant", call);
    history.emplace_back("tool", llm::ChatFormatter::tool_observation("read", "one", false));
    auto expected =
        before + call + "<|tool_response>response:read{value:<|\"|>one<|\"|>}<tool_response|>";
    REQUIRE(llm::ChatFormatter::render(history, profile) == expected);
    history.emplace_back("assistant", "Done");
    history.emplace_back("user", "Next");
    auto next = llm::ChatFormatter::render(history, profile);
    REQUIRE(next.starts_with(expected + "Done<turn|>\n<|turn>user\nNext<turn|>\n"));
    history.erase(history.end() - 2);
    REQUIRE(llm::ChatFormatter::render(history, profile)
                .starts_with(expected + "<turn|>\n<|turn>user\nNext"));
}

TEST_CASE("Native declarations advertise schemas without conflicting JSON call instructions") {
    nlohmann::json tools = {{{"type", "function"},
                             {"function",
                              {{"name", "read"},
                               {"description", "Read a file"},
                               {"parameters",
                                {{"type", "object"},
                                 {"properties", {{"path", {{"type", "string"}}}}},
                                 {"required", {"path"}}}}}}}};
    auto gemma = llm::ChatFormatter::tool_definitions(tools, "gemma");
    REQUIRE(gemma.find("<|tool>declaration:read{") == 0);
    REQUIRE(gemma.find("path:{type:<|\"|>STRING<|\"|>}") != std::string::npos);
    REQUIRE(gemma.find("required:[<|\"|>path<|\"|>]") != std::string::npos);
    auto qwen = llm::ChatFormatter::tool_definitions(tools, "qwen_xml");
    REQUIRE(qwen.find("<tools>\n{\"function\"") != std::string::npos);
    REQUIRE(qwen.ends_with("</tools>\n"));
    REQUIRE(llm::ChatFormatter::tool_call_instruction("gemma").find("Use JSON") ==
            std::string::npos);
}

TEST_CASE("GBNF tool grammar supports all native syntax families and flat property types") {
    nlohmann::json tools = {{{"type", "function"},
                             {"function",
                              {{"name", "read_file"},
                               {"parameters",
                                {{"type", "object"},
                                 {"properties",
                                  {{"path", {{"type", "string"}}},
                                   {"start_line", {{"type", "integer"}}},
                                   {"enabled", {{"type", "boolean"}}},
                                   {"score", {{"type", "number"}}}}},
                                 {"required", {"path"}}}}}}}};
    auto json = llm::ToolGrammar::generate(tools, "json_tagged");
    REQUIRE(json.is_ok());
    REQUIRE(json.value().find("\\\"tool_call\\\"") != std::string::npos);
    REQUIRE(json.value().find("arguments-a ::= ") != std::string::npos);
    REQUIRE(json.value().find("integer ::= ") != std::string::npos);
    REQUIRE(json.value().find("number ::= ") != std::string::npos);
    REQUIRE(json.value().find("boolean ::= ") != std::string::npos);

    auto qwen = llm::ToolGrammar::generate(tools, "qwen_xml");
    REQUIRE(qwen.is_ok());
    REQUIRE(qwen.value().find("<tool_call><function=read_file>") != std::string::npos);
    REQUIRE(qwen.value().find("<parameter=path>") != std::string::npos);

    auto gemma = llm::ToolGrammar::generate(tools, "gemma");
    REQUIRE(gemma.is_ok());
    REQUIRE(gemma.value().find("<|tool_call>call:read_file{") != std::string::npos);
    REQUIRE(gemma.value().find("gemma-string ::= ") != std::string::npos);
}

TEST_CASE("GBNF generator rejects unsupported schema declarations") {
    auto tools = nlohmann::json::array(
        {{{"type", "function"},
          {"function", {{"name", "bad-tool"}, {"parameters", {{"type", "object"}}}}}}});
    REQUIRE_FALSE(llm::ToolGrammar::generate(tools, "json_tagged").is_ok());
    tools[0]["function"]["name"] = "read";
    tools[0]["function"]["parameters"] = {{"type", "object"},
                                          {"properties", {{"path", {{"type", "array"}}}}}};
    REQUIRE_FALSE(llm::ToolGrammar::generate(tools, "json_tagged").is_ok());
    REQUIRE_FALSE(llm::ToolGrammar::generate(tools, "unknown").is_ok());
}

TEST_CASE("Repository map indexes and ranks six language families within its output budget") {
    Workspace workspace;
    auto put = [&](const std::string &path, const std::string &content) {
        fs::create_directories((workspace.root / path).parent_path());
        std::ofstream(workspace.root / path) << content;
    };
    put("src/math.c", "int calculate(int input) { return input; }\n");
    put("src/runner.cpp", "class Runner { public: int run(int input) { return input; } };\n");
    put("src/parser.py", "def parse_input(value):\n    return value\n");
    put("src/render.js", "function render_view(value) { return value; }\n");
    put("src/user.ts", "interface UserRecord { name: string; }\nfunction loadUser() {}\n");
    put("src/component.tsx", "export function Component() { return <div />; }\n");
    put("src/worker.rs", "struct Worker;\nfn execute_task() {}\n");
    put("src/server.go", "type ServerRecord struct{}\nfunc ProcessRequest() {}\n");

    auto map = agent::RepositoryMap::build(
        workspace.root,
        "calculate Runner run parse_input render_view UserRecord Component execute_task "
        "ProcessRequest",
        {}, 4096);
    REQUIRE(map.is_ok());
    INFO(map.value());
    for (const auto &name : {"calculate", "Runner", "run", "parse_input", "render_view",
                             "UserRecord", "Component", "execute_task", "ProcessRequest"})
        REQUIRE(map.value().find(name) != std::string::npos);
    REQUIRE(map.value().size() <= 4096);
    REQUIRE_FALSE(agent::RepositoryMap::build(workspace.root, "run", {}, 4).is_ok());
}

TEST_CASE("Execution adds a transient repository map after the stable prefix and budgets it") {
    struct CapturingBackend : llm::FakeBackend {
        std::vector<std::pair<std::string, std::string>> sized_messages;
        std::vector<std::pair<std::string, std::string>> generated_messages;

        util::Result<llm::PromptSize>
        count_prompt_tokens(const std::vector<std::pair<std::string, std::string>> &messages,
                            std::stop_token stop) override {
            sized_messages = messages;
            return llm::LlmBackend::count_prompt_tokens(messages, stop);
        }
        util::Result<llm::CompletionResult> complete(const llm::CompletionRequest &request,
                                                     llm::TokenCallback callback,
                                                     std::stop_token stop) override {
            generated_messages = request.messages;
            return llm::LlmBackend::complete(request, std::move(callback), stop);
        }
    };
    Workspace workspace;
    std::ofstream(workspace.root / "alpha.cpp") << "class Alpha { public: void execute() {} };\n";
    auto backend = std::make_shared<CapturingBackend>();
    backend->set_script("map-task", {"Done"});
    auto tools = std::make_shared<core::ToolRegistry>(workspace.root);
    core::register_default_tools(*tools);
    auto sessions = std::make_shared<session::Store>(workspace.root);
    auto id = sessions->create("repository-map");
    REQUIRE(id.is_ok());
    core::ExecutionEngine engine(backend, tools);
    REQUIRE(engine.attach_session(sessions, id.value()).is_ok());
    REQUIRE(engine.run("map-task Alpha").is_ok());

    REQUIRE(backend->sized_messages == backend->generated_messages);
    REQUIRE(backend->generated_messages.size() >= 3);
    REQUIRE(backend->generated_messages[0].first == "system");
    REQUIRE(backend->generated_messages[1].first == "user");
    REQUIRE(backend->generated_messages[1].second.find(
                "Untrusted repository navigation metadata") != std::string::npos);
    REQUIRE(backend->generated_messages[1].second.find("Alpha") != std::string::npos);
    auto stored = sessions->load(id.value());
    REQUIRE(stored.is_ok());
    for (const auto &[role, content] : stored.value().second) {
        static_cast<void>(role);
        REQUIRE(content.find("Untrusted repository navigation metadata") == std::string::npos);
    }
}

TEST_CASE("Repository map skips symlinks and dependency directories") {
    Workspace workspace, outside;
    const auto secret = "UnindexedExternalDeclaration";
    std::ofstream(outside.root / "external.cpp") << "void " << secret << "() {}\n";
    std::error_code error;
    fs::create_symlink(outside.root / "external.cpp", workspace.root / "linked.cpp", error);
    REQUIRE_FALSE(error);
    fs::create_directories(workspace.root / "node_modules");
    std::ofstream(workspace.root / "node_modules" / "dependency.cpp")
        << "void " << secret << "() {}\n";

    auto map = agent::RepositoryMap::build(workspace.root, secret, {});
    REQUIRE(map.is_ok());
    REQUIRE(map.value().find(secret) == std::string::npos);
}

TEST_CASE("Qwen arguments use declared types rather than guessing numeric-looking filenames") {
    nlohmann::json schemas = {
        {"read",
         {{"properties",
           {{"path", {{"type", "string"}}}, {"start_line", {{"type", "integer"}}}}}}}};
    auto calls =
        core::Codec::parse_tool_calls("<function=read><parameter=path>true</"
                                      "parameter><parameter=start_line>2</parameter></function>",
                                      "qwen_xml", schemas);
    REQUIRE(calls.size() == 1);
    REQUIRE(calls[0].arguments["path"] == "true");
    REQUIRE(calls[0].arguments["start_line"] == 2);
}

TEST_CASE("A failed batch logs a result for each call without executing the remaining mutations") {
    Workspace workspace;
    auto backend = std::make_shared<llm::FakeBackend>();
    backend->set_script(
        "batch-task",
        {R"({"tool_calls":[{"name":"read","arguments":{"path":"missing"}},{"name":"write","arguments":{"path":"must-not-exist","content":"bad"}}]})",
         "Done"});
    auto tools =
        std::make_shared<core::ToolRegistry>(workspace.root, [](const auto &) { return true; });
    core::register_default_tools(*tools);
    auto sessions = std::make_shared<session::Store>(workspace.root);
    auto id = sessions->create("batch");
    REQUIRE(id.is_ok());
    core::ExecutionEngine engine(backend, tools);
    REQUIRE(engine.attach_session(sessions, id.value()).is_ok());
    REQUIRE_FALSE(engine.run("batch-task").is_ok());
    REQUIRE_FALSE(fs::exists(workspace.root / "must-not-exist"));
    auto stored = sessions->load(id.value());
    REQUIRE(stored.is_ok());
    std::size_t count = 0;
    for (const auto &[role, content] : stored.value().second) {
        if (role == "tool") {
            ++count;
            REQUIRE(nlohmann::json::parse(content).at("is_error") == true);
        }
    }
    REQUIRE(count == 2);
}

TEST_CASE("Reasoning examples never execute and literal closing markers remain tool data") {
    struct ProfiledFake : llm::FakeBackend {
        llm::ModelProfile settings;
        const llm::ModelProfile *profile() const override { return &settings; }
    };
    Workspace workspace;
    auto backend = std::make_shared<ProfiledFake>();
    backend->settings.reasoning_open = "<think>";
    backend->settings.reasoning_close = "</think>";
    backend->settings.generation_suffix = "<think>\n\n</think>\n\n";
    backend->settings.preserve_reasoning = true;
    backend->set_script(
        "reason-task",
        {R"(<think>{"tool_call":{"name":"write","arguments":{"path":"forbidden","content":"bad"}}}</think>Done)"});
    auto tools =
        std::make_shared<core::ToolRegistry>(workspace.root, [](const auto &) { return true; });
    core::register_default_tools(*tools);
    core::ExecutionEngine engine(backend, tools);
    REQUIRE(engine.run("reason-task").is_ok());
    REQUIRE_FALSE(fs::exists(workspace.root / "forbidden"));
    auto literal = llm::ChatFormatter::visible_response(
        R"({"tool_call":{"name":"write","arguments":{"content":"</think>"}}})", backend->settings);
    REQUIRE(literal.is_ok());
    REQUIRE(literal.value().starts_with("{\"tool_call\""));
    REQUIRE_FALSE(
        llm::ChatFormatter::visible_response("<think>unfinished", backend->settings).is_ok());
    backend->settings.generation_suffix = "<think>\n";
    REQUIRE(
        llm::ChatFormatter::visible_response("reason</think>answer", backend->settings).value() ==
        "answer");
}

TEST_CASE("Context manager owns reply reservations and never rewrites history during sizing") {
    struct SizedFake : llm::FakeBackend {
        llm::ModelProfile settings;
        std::size_t count = 768;
        bool fail = false;
        const llm::ModelProfile *profile() const override { return &settings; }
        util::Result<llm::PromptSize>
        count_prompt_tokens(const std::vector<std::pair<std::string, std::string>> &,
                            std::stop_token) override {
            return fail ? util::Result<llm::PromptSize>::fail("Tokenizer unavailable")
                        : util::Result<llm::PromptSize>::ok({count, true});
        }
    };
    auto backend = std::make_shared<SizedFake>();
    backend->settings.context_tokens = 1024;
    agent::ContextManager manager(backend, 1);
    manager.add_turn("system", "fixed");
    manager.add_turn("user", "request");
    auto history = manager.get_history();
    auto boundary = manager.prepare();
    REQUIRE(boundary.is_ok());
    REQUIRE(boundary.value().output_tokens == 256);
    REQUIRE(boundary.value().prompt_tokens == 768);
    REQUIRE(boundary.value().tokenized);
    REQUIRE(boundary.value().describe().find("tokenized") != std::string::npos);
    ++backend->count;
    REQUIRE_FALSE(manager.prepare().is_ok());
    REQUIRE(manager.usage()->prompt_tokens == 769);
    REQUIRE(manager.get_history() == history);
    backend->fail = true;
    REQUIRE(manager.prepare().error() == "Tokenizer unavailable");
    REQUIRE_FALSE(manager.usage().has_value());
    backend->fail = false;
    std::stop_source cancelled;
    cancelled.request_stop();
    REQUIRE(manager.prepare(cancelled.get_token()).error() == "Cancelled");
    REQUIRE(manager.get_history() == history);
}

TEST_CASE("Estimated context includes serialized observations and errors before generation") {
    auto backend = std::make_shared<llm::FakeBackend>();
    agent::ContextManager manager(backend);
    manager.add_turn("user", "request");
    auto first = manager.prepare();
    REQUIRE(first.is_ok());
    REQUIRE_FALSE(first.value().tokenized);
    REQUIRE(first.value().describe().find("estimated") != std::string::npos);
    manager.add_turn("assistant", "call");
    manager.add_turn("tool", llm::ChatFormatter::tool_observation("read", "contents", false));
    REQUIRE(manager.prepare().value().prompt_tokens > first.value().prompt_tokens);
    manager.add_turn("user", std::string(16000, 'x'));
    auto history = manager.get_history();
    REQUIRE_FALSE(manager.prepare().is_ok());
    REQUIRE(manager.get_history() == history);
    agent::ContextManager missing(nullptr);
    REQUIRE_FALSE(missing.prepare().is_ok());
}

TEST_CASE("Engine reports a full context and stops before generation or mutation") {
    struct SizedFake : llm::FakeBackend {
        bool generated = false;
        util::Result<llm::PromptSize>
        count_prompt_tokens(const std::vector<std::pair<std::string, std::string>> &,
                            std::stop_token) override {
            return util::Result<llm::PromptSize>::ok({16384, true});
        }
        util::Result<llm::CompletionResult> complete(const llm::CompletionRequest &,
                                                     llm::TokenCallback, std::stop_token) override {
            generated = true;
            return util::Result<llm::CompletionResult>::ok({"unreachable"});
        }
    };
    Workspace workspace;
    auto backend = std::make_shared<SizedFake>();
    auto tools = std::make_shared<core::ToolRegistry>(workspace.root);
    core::register_default_tools(*tools);
    core::ExecutionEngine engine(backend, tools);
    std::optional<agent::ContextUsage> seen;
    engine.set_context_callback([&](const auto &usage) { seen = usage; });
    auto result = engine.run("request");
    REQUIRE_FALSE(result.is_ok());
    REQUIRE(result.error().find("Context budget") != std::string::npos);
    REQUIRE_FALSE(backend->generated);
    REQUIRE(seen.has_value());
    REQUIRE(seen->output_tokens == 2048);
    REQUIRE(seen->prompt_tokens == 16384);
}

TEST_CASE("Canonical tools validate arguments before executing and carry explicit status flags") {
    Workspace workspace;
    util::SnapshotManager snapshots(workspace.root);
    tools::ToolContext context{snapshots};
    int executions = 0;
    tools::Tool tool{"example",
                     "A test seam",
                     {{"type", "object"},
                      {"properties", {{"path", {{"type", "string"}}}}},
                      {"required", {"path"}},
                      {"additionalProperties", false}},
                     [&](const nlohmann::json &, tools::ToolContext &) {
                         ++executions;
                         return tools::ToolResult::ok("observed");
                     }};
    REQUIRE(tool.name() == "example");
    REQUIRE(tool.description() == "A test seam");
    REQUIRE(tool.json_schema().at("required")[0] == "path");
    for (const auto &args :
         {nlohmann::json::array(), nlohmann::json::object(), nlohmann::json{{"path", 3}},
          nlohmann::json{{"path", "x"}, {"extra", true}}})
        REQUIRE(tool.execute(args, context).is_error);
    REQUIRE(executions == 0);
    auto result = tool.execute({{"path", "x"}}, context);
    REQUIRE_FALSE(result.is_error);
    REQUIRE_FALSE(result.truncated);
    REQUIRE(result.content == "observed");
    REQUIRE(executions == 1);
    std::stop_source cancelled;
    cancelled.request_stop();
    snapshots.set_stop_token(cancelled.get_token());
    REQUIRE(tool.execute({{"path", "x"}}, context).is_error);
    REQUIRE(executions == 1);
}

TEST_CASE("Tool output caps preserve UTF-8 boundaries and replace invalid binary bytes") {
    Workspace workspace;
    util::SnapshotManager snapshots(workspace.root);
    tools::ToolContext context{snapshots, 64};
    std::string content = std::string(40, 'x');
    for (int i = 0; i < 10; ++i)
        content += "\xe2\x82\xac";
    tools::Tool tool{"text", "Output test", {{"type", "object"}}, [&](const auto &, auto &) {
                         return tools::ToolResult::ok(content);
                     }};
    auto result = tool.execute(nlohmann::json::object(), context);
    REQUIRE(result.truncated);
    REQUIRE_FALSE(result.is_error);
    REQUIRE(result.content.size() <= context.max_output_bytes);
    REQUIRE(result.content.starts_with(std::string(40, 'x') + "\xe2\x82\xac"));
    REQUIRE(result.content.ends_with("\n[output truncated]"));
    REQUIRE(nlohmann::json::parse(nlohmann::json(result.content).dump()) == result.content);
    content = "\xff\x80";
    result = tool.execute(nlohmann::json::object(), context);
    REQUIRE_FALSE(result.truncated);
    REQUIRE(result.content == "\xef\xbf\xbd\xef\xbf\xbd");
    // Invalid overlong, surrogate and out-of-range sequences are never allowed into JSON.
    for (const auto *bytes : {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80"}) {
        content = bytes;
        result = tool.execute(nlohmann::json::object(), context);
        REQUIRE(nlohmann::json::parse(nlohmann::json(result.content).dump()) == result.content);
    }
    content = std::string(100, 'x');
    for (std::size_t limit = 0; limit < 24; ++limit) {
        context.max_output_bytes = limit;
        result = tool.execute(nlohmann::json::object(), context);
        REQUIRE(result.truncated);
        REQUIRE(result.content.size() <= limit);
    }
}

TEST_CASE("Tool errors and exceptions are bounded without losing their status") {
    Workspace workspace;
    util::SnapshotManager snapshots(workspace.root);
    tools::ToolContext context{snapshots, 100};
    tools::Tool error{"error", "Error test", {{"type", "object"}}, [](const auto &, auto &) {
                          return tools::ToolResult::fail(std::string(200, 'e'));
                      }};
    auto result = error.execute(nlohmann::json::object(), context);
    REQUIRE(result.is_error);
    REQUIRE(result.truncated);
    REQUIRE(result.content.size() == context.max_output_bytes);
    tools::Tool throwing{"throw",
                         "Exception test",
                         {{"type", "object"}},
                         [](const auto &, auto &) -> tools::ToolResult {
                             throw std::runtime_error("handler failed");
                         }};
    result = throwing.execute(nlohmann::json::object(), context);
    REQUIRE(result.is_error);
    REQUIRE(result.content == "handler failed");
}

TEST_CASE("Read observations retain truncation status in the append-only session") {
    Workspace workspace;
    std::ofstream(workspace.root / "large") << std::string(5998, 'x') << "\xe2\x82\xac\n";
    auto registry = std::make_shared<core::ToolRegistry>(workspace.root);
    core::register_default_tools(*registry);
    auto observed = registry->execute_tool("read", {{"path", "large"}});
    REQUIRE_FALSE(observed.is_error);
    REQUIRE(observed.truncated);
    REQUIRE(observed.content.size() <= 6000);
    auto backend = std::make_shared<llm::FakeBackend>();
    backend->set_script("read-task",
                        {R"({"tool_call":{"name":"read","arguments":{"path":"large"}}})", "Done"});
    auto store = std::make_shared<session::Store>(workspace.root);
    auto id = store->create("read");
    REQUIRE(id.is_ok());
    core::ExecutionEngine engine(backend, registry);
    REQUIRE(engine.attach_session(store, id.value()).is_ok());
    REQUIRE(engine.run("read-task").is_ok());
    auto history = store->load(id.value());
    REQUIRE(history.is_ok());
    bool seen = false;
    for (const auto &[role, content] : history.value().second)
        if (role == "tool") {
            seen = true;
            REQUIRE(nlohmann::json::parse(content).at("truncated") == true);
        }
    REQUIRE(seen);
}

TEST_CASE("SnapshotStore lists durable IDs and restores an older ID with undo of the restore") {
    Workspace workspace;
    safety::SnapshotStore store(workspace.root, [](const auto &) { return true; });
    REQUIRE(store.list().is_ok());
    REQUIRE(store.list().value().empty());
    REQUIRE_FALSE(fs::exists(workspace.root / ".platypus"));
    std::ofstream(workspace.root / "file") << "one";
    auto first = store.snapshot("first");
    REQUIRE(first.is_ok());
    std::ofstream(workspace.root / "file") << "two";
    auto second = store.snapshot("second");
    REQUIRE(second.is_ok());
    REQUIRE(first.value() != second.value());
    safety::SnapshotStore reconnected(workspace.root, [](const auto &) { return true; });
    auto ids = reconnected.list();
    REQUIRE(ids.is_ok());
    REQUIRE(ids.value() == std::vector<safety::SnapshotId>{second.value(), first.value()});
    std::ofstream(workspace.root / "file") << "three";
    REQUIRE(reconnected.restore(first.value()).is_ok());
    REQUIRE(read(workspace.root / "file") == "one");
    REQUIRE(reconnected.rollback().is_ok());
    REQUIRE(read(workspace.root / "file") == "three");
}

TEST_CASE("Invalid or denied snapshot restores do not change files or the undo stack") {
    Workspace workspace;
    safety::SnapshotStore store(workspace.root);
    REQUIRE_FALSE(store.resolve(std::string("file\0ignored", 12)).is_ok());
    std::ofstream(workspace.root / "file") << "before";
    auto id = store.snapshot("before");
    REQUIRE(id.is_ok());
    std::ofstream(workspace.root / "file") << "current";
    auto stack = read(workspace.root / ".platypus/undo.json");
    for (const auto &invalid : {std::string("HEAD"), std::string("--all"), std::string("../escape"),
                                std::string(40, 'f')})
        REQUIRE_FALSE(store.restore(invalid).is_ok());
    REQUIRE(store.restore(id.value()).error() == "Permission denied");
    REQUIRE(read(workspace.root / "file") == "current");
    REQUIRE(read(workspace.root / ".platypus/undo.json") == stack);
}

TEST_CASE("Unsafe snapshot manifests are refused before approval or workspace mutation") {
    Workspace workspace;
    int approvals = 0;
    safety::SnapshotStore store(workspace.root, [&](const auto &) {
        ++approvals;
        return true;
    });
    std::ofstream(workspace.root / "file") << "before";
    auto id = store.snapshot("before");
    REQUIRE(id.is_ok());
    std::ofstream(workspace.root / "file") << "current";
    auto manifest = workspace.root / ".platypus/snapshot-modes" / (id.value() + ".json");
    for (const auto &invalid :
         {nlohmann::json{{"files", {{"../escape", 0644}}},
                         {"directories", nlohmann::json::object()}},
          nlohmann::json{{"files", {{".git/config", 0644}}},
                         {"directories", nlohmann::json::object()}},
          nlohmann::json{{"files", {{"file", -1}}}, {"directories", nlohmann::json::object()}},
          nlohmann::json{{"files", {{"file", 65536}}}, {"directories", nlohmann::json::object()}},
          nlohmann::json{{"files", false}, {"directories", nlohmann::json::object()}}}) {
        std::ofstream(manifest) << invalid.dump();
        REQUIRE_FALSE(store.restore(id.value()).is_ok());
        REQUIRE(read(workspace.root / "file") == "current");
    }
    fs::remove(manifest);
    auto outside = workspace.root / "outside-metadata";
    std::ofstream(outside) << "private";
    fs::create_symlink(outside, manifest);
    REQUIRE_FALSE(store.restore(id.value()).is_ok());
    REQUIRE(read(outside) == "private");
    REQUIRE(approvals == 0);
}

TEST_CASE("Writes refuse changes made to the file during approval") {
    for (bool initially_present : {false, true}) {
        Workspace workspace;
        if (initially_present)
            std::ofstream(workspace.root / "file") << "before";
        safety::SnapshotStore store(workspace.root, [&](const auto &) {
            std::ofstream(workspace.root / "file") << "external change";
            return true;
        });
        auto result = store.write("file", "model replacement");
        REQUIRE_FALSE(result.is_ok());
        REQUIRE(result.error().find("changed during approval") != std::string::npos);
        REQUIRE(read(workspace.root / "file") == "external change");
    }
}

TEST_CASE("Restore refuses content and empty-directory changes made during approval") {
    for (bool directory_change : {false, true}) {
        Workspace workspace;
        std::ofstream(workspace.root / "file") << "before";
        safety::SnapshotStore store(workspace.root, [&](const auto &) {
            if (directory_change)
                fs::create_directory(workspace.root / "external-empty");
            else
                std::ofstream(workspace.root / "file") << "external change";
            return true;
        });
        auto id = store.snapshot("before");
        REQUIRE(id.is_ok());
        std::ofstream(workspace.root / "file") << "current";
        auto stack = read(workspace.root / ".platypus/undo.json");
        auto result = store.restore(id.value());
        REQUIRE_FALSE(result.is_ok());
        REQUIRE(result.error().find("Workspace changed during approval") != std::string::npos);
        REQUIRE(read(workspace.root / ".platypus/undo.json") == stack);
        REQUIRE(read(workspace.root / "file") ==
                (directory_change ? "current" : "external change"));
        if (directory_change)
            REQUIRE(fs::is_directory(workspace.root / "external-empty"));
    }
}

TEST_CASE("A failed metadata restore recovers original content and retains the undo stack") {
    Workspace workspace;
    std::ofstream(workspace.root / "file") << "before";
    safety::SnapshotStore store(workspace.root, [](const auto &) { return true; });
    auto id = store.snapshot("before");
    REQUIRE(id.is_ok());
    std::ofstream(workspace.root / "file") << "current";
    std::ofstream(workspace.root / "extra") << "keep this";
    fs::create_directory(workspace.root / "empty");
    auto manifest = workspace.root / ".platypus/snapshot-modes" / (id.value() + ".json");
    auto modes = nlohmann::json::parse(read(manifest));
    // Structurally valid but inconsistent with the Git tree: exercise failure after read-tree.
    modes["files"]["nonexistent"] = 0644;
    std::ofstream(manifest) << modes.dump();
    auto stack = read(workspace.root / ".platypus/undo.json");
    auto restored = store.restore(id.value());
    REQUIRE_FALSE(restored.is_ok());
    REQUIRE(restored.error().find("original state restored") != std::string::npos);
    REQUIRE(read(workspace.root / "file") == "current");
    REQUIRE(read(workspace.root / "extra") == "keep this");
    REQUIRE(fs::is_directory(workspace.root / "empty"));
    REQUIRE(read(workspace.root / ".platypus/undo.json") == stack);
}

TEST_CASE("Native parsing rejects malformed batches without executing embedded JSON data") {
    auto embedded = R"({"tool_call":{"name":"write","arguments":{"path":"x","content":"bad"}}})";
    REQUIRE(core::Codec::parse_tool_calls(std::string("<function=read><parameter=path>") + embedded,
                                          "qwen_xml")
                .empty());
    REQUIRE(core::Codec::parse_tool_calls(std::string("<|tool_call>call:read{") + embedded, "gemma")
                .empty());
    auto first = R"(<|tool_call>call:read{path:<|"|>a<|"|>}<tool_call|>)";
    auto second = R"(<|tool_call>call:read{path:<|"|>b<|"|>}<tool_call|>)";
    auto batch = core::Codec::parse_tool_calls(std::string(first) + second, "gemma");
    REQUIRE(batch.size() == 2);
    REQUIRE(batch[0].arguments.at("path") == "a");
    REQUIRE(batch[1].arguments.at("path") == "b");
    REQUIRE(
        core::Codec::parse_tool_calls(std::string(first) + "<|tool_call>broken", "gemma").empty());
    REQUIRE(
        core::Codec::parse_tool_calls(
            "<function=read><parameter=path>a</parameter><parameter=path>b</parameter></function>",
            "qwen_xml")
            .empty());
    REQUIRE(core::Codec::parse_tool_calls(
                R"({"tool_call":{"name":"read","\u006eame":"write","arguments":{}}})")
                .empty());
    REQUIRE(core::Codec::parse_tool_calls(
                R"(<|tool_call>call:read{path:<|"|>a<|"|>,path:<|"|>b<|"|>}<tool_call|>)", "gemma")
                .empty());
}

TEST_CASE("Tool-call parser bounds native payloads JSON nesting and batch size") {
    auto native =
        "<function=read><parameter=path>" + std::string(20000, 'x') + "</parameter></function>";
    auto calls = core::Codec::parse_tool_calls(native, "qwen_xml");
    REQUIRE(calls.size() == 1);
    REQUIRE(calls[0].arguments.at("path").get<std::string>().size() == 20000);
    REQUIRE(core::Codec::parse_tool_calls(std::string(65537, 'x') + native, "qwen_xml").empty());
    auto nested = std::string(256, '[') + "0" + std::string(256, ']');
    REQUIRE(core::Codec::parse_tool_calls(
                "{\"tool_call\":{\"name\":\"read\",\"arguments\":{\"path\":" + nested + "}}}")
                .empty());
    nlohmann::json encoded = {
        {"tool_call", {{"name", "read"}, {"arguments", "{\"path\":" + nested + "}"}}}};
    REQUIRE(core::Codec::parse_tool_calls(encoded.dump()).empty());
    auto batch = nlohmann::json::array();
    for (int i = 0; i < 65; ++i)
        batch.push_back({{"name", "read"}, {"arguments", {{"path", "x"}}}});
    REQUIRE(core::Codec::parse_tool_calls(nlohmann::json{{"tool_calls", batch}}.dump()).empty());
    REQUIRE(core::Codec::parse_tool_calls("<function=read><parameter=path>" +
                                              std::string(1, '\xff') + "</parameter></function>",
                                          "qwen_xml")
                .empty());
}

TEST_CASE("Fuzz entry survives deterministic mutations across all syntax families") {
    std::vector<std::string> seeds{
        R"({"tool_call":{"name":"read","arguments":{"path":"x"}}})",
        R"({"tool_call":{"name":"read","arguments":{"path":"x",}}})",
        "<function=read><parameter=path>x</parameter></function>",
        R"(<|tool_call>call:read{path:<|"|>x<|"|>}<tool_call|>)",
        "<function=read><parameter=path>{\"tool_call\":{\"name\":\"write\"}}",
        std::string(300, '['),
        ""};
    std::mt19937 random(1234);
    for (int iteration = 0; iteration < 2000; ++iteration) {
        auto input = seeds[random() % seeds.size()];
        auto position = random() % (input.size() + 1);
        input.insert(position, 1, static_cast<char>(random() % 256));
        if (iteration % 3 == 0)
            input.resize(random() % (input.size() + 1));
        REQUIRE(LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t *>(input.data()),
                                       input.size()) == 0);
        REQUIRE(LLVMFuzzerEditFormatTestOneInput(
                    reinterpret_cast<const std::uint8_t *>(input.data()), input.size()) == 0);
    }
}

TEST_CASE("Agent blocks the fourth identical action and the model recovers") {
    struct CapturingFake : llm::FakeBackend {
        std::vector<std::vector<std::pair<std::string, std::string>>> prompts;
        util::Result<std::vector<llm::ChatChunk>>
        generate_chat(const std::vector<std::pair<std::string, std::string>> &messages, float temp,
                      int max_tokens) override {
            prompts.push_back(messages);
            return llm::FakeBackend::generate_chat(messages, temp, max_tokens);
        }
    };
    auto backend = std::make_shared<CapturingFake>();
    const std::string repeated = R"({"tool_call":{"name":"probe","arguments":{"value":"same"}}})";
    const std::string different =
        R"({"tool_call":{"name":"probe","arguments":{"value":"different"}}})";
    backend->set_script("loop-guard-task",
                        {repeated, repeated, repeated, repeated, different, "Finished."});
    Workspace workspace;
    auto registry = std::make_shared<core::ToolRegistry>(workspace.root);
    int executions = 0;
    registry->register_tool(
        core::Tool{"probe", "Count a probe call.",
                   nlohmann::json{{"type", "object"},
                                  {"properties", {{"value", {{"type", "string"}}}}},
                                  {"required", {"value"}},
                                  {"additionalProperties", false}},
                   [&](const nlohmann::json &, tools::ToolContext &) {
                       ++executions;
                       return tools::ToolResult::ok("probe completed");
                   }});
    core::ExecutionEngine engine(backend, registry);
    auto result = engine.run("loop-guard-task");
    REQUIRE(result.is_ok());
    REQUIRE(result.value() == "Finished.");
    REQUIRE(executions == 4);
    bool saw_correction = false;
    for (const auto &prompt : backend->prompts)
        for (const auto &[role, content] : prompt)
            if (role == "tool" &&
                content.find("Repeated identical action blocked on occurrence 4") !=
                    std::string::npos)
                saw_correction = true;
    REQUIRE(saw_correction);
}
