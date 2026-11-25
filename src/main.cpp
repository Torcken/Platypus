// Command-line entry point wires the real backend, safe tools and sequential agent.
// Mutating headless runs require --yes; interactive runs approve each diff/command.
#include "core/execution_engine.hpp"
#include "core/plan_commands.hpp"
#include "llm/llm_backend_factory.hpp"
#include "platform/hardware.hpp"
#include "platform/model_catalog.hpp"
#include "platform/model_download.hpp"
#include "tui/terminal.hpp"
#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <unistd.h>
namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }

std::filesystem::path model_catalog_path() {
    if (const auto *configured = std::getenv("PLATYPUS_MODEL_CATALOG"))
        return configured;
    const std::filesystem::path source_path(PLATYPUS_SOURCE_CATALOG_PATH);
    if (std::filesystem::is_regular_file(source_path))
        return source_path;
    return PLATYPUS_INSTALL_CATALOG_PATH;
}

platypus::util::Result<platypus::platform::ModelCatalog>
load_catalog(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return platypus::util::Result<platypus::platform::ModelCatalog>::fail(
            "Cannot read model catalog: " + path.string());
    std::string text(256 * 1024 + 1, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    return platypus::platform::parse_model_catalog(text);
}

std::string model_size(std::uint64_t bytes) {
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    std::ostringstream value;
    value << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / gib;
    return value.str() + " GiB";
}

int models_command(int argc, char *argv[]) {
    using namespace platypus::platform;
    if (argc < 3) {
        std::cerr << "Usage: platypus models <list|recommend|download> [options]\n";
        return 2;
    }
    const std::string command = argv[2];
    auto catalog_path = model_catalog_path();
    std::string model_id;
    std::string preference = "balanced";
    std::filesystem::path output_path;
    for (int i = 3; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--catalog" || argument == "--preference" || argument == "--output") {
            if (++i >= argc) {
                std::cerr << "Missing value for " << argument << '\n';
                return 2;
            }
            if (argument == "--catalog")
                catalog_path = argv[i];
            else if (argument == "--preference")
                preference = argv[i];
            else
                output_path = argv[i];
        } else if (!argument.starts_with('-') && model_id.empty()) {
            model_id = argument;
        } else {
            std::cerr << "Unknown models option: " << argument << '\n';
            return 2;
        }
    }
    auto catalog = load_catalog(catalog_path);
    if (!catalog.is_ok()) {
        std::cerr << catalog.error() << '\n';
        return 1;
    }
    if (command == "list") {
        for (const auto &model : catalog.value().models)
            std::cout << model.id << "\t" << model.display_name << " (" << model.quantization
                      << ")\t" << model_size(model.size_hint_bytes) << '\n';
        return 0;
    }
    if (command == "recommend") {
        const auto selected_preference = preference == "fast"       ? ModelPreference::fast
                                         : preference == "balanced" ? ModelPreference::balanced
                                         : preference == "quality"  ? ModelPreference::quality
                                                                    : ModelPreference::balanced;
        if (preference != "fast" && preference != "balanced" && preference != "quality") {
            std::cerr << "Preference must be fast, balanced, or quality\n";
            return 2;
        }
        auto hardware = detect_hardware();
        if (!hardware.is_ok()) {
            std::cerr << hardware.error() << '\n';
            return 1;
        }
        auto recommendation =
            recommend_model(catalog.value(), hardware.value().catalog_facts(), selected_preference);
        if (!recommendation.is_ok()) {
            std::cerr << recommendation.error() << '\n';
            return 1;
        }
        const auto &model = recommendation.value();
        std::cout << model.id << "\t" << model.display_name << " (" << model.quantization << ")\t"
                  << model_size(model.size_hint_bytes) << '\n';
        return 0;
    }
    if (command != "download" || model_id.empty()) {
        std::cerr << "Usage: platypus models download MODEL_ID [--output FILE]\n";
        return 2;
    }
    const auto model =
        std::find_if(catalog.value().models.begin(), catalog.value().models.end(),
                     [&](const auto &candidate) { return candidate.id == model_id; });
    if (model == catalog.value().models.end()) {
        std::cerr << "Unknown model id: " << model_id << '\n';
        return 1;
    }
    if (output_path.empty()) {
        const auto *home = std::getenv("HOME");
        const auto safe_id = std::all_of(model_id.begin(), model_id.end(), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-';
        });
        if (!home || !safe_id) {
            std::cerr << "Set --output or HOME; model id is not a safe filename\n";
            return 2;
        }
        output_path =
            std::filesystem::path(home) / ".local/share/platypus/models" / (model_id + ".gguf");
    }
    int previous_percent = -1;
    auto downloaded =
        download_model(*model, output_path, [&](std::uint64_t received, std::uint64_t total) {
            const auto percent = total == 0 ? 0 : static_cast<int>(received * 100 / total);
            if (percent != previous_percent) {
                std::cerr << "\rDownloading " << model_id << ": " << percent << "%" << std::flush;
                previous_percent = percent;
            }
        });
    std::cerr << '\n';
    if (!downloaded.is_ok()) {
        std::cerr << downloaded.error() << '\n';
        return 1;
    }
    std::cout << "Verified model saved to " << downloaded.value().string() << '\n';
    return 0;
}
} // namespace
int main(int argc, char *argv[]) {
    using namespace platypus;
    try {
        if (argc > 1 && std::string_view(argv[1]) == "models")
            return models_command(argc, argv);
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
                       "starts an interactive session. /undo restores the latest snapshot.\n"
                       "       platypus models list|recommend|download MODEL_ID\n";
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
        auto remote_config = llm::load_remote_provider_config(tools->snapshots().root());
        if (!remote_config.is_ok())
            throw std::runtime_error(remote_config.error());
        if (remote_config.value())
            engine.set_remote_provider(
                std::make_shared<llm::RemoteProvider>(*remote_config.value()));
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
                     "/plan, /milestone, /task, /done, /decision, /review, /review --copy, "
                     "/review --remote, "
                     "/import, /review-import, /undo, /quit\n";
        while (!source.stop_requested()) {
            std::cout << "> " << std::flush;
            if (!std::getline(std::cin, task) || task == "/quit")
                break;
            if (task == "/undo") {
                auto result = tools->snapshots().rollback();
                std::cout << (result.is_ok() ? "Snapshot restored" : result.error()) << '\n';
            } else if (!task.empty()) {
                auto plan_response =
                    core::handle_plan_command(task, tools->snapshots(), [&](const auto &bundle) {
                        return engine.review_remotely(bundle, source.get_token());
                    });
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
