#include "llm_backend_factory.hpp"

namespace platypus::llm {

std::unique_ptr<LlmBackend> LlmBackendFactory::create_backend(const std::string &type,
                                                              const std::string &base_url) {
    if (type == "fake") {
        return std::make_unique<FakeBackend>();
    } else if (type == "llama_server") {
        return std::make_unique<LlamaServerBackend>(base_url);
    }

    return nullptr;
}

} // namespace platypus::llm
