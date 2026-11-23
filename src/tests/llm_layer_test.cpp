#include "../llm/backend.hpp"
#include "../llm/llama_server_backend.hpp"
#include "../llm/fake_backend.hpp"
#include "../llm/chat_formatter.hpp"
#include "../llm/model_profile.hpp"
#include "../util/logger.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

using namespace platypus::llm;

void test_chat_formatter() {
    std::vector<std::pair<std::string, std::string>> messages = {
        {"system", "You are a helpful assistant."},
        {"user", "Hello, how can I help you today?"}
    };

    std::string json = ChatFormatter::to_json(messages);
    std::cout << "Test ChatFormatter::to_json: " << json << std::endl;
    assert(json.find("\"role\":\"system\"") != std::string::npos);
    assert(json.find("\"role\":\"user\"") != std::string::npos);

    std::string raw = ChatFormatter::to_raw_string(messages, "System: You are a helpful assistant.");
    std::cout << "Test ChatFormatter::to_raw_string: " << raw << std::endl;
    assert(raw.find("System: You are a helpful assistant.") != std::string::npos);
    assert(raw.find("user: Hello, how can I help you today?") != std::string::npos);

    std::cout << "ChatFormatter tests passed!" << std::endl;
}

void test_fake_backend() {
    auto backend = std::make_unique<FakeBackend>();
    std::vector<std::pair<std::string, std::string>> messages = {
        {"user", "Hello!"}
    };

    auto result = backend->generate_chat(messages);
    assert(result.is_ok());
    assert(!result.value().empty());
    assert(result.value()[0].content == "Mock response from FakeBackend.");

    std::cout << "FakeBackend tests passed!" << std::endl;
}

void test_llama_server_backend() {
    auto backend = std::make_unique<LlamaServerBackend>("http://localhost:8080");
    std::vector<std::pair<std::string, std::string>> messages = {
        {"user", "Hello!"}
    };

    auto result = backend->generate_chat(messages);
    assert(result.is_ok());
    assert(!result.value().empty());
    assert(result.value()[0].content == "Mock response from llama.cpp server.");

    std::cout << "LlamaServerBackend tests passed!" << std::endl;
}

int main() {
    try {
        test_chat_formatter();
        test_fake_backend();
        test_llama_server_backend();
        std::cout << "All LLM Layer tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
