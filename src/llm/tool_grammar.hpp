#ifndef PLATYPUS_LLM_TOOL_GRAMMAR_HPP
#define PLATYPUS_LLM_TOOL_GRAMMAR_HPP

#include "../util/result.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace platypus::llm {

/// Build a GBNF grammar for one native call using Platypus's flat primitive tool schemas.
class ToolGrammar {
  public:
    static util::Result<std::string> generate(const nlohmann::json &tools,
                                              const std::string &syntax);
};

} // namespace platypus::llm

#endif // PLATYPUS_LLM_TOOL_GRAMMAR_HPP
