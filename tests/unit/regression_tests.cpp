// Regression coverage for audit findings: safety, retries, parsing and subprocess lifetimes.
#include "core/codec.hpp"
#include "core/execution_engine.hpp"
#include "llm/chat_formatter.hpp"
#include "llm/fake_backend.hpp"
#include "platform/process.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>
namespace fs = std::filesystem;
using namespace platypus;
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
    std::ofstream(file) << "name='test'\ntool_syntax='json_tagged'\ncontext_tokens=16384\n";
    REQUIRE(llm::ModelProfileManager::read_profile(file.string()).is_ok());
    std::ofstream(file) << "name='test'\ntool_syntax='unknown'\n";
    REQUIRE_FALSE(llm::ModelProfileManager::read_profile(file.string()).is_ok());
    REQUIRE_FALSE(
        llm::ModelProfileManager::read_profile((workspace.root / "missing").string()).is_ok());
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
