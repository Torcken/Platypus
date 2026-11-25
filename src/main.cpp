// Command-line entry point wires the real backend, safe tools and sequential agent.
// Mutating headless runs require --yes; interactive runs approve each diff/command.
#include "core/execution_engine.hpp"
#include "core/plan_commands.hpp"
#include "llm/llm_backend_factory.hpp"
#include "tui/terminal.hpp"
#include <csignal>
#include <iostream>
#include <thread>
#include <unistd.h>
namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
} // namespace
int main(int argc, char *argv[]) {
    using namespace platypus;
    try {
        bool headless = false, yes = false, undo = false, resume = false, direct = false;
        std::string resume_id;
        int steps = 25;
        std::string profile_path;
        std::string backend_name = "llama_server", endpoint = "http://127.0.0.1:8080", task;
        auto root = std::filesystem::current_path();
        for (int i = 1; i < argc; ++i) {
            std::string argument = argv[i];
            auto next = [&]() -> std::string {
                if (++i == argc)
                    throw std::runtime_error("Missing value for " + argument);
                return argv[i];
            };
            if (argument == "--help" || argument == "-h") {
                std::cout
                    << "Platypus 0.1.0\nUsage: platypus run [--headless] [--yes] [--direct] "
                       "[--project DIR] "
                       "[--endpoint URL] [--max-steps N] instruction\n"
                       "       platypus undo [--yes] [--project DIR]\nWithout an instruction, "
                       "starts an interactive session. /undo restores the latest snapshot.\n";
                return 0;
            }
            if (argument == "--version") {
                std::cout << "0.1.0\n";
                return 0;
            }
            if (argument == "run")
                continue;
            if (argument == "undo") {
                undo = true;
                continue;
            }
            if (argument == "--headless")
                headless = true;
            else if (argument == "--yes")
                yes = true;
            else if (argument == "--direct")
                direct = true;
            else if (argument == "--project")
                root = next();
            else if (argument == "--endpoint")
                endpoint = next();
            else if (argument == "--resume") {
                resume = true;
                if (i + 1 < argc &&
                    std::string(argv[i + 1]).find_first_not_of("0123456789") == std::string::npos)
                    resume_id = argv[++i];
            } else if (argument == "--profile")
                profile_path = next();
            else if (argument == "--backend")
                backend_name = next();
            else if (argument == "--max-steps")
                steps = std::stoi(next());
            else if (argument.starts_with('-'))
                throw std::runtime_error("Unknown option: " + argument);
            else {
                if (!task.empty())
                    task += ' ';
                task += argument;
            }
        }
        auto confirmer = [yes, headless](const std::string &action) {
            std::cerr << action << '\n';
            if (yes) {
                std::cerr << "Approved (--yes)\n";
                return true;
            }
            if (headless || !isatty(STDIN_FILENO))
                return false;
            std::cerr << "Approve? [y/N] ";
            std::string reply;
            std::getline(std::cin, reply);
            return reply == "y" || reply == "yes";
        };
        auto tools = std::make_shared<core::ToolRegistry>(root, confirmer);
        core::register_default_tools(*tools);
        if (undo) {
            auto restored = tools->snapshots().rollback();
            if (!restored.is_ok()) {
                std::cerr << restored.error() << '\n';
                return 1;
            }
            std::cout << "Snapshot restored\n";
            return 0;
        }
        auto backend = llm::LlmBackendFactory::create_backend(backend_name, endpoint);
        if (!backend)
            throw std::runtime_error("Unknown backend: " + backend_name);
        if (!profile_path.empty()) {
            auto profile = llm::ModelProfileManager::read_profile(profile_path);
            if (!profile.is_ok())
                throw std::runtime_error(profile.error());
            auto real = dynamic_cast<llm::LlamaServerBackend *>(backend.get());
            if (!real)
                throw std::runtime_error("Profiles require the llama_server backend");
            real->set_profile(profile.value());
        }
        core::ExecutionEngine engine(std::move(backend), tools);
        auto sessions = std::make_shared<session::Store>(tools->snapshots().root());
        auto selected =
            resume ? sessions->load(resume_id)
                   : util::Result<std::pair<std::string, session::History>>::fail("new session");
        std::string session_id;
        if (resume) {
            if (!selected.is_ok())
                throw std::runtime_error(selected.error());
            session_id = selected.value().first;
        } else {
            auto created = sessions->create(task.empty() ? "Interactive session" : task);
            if (!created.is_ok())
                throw std::runtime_error(created.error());
            session_id = created.value();
        }
        auto attached = engine.attach_session(sessions, session_id);
        if (!attached.is_ok())
            throw std::runtime_error(attached.error());
        std::cerr << "Session " << session_id << '\n';
        if (!headless && task.empty() && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))
            return tui::run_terminal(engine, *tools, profile_path.empty() ? endpoint : profile_path,
                                     steps, yes, direct);
        std::signal(SIGINT, interrupt);
        std::stop_source source;
        // C++ note: jthread joins before source is destroyed; no detached callbacks can outlive it.
        std::jthread watcher([&](std::stop_token stop) {
            while (!stop.stop_requested()) {
                if (interrupted) {
                    source.request_stop();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
        auto execute = [&](const std::string &query) {
            const bool use_direct = direct || query.starts_with("/direct ");
            const auto instruction = query.starts_with("/direct ") ? query.substr(8) : query;
            if (instruction.empty()) {
                std::cerr << "Usage: /direct <instruction>\n";
                return 2;
            }
            auto approve_plan = [&](const std::vector<std::string> &proposal)
                -> std::optional<std::vector<std::string>> {
                std::cerr << "Proposed plan:\n";
                for (std::size_t i = 0; i < proposal.size(); ++i)
                    std::cerr << "  " << i + 1 << ". " << proposal[i] << '\n';
                if (yes) {
                    std::cerr << "Approved (--yes)\n";
                    return proposal;
                }
                if (headless)
                    return std::nullopt;
                while (true) {
                    std::cerr << "Plan decision [y approve, n reject, e: step | step edit]: ";
                    std::string decision;
                    if (!std::getline(std::cin, decision))
                        return std::nullopt;
                    auto parsed = core::parse_plan_decision(decision, proposal);
                    if (parsed.is_ok())
                        return parsed.value();
                    std::cerr << parsed.error() << '\n';
                }
            };
            auto result = use_direct ? engine.run(instruction, steps, headless, source.get_token())
                                     : engine.run_planned(instruction, approve_plan, steps,
                                                          headless, source.get_token());
            if (!result.is_ok()) {
                std::cerr << result.error() << '\n';
                return 1;
            }
            std::cout << result.value() << '\n';
            return 0;
        };
        if (!task.empty())
            return execute(task);
        if (headless)
            throw std::runtime_error("Headless mode requires an instruction");
        std::cout << "Platypus — plan-first tasks; /direct <instruction> skips planning. "
                     "/plan, /milestone, /task, /done, /import, /undo, /quit\n";
        while (!source.stop_requested()) {
            std::cout << "> " << std::flush;
            if (!std::getline(std::cin, task) || task == "/quit")
                break;
            if (task == "/undo") {
                auto result = tools->snapshots().rollback();
                std::cout << (result.is_ok() ? "Snapshot restored" : result.error()) << '\n';
            } else if (!task.empty()) {
                auto plan_response = core::handle_plan_command(task, tools->snapshots());
                if (plan_response)
                    std::cout << *plan_response;
                else
                    execute(task);
            }
        }
        return source.stop_requested() ? 130 : 0;
    } catch (const std::exception &e) {
        std::cerr << "Platypus: " << e.what() << '\n';
        return 1;
    }
}
