#include "../llm/backend.hpp"
#include "../llm/chat_formatter.hpp"
#include "../llm/fake_backend.hpp"
#include "../llm/llama_server_backend.hpp"
#include "../llm/model_profile.hpp"
#include "../util/logger.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace platypus::llm;

void test_chat_formatter() {
    std::vector<std::pair<std::string, std::string>> messages = {
        {"system", "You are a helpful assistant."}, {"user", "Hello, how can I help you today?"}};

    std::string json = ChatFormatter::to_json(messages);
    std::cout << "Test ChatFormatter::to_json: " << json << std::endl;
    assert(json.find("\"role\":\"system\"") != std::string::npos);
    assert(json.find("\"role\":\"user\"") != std::string::npos);

    std::string raw =
        ChatFormatter::to_raw_string(messages, "System: You are a helpful assistant.");
    std::cout << "Test ChatFormatter::to_raw_string: " << raw << std::endl;
    assert(raw.find("System: You are a helpful assistant.") != std::string::npos);
    assert(raw.find("user: Hello, how can I help you today?") != std::string::npos);

    std::cout << "ChatFormatter tests passed!" << std::endl;
}

void test_fake_backend() {
    auto backend = std::make_unique<FakeBackend>();
    backend->set_script("Hello!", {"Mock response from FakeBackend."});
    std::vector<std::pair<std::string, std::string>> messages = {{"user", "Hello!"}};

    auto result = backend->generate_chat(messages);
    assert(result.is_ok());
    assert(!result.value().empty());
    assert(result.value()[0].content == "Mock response from FakeBackend.");

    std::cout << "FakeBackend tests passed!" << std::endl;
}

void test_llama_server_backend() {
    const char *endpoint = std::getenv("PLATYPUS_TEST_SERVER");
    assert(endpoint != nullptr);
    auto backend = std::make_unique<LlamaServerBackend>(endpoint);
    std::vector<std::pair<std::string, std::string>> messages = {{"user", "Hello!"}};

    auto result = backend->generate_chat(messages);
    assert(result.is_ok());
    assert(!result.value().empty());
    assert(result.value()[0].content == "Fixture response from HTTP server.");

    auto health = backend->health();
    assert(health.is_ok());
    std::string streamed;
    auto stream = backend->stream_chat(messages, 0.3f, 128,
                                       [&](const ChatChunk &chunk) { streamed += chunk.content; });
    assert(stream.is_ok());
    assert(streamed == "streamed response");
    CompletionRequest request;
    request.messages = {{"user", "Use tools"}};
    request.tools =
        nlohmann::json::array({{{"type", "function"}, {"function", {{"name", "read_file"}}}}});
    std::string streamed_payload;
    auto streamed_tools = backend->complete(
        request, [&](const std::string &chunk) { streamed_payload += chunk; }, {});
    assert(streamed_tools.is_ok());
    auto calls = nlohmann::json::parse(streamed_tools.value().content).at("tool_calls");
    assert(calls.size() == 2);
    assert(calls.at(0).at("function").at("name") == "read_file");
    assert(calls.at(1).at("function").at("name") == "list_files");
    assert(streamed_payload.find("Explanation") != std::string::npos);
    request.messages = {{"user", "Incomplete tools"}};
    auto incomplete = backend->complete(request, [](const std::string &) {}, {});
    assert(!incomplete.is_ok());
    auto tokens = backend->tokenize("test");
    assert(tokens.is_ok() && tokens.value().size() == 3);
    ModelProfile profile;
    profile.name = "fixture";
    profile.sampling = {{"top_k", 17}, {"top_p", 0.91}, {"seed", 42}};
    backend->set_profile(profile);
    auto sampling = backend->generate_chat({{"user", "Sampling check"}}, 0.3f, 128);
    assert(sampling.is_ok());
    auto sent_sampling = nlohmann::json::parse(sampling.value()[0].content);
    assert(sent_sampling.at("top_k") == 17);
    assert(sent_sampling.at("top_p") == 0.91);
    assert(sent_sampling.at("seed") == 42);
    std::string streamed_sampling;
    auto sampling_stream =
        backend->stream_chat({{"user", "Sampling stream"}}, 0.3f, 128,
                             [&](const ChatChunk &chunk) { streamed_sampling += chunk.content; });
    assert(sampling_stream.is_ok());
    sent_sampling = nlohmann::json::parse(streamed_sampling);
    assert(sent_sampling.at("top_k") == 17 && sent_sampling.at("seed") == 42);
    profile.turn_prefix = "<turn>{role}\n";
    profile.turn_suffix = "</turn>\n";
    backend->set_profile(profile);
    auto raw = backend->generate_chat({{"user", "Sampling raw"}});
    assert(raw.is_ok());
    auto raw_sampling = nlohmann::json::parse(raw.value()[0].content);
    assert(raw_sampling.at("top_k") == 17 && raw_sampling.at("seed") == 42);

    setenv("PLATYPUS_DEBUG_PREFIX", "1", 1);
    std::ostringstream prefix_log;
    auto *previous_error_buffer = std::cerr.rdbuf(prefix_log.rdbuf());
    LlamaServerBackend diagnostic_backend(endpoint);
    diagnostic_backend.set_profile(profile);
    auto first = diagnostic_backend.generate_chat({{"system", "stable instructions"}});
    auto changed = diagnostic_backend.generate_chat({{"system", "changed instructions"}});
    std::cerr.rdbuf(previous_error_buffer);
    unsetenv("PLATYPUS_DEBUG_PREFIX");
    assert(first.is_ok() && changed.is_ok());
    assert(prefix_log.str().find("Stable system prompt prefix changed") != std::string::npos);
    std::cout << "LlamaServerBackend tests passed!" << std::endl;
}

int main() {
    try {
        test_chat_formatter();
        test_fake_backend();
        test_llama_server_backend();
        std::cout << "All LLM Layer tests passed!" << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
