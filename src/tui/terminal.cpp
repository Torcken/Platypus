// Thread-safe terminal history, input, approval prompts and cooperative cancellation.
// Only the worker touches the engine. FTXUI PostEvent wakes the UI after state changes.
#include "terminal.hpp"
#include <algorithm>
#include <condition_variable>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>
namespace platypus::tui {
int run_terminal(core::ExecutionEngine &engine, core::ToolRegistry &tools, const std::string &model,
                 int steps, bool auto_approve) {
    using namespace ftxui;
    auto screen = ScreenInteractive::Fullscreen();
    std::mutex mutex;
    std::condition_variable_any answered;
    std::string history = "Platypus — enter a task, /undo, or /quit\n";
    std::string input;
    bool busy = false, approving = false;
    int scroll = 0;
    std::optional<bool> decision;
    std::stop_source task_stop;
    std::jthread worker;
    auto append = [&](const std::string &text) {
        {
            std::lock_guard lock(mutex);
            history += text;
            scroll = 0;
        }
        screen.PostEvent(Event::Custom);
    };
    tools.snapshots().set_confirmer([&](const std::string &action) {
        std::unique_lock lock(mutex);
        history += "\n" + action + "\n";
        scroll = 0;
        if (auto_approve) {
            history += "Approved (--yes)\n";
            return true;
        }
        approving = true;
        decision.reset();
        screen.PostEvent(Event::Custom);
        answered.wait(lock, task_stop.get_token(), [&] { return decision.has_value(); });
        bool accepted = decision.value_or(false) && !task_stop.stop_requested();
        approving = false;
        history += accepted ? "Approved\n" : "Denied\n";
        return accepted;
    });
    engine.set_token_callback([&](const std::string &token) { append(token); });
    auto editor = Input(&input, "Describe a task…");
    auto renderer = Renderer(editor, [&] {
        std::lock_guard lock(mutex);
        std::istringstream stream(history);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(stream, line))
            lines.push_back(line);
        Elements elements;
        int focused = std::max(0, static_cast<int>(lines.size()) - 1 - scroll);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            auto element = text(lines[i]);
            if (static_cast<int>(i) == focused)
                element = element | focus;
            elements.push_back(element);
        }
        return vbox({text("Platypus | " + model + (busy ? " | Working" : " | Ready")) | bold,
                     separator(), vbox(elements) | vscroll_indicator | frame | flex, separator(),
                     approving ? text("Approve this action? Y = approve, N = deny") | bold
                               : editor->Render(),
                     text("Enter: send  Esc: cancel/exit  PgUp/PgDn: history  /undo: restore") |
                         dim}) |
               border;
    });
    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::PageUp || event == Event::PageDown) {
            std::lock_guard lock(mutex);
            scroll = std::max(0, scroll + (event == Event::PageUp ? 10 : -10));
            return true;
        }
        {
            std::lock_guard lock(mutex);
            if (approving && (event == Event::Character('y') || event == Event::Character('Y') ||
                              event == Event::Character('n') || event == Event::Character('N'))) {
                decision = event == Event::Character('y') || event == Event::Character('Y');
                answered.notify_all();
                return true;
            }
            if (event == Event::Escape || event == Event::CtrlC) {
                if (busy) {
                    task_stop.request_stop();
                    answered.notify_all();
                } else
                    screen.ExitLoopClosure()();
                return true;
            }
            if (event != Event::Return)
                return false;
            if (busy || input.empty())
                return true;
        }
        auto query = std::exchange(input, "");
        if (query == "/quit") {
            screen.ExitLoopClosure()();
            return true;
        }
        if (worker.joinable())
            worker.join();
        task_stop = std::stop_source{};
        {
            std::lock_guard lock(mutex);
            busy = true;
        }
        append("\n> " + query + "\n");
        worker = std::jthread([&, query](std::stop_token) {
            if (query == "/undo") {
                tools.snapshots().set_stop_token(task_stop.get_token());
                auto result = tools.snapshots().rollback();
                append(result.is_ok() ? "Snapshot restored\n" : result.error() + "\n");
            } else {
                auto result = engine.run(query, steps, false, task_stop.get_token());
                append(result.is_ok() ? "\n" : "\nError: " + result.error() + "\n");
            }
            {
                std::lock_guard lock(mutex);
                busy = false;
            }
            screen.PostEvent(Event::Custom);
        });
        return true;
    });
    struct Cleanup {
        std::function<void()> action;
        ~Cleanup() { action(); }
    } cleanup{[&] {
        task_stop.request_stop();
        answered.notify_all();
        if (worker.joinable())
            worker.join();
        engine.set_token_callback({});
        tools.snapshots().set_confirmer({});
    }};
    screen.Loop(component);
    return 0;
}
} // namespace platypus::tui
