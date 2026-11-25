// FTXUI terminal entry point, sharing the headless engine and safety contract.
// Generation runs on one joined worker; UI events approve or cancel actions.
#pragma once
#include "core/execution_engine.hpp"
namespace platypus::tui {
/// Run the full-screen terminal UI until quit; all workers are joined before returning.
int run_terminal(core::ExecutionEngine &engine, core::ToolRegistry &tools, const std::string &model,
                 int steps, bool auto_approve, bool direct = false);
} // namespace platypus::tui
