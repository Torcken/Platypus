// Compatibility include for pre-migration callers and the original regression suite.
// New agent code uses agent/context_manager.hpp and platypus::agent::ContextManager.
#ifndef PLATYPUS_CORE_CONTEXT_MANAGER_HPP
#define PLATYPUS_CORE_CONTEXT_MANAGER_HPP
#include "agent/context_manager.hpp"
namespace platypus::core {
using ContextManager = agent::ContextManager;
}
#endif
