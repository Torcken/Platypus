#ifndef PLATYPUS_CORE_PLAN_COMMANDS_HPP
#define PLATYPUS_CORE_PLAN_COMMANDS_HPP

#include "../safety/snapshot_store.hpp"
#include <optional>
#include <string>

namespace platypus::core {

/// Handle an interactive plan command, returning no value for ordinary model tasks.
std::optional<std::string> handle_plan_command(const std::string &input,
                                               safety::SnapshotStore &workspace);

} // namespace platypus::core

#endif // PLATYPUS_CORE_PLAN_COMMANDS_HPP
