#ifndef PLATYPUS_CORE_PLAN_COMMANDS_HPP
#define PLATYPUS_CORE_PLAN_COMMANDS_HPP

#include "../safety/snapshot_store.hpp"
#include <optional>
#include <string>
#include <vector>

namespace platypus::core {

/// Handle an interactive plan command, returning no value for ordinary model tasks.
std::optional<std::string> handle_plan_command(const std::string &input,
                                               safety::SnapshotStore &workspace);

/// Parse y/n or `e: step one | step two` for an explicitly displayed plan proposal.
util::Result<std::optional<std::vector<std::string>>>
parse_plan_decision(const std::string &input, const std::vector<std::string> &proposal);

} // namespace platypus::core

#endif // PLATYPUS_CORE_PLAN_COMMANDS_HPP
