// Byte-based fallback sizing for backends without a prompt tokenizer.
// The estimate is deliberately conservative and must be labelled as estimated in the UI.
// llm/backend.cpp uses this for generic server-side chat templates.
#ifndef PLATYPUS_UTIL_TOKEN_ESTIMATE_HPP
#define PLATYPUS_UTIL_TOKEN_ESTIMATE_HPP

#include <cstddef>
#include <string_view>

namespace platypus::util {
/// Budget one token per serialized byte, plus a fixed allowance for template wrappers.
/// This is an estimate, not a tokenizer; hidden server templates cannot be counted locally.
inline std::size_t estimate_prompt_tokens(std::string_view serialized, std::size_t turns) {
    return serialized.size() + turns * 32 + 256;
}
} // namespace platypus::util
#endif
