// Default prompt sizing for backends without access to the model tokenizer.
// ContextManager consumes this estimate; native llama-server profiles override it.
#include "backend.hpp"
#include "chat_formatter.hpp"
#include "util/token_estimate.hpp"

namespace platypus::llm {
util::Result<PromptSize>
LlmBackend::count_prompt_tokens(const std::vector<std::pair<std::string, std::string>> &messages,
                                std::stop_token stop) try {
    if (stop.stop_requested())
        return util::Result<PromptSize>::fail("Cancelled");
    auto serialized = ChatFormatter::to_json(messages);
    return util::Result<PromptSize>::ok(
        {util::estimate_prompt_tokens(serialized, messages.size()), false});
} catch (const std::exception &error) {
    return util::Result<PromptSize>::fail(error.what());
}
} // namespace platypus::llm
