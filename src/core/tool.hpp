// Compatibility names for callers of the original core tool registry.
// New tool implementations use tools/registry.hpp and tools/tool.hpp.
#ifndef PLATYPUS_CORE_TOOL_HPP
#define PLATYPUS_CORE_TOOL_HPP
#include "tools/registry.hpp"
namespace platypus::core {
using Tool = tools::Tool;
using ToolRegistry = tools::ToolRegistry;
using tools::register_default_tools;
using tools::register_filesystem_tools;
using tools::register_search_tools;
using tools::register_shell_tools;
} // namespace platypus::core
#endif
