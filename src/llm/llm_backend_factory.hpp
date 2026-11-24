#ifndef PLATYPUS_LLM_LLM_BACKEND_FACTORY_HPP
#define PLATYPUS_LLM_LLM_BACKEND_FACTORY_HPP

#include "backend.hpp"
#include "fake_backend.hpp"
#include "llama_server_backend.hpp"
#include <memory>
#include <string>

namespace platypus::llm {

/**
 * @brief Factory class for creating different types of LlmBackend.
 */
class LlmBackendFactory {
  public:
    /**
     * @brief Creates a backend based on the specified type.
     * @param type "fake" or "llama_server".
     * @param base_url The base URL for the llama.cpp server (ignored for "fake").
     * @return A unique_ptr to the created LlmBackend.
     */
    static std::unique_ptr<LlmBackend>
    create_backend(const std::string &type, const std::string &base_url = "http://localhost:8080");
};

} // namespace platypus::llm

#endif // PLATYPUS_LLM_LLM_BACKEND_FACTORY_HPP
