// Real HTTP transport for the llama-server OpenAI-compatible endpoint.
// CURL owns connections; stop_token cancellation interrupts transfers via the progress callback.
#include "llama_server_backend.hpp"
#include "chat_formatter.hpp"
#include <cstdlib>
#include <curl/curl.h>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace platypus::llm {
namespace {
struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};
util::Result<std::string> request(const std::string &url, const std::string *payload,
                                  std::stop_token stop,
                                  const std::function<void(std::string_view)> &callback = {},
                                  const std::string &metadata = {}) {
    static CurlGlobal initialized;
    (void)initialized;
    using Return = util::Result<std::string>;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl)
        return Return::fail("Cannot initialize HTTP client");
    struct State {
        std::string body;
        std::stop_token stop;
        std::function<void(std::string_view)> callback;
        bool failed = false;
    } state{{}, stop, callback};
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(
        curl.get(), CURLOPT_XFERINFOFUNCTION,
        +[](void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
            return static_cast<State *>(userdata)->stop.stop_requested() ? 1 : 0;
        });
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(
        curl.get(), CURLOPT_WRITEFUNCTION,
        +[](char *data, size_t size, size_t count, void *userdata) -> size_t {
            auto &current = *static_cast<State *>(userdata);
            auto bytes = size * count;
            try {
                if (current.body.size() + bytes > 16 * 1024 * 1024)
                    return 0;
                current.body.append(data, bytes);
                if (current.callback)
                    current.callback(std::string_view(data, bytes));
                return bytes;
            } catch (...) {
                current.failed = true;
                return 0;
            }
        });
    curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/json");
    if (!metadata.empty()) {
        auto *updated = curl_slist_append(headers, ("X-Platypus-Benchmark: " + metadata).c_str());
        if (!updated) {
            curl_slist_free_all(headers);
            return Return::fail("Cannot allocate benchmark metadata header");
        }
        headers = updated;
    }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_owner(headers,
                                                                             curl_slist_free_all);
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
    if (payload)
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, payload->c_str());
    auto error = curl_easy_perform(curl.get());
    if (error != CURLE_OK)
        return Return::fail(stop.stop_requested() ? "Cancelled" : curl_easy_strerror(error));
    if (state.failed)
        return Return::fail("Streaming callback failed");
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300)
        return Return::fail("HTTP " + std::to_string(status) + ": " + state.body.substr(0, 2000));
    return Return::ok(state.body);
}
nlohmann::json messages_json(const std::vector<std::pair<std::string, std::string>> &messages) {
    return nlohmann::json::parse(ChatFormatter::to_json(messages));
}
nlohmann::json sampling_request(nlohmann::json body, const ModelProfile *profile) {
    if (profile)
        for (const auto &[name, value] : profile->sampling.items())
            body[name] = value;
    return body;
}
} // namespace
LlamaServerBackend::LlamaServerBackend(std::string base_url) : base_url_(std::move(base_url)) {
    while (!base_url_.empty() && base_url_.back() == '/')
        base_url_.pop_back();
    auto *trace = std::getenv("PLATYPUS_BENCHMARK_TRACE");
    benchmark_metadata_enabled_ = trace && std::string_view(trace) == "1";
    auto *diagnostic = std::getenv("PLATYPUS_DEBUG_PREFIX");
    prefix_diagnostic_enabled_ = diagnostic && std::string_view(diagnostic) == "1";
}
void LlamaServerBackend::diagnose_prefix(
    const std::vector<std::pair<std::string, std::string>> &messages) {
    if (!prefix_diagnostic_enabled_ || !profile_)
        return;
    auto current = ChatFormatter::stable_prefix(messages, *profile_);
    if (current.empty())
        return;
    if (!previous_stable_prefix_.empty() && current != previous_stable_prefix_)
        util::Logger::get_instance().warn(
            "Stable system prompt prefix changed between generations");
    previous_stable_prefix_ = std::move(current);
}
util::Result<CompletionResult> LlamaServerBackend::complete(const CompletionRequest &completion,
                                                            TokenCallback on_token,
                                                            std::stop_token stop) try {
    benchmark_metadata_.clear();
    if (benchmark_metadata_enabled_) {
        nlohmann::json observations = nlohmann::json::array();
        for (std::size_t i = 0; i < completion.messages.size(); ++i) {
            const auto &[role, text] = completion.messages[i];
            if (role == "tool") {
                auto result = nlohmann::json::parse(text);
                observations.push_back(
                    {{"id", i}, {"name", result.at("name")}, {"is_error", result.at("is_error")}});
            }
        }
        nlohmann::json reasoning = nlohmann::json::object();
        if (profile_) {
            auto suffix = profile_->generation_suffix;
            auto last = suffix.find_last_not_of(" \t\r\n");
            if (last != std::string::npos)
                suffix.resize(last + 1);
            reasoning = {{"open", profile_->reasoning_open},
                         {"close", profile_->reasoning_close},
                         {"implicit", !profile_->reasoning_open.empty() &&
                                          suffix.ends_with(profile_->reasoning_open)}};
        }
        benchmark_metadata_ = nlohmann::json{
            {"version", 1},
            {"syntax", profile_ ? profile_->tool_syntax : "json_tagged"},
            {"tools", completion.tools},
            {"reasoning", reasoning},
            {"observations",
             observations}}.dump(-1, ' ', true);
        if (benchmark_metadata_.size() > 48 * 1024)
            return util::Result<CompletionResult>::fail("Benchmark metadata exceeds header limit");
    }
    bool emitted_tokens = false;
    auto complete_once = [&] {
        TokenCallback tracked_callback;
        if (on_token) {
            tracked_callback = [&](const std::string &chunk) {
                emitted_tokens = emitted_tokens || !chunk.empty();
                on_token(chunk);
            };
        }
        return LlmBackend::complete(completion, std::move(tracked_callback), stop);
    };
    auto result = complete_once();
    if (!result.is_ok() && !emitted_tokens && runtime_oom_recovery_) {
        auto recovery = runtime_oom_recovery_(result.error());
        if (!recovery.is_ok()) {
            result = util::Result<CompletionResult>::fail("Inference OOM recovery failed: " +
                                                          recovery.error());
        } else if (recovery.value()) {
            result = complete_once();
        }
    }
    benchmark_metadata_.clear();
    return result;
} catch (const std::exception &error) {
    benchmark_metadata_.clear();
    return util::Result<CompletionResult>::fail(error.what());
}
util::Result<std::string> LlamaServerBackend::post_request(const std::string &endpoint,
                                                           const std::string &payload) {
    return request(base_url_ + endpoint, &payload, stop_, {},
                   endpoint == "/tokenize" ? "" : benchmark_metadata_);
}
util::Result<std::string> LlamaServerBackend::health() {
    return request(base_url_ + "/health", nullptr, stop_);
}
util::Result<PromptSize> LlamaServerBackend::count_prompt_tokens(
    const std::vector<std::pair<std::string, std::string>> &messages, std::stop_token stop) try {
    if (!profile_ || profile_->turn_prefix.empty())
        return LlmBackend::count_prompt_tokens(messages, stop);
    set_stop_token(stop);
    if (stop.stop_requested())
        return util::Result<PromptSize>::fail("Cancelled");
    auto response = post_request(
        "/tokenize", nlohmann::json{{"content", ChatFormatter::render(messages, *profile_)},
                                    {"add_special", true},
                                    {"parse_special", true}}
                         .dump());
    if (!response.is_ok())
        return util::Result<PromptSize>::fail("Prompt tokenization failed: " + response.error());
    auto tokens = nlohmann::json::parse(response.value()).at("tokens").get<std::vector<int>>();
    return util::Result<PromptSize>::ok({tokens.size(), true});
} catch (const std::exception &error) {
    return util::Result<PromptSize>::fail("Prompt tokenization failed: " +
                                          std::string(error.what()));
}
util::Result<std::vector<int>> LlamaServerBackend::tokenize(const std::string &content) {
    auto response = post_request(
        "/tokenize",
        nlohmann::json{{"content", content}, {"add_special", false}, {"parse_special", true}}
            .dump());
    if (!response.is_ok())
        return util::Result<std::vector<int>>::fail(response.error());
    try {
        return util::Result<std::vector<int>>::ok(
            nlohmann::json::parse(response.value()).at("tokens").get<std::vector<int>>());
    } catch (const std::exception &e) {
        return util::Result<std::vector<int>>::fail(e.what());
    }
}
util::Result<std::vector<ChatChunk>>
LlamaServerBackend::generate_chat(const std::vector<std::pair<std::string, std::string>> &messages,
                                  float temperature, int max_tokens) {
    using Return = util::Result<std::vector<ChatChunk>>;
    try {
        auto body = sampling_request(nlohmann::json{{"messages", messages_json(messages)},
                                                    {"temperature", temperature},
                                                    {"max_tokens", max_tokens},
                                                    {"stream", false}},
                                     profile_ ? &*profile_ : nullptr)
                        .dump();
        bool raw = profile_ && !profile_->turn_prefix.empty();
        if (raw)
            diagnose_prefix(messages);
        if (raw)
            body = sampling_request(
                       nlohmann::json{{"prompt", ChatFormatter::render(messages, *profile_)},
                                      {"temperature", profile_->temperature},
                                      {"n_predict", max_tokens},
                                      {"cache_prompt", true},
                                      {"stop", profile_->stop_strings},
                                      {"stream", false}},
                       &*profile_)
                       .dump();
        auto response = post_request(raw ? "/completion" : "/v1/chat/completions", body);
        if (!response.is_ok())
            return Return::fail(response.error());
        auto parsed = nlohmann::json::parse(response.value());
        if (raw)
            return Return::ok({{parsed.at("content").get<std::string>(), true, "assistant"}});
        auto message = parsed.at("choices").at(0).at("message");
        auto content = message.value("content", nlohmann::json(nullptr));
        if (message.contains("tool_calls"))
            content = nlohmann::json{{"tool_calls", message.at("tool_calls")}}.dump();
        if (!content.is_string())
            return Return::fail("Missing completion content");
        return Return::ok({{content.get<std::string>(), true, "assistant"}});
    } catch (const std::exception &e) {
        return Return::fail(std::string("Invalid server response: ") + e.what());
    }
}
util::Result<util::Void>
LlamaServerBackend::stream_request(const std::string &endpoint, const std::string &body,
                                   std::function<void(const std::string &)> callback) {
    std::string pending;
    auto result = request(
        base_url_ + endpoint, &body, stop_,
        [&](std::string_view chunk) {
            pending.append(chunk);
            std::size_t newline;
            while ((newline = pending.find('\n')) != std::string::npos) {
                auto line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.starts_with("data:")) {
                    auto data = line.substr(5);
                    if (!data.empty() && data.front() == ' ')
                        data.erase(0, 1);
                    if (data != "[DONE]")
                        callback(data);
                }
            }
        },
        benchmark_metadata_);
    return result.is_ok() ? util::Result<util::Void>::ok()
                          : util::Result<util::Void>::fail(result.error());
}
util::Result<util::Void>
LlamaServerBackend::stream_chat(const std::vector<std::pair<std::string, std::string>> &messages,
                                float temperature, int max_tokens,
                                std::function<void(const ChatChunk &)> callback) try {
    auto body = sampling_request(nlohmann::json{{"messages", messages_json(messages)},
                                                {"temperature", temperature},
                                                {"max_tokens", max_tokens},
                                                {"stream", true}},
                                 profile_ ? &*profile_ : nullptr)
                    .dump();
    bool raw = profile_ && !profile_->turn_prefix.empty();
    if (raw)
        diagnose_prefix(messages);
    if (raw)
        body =
            sampling_request(nlohmann::json{{"prompt", ChatFormatter::render(messages, *profile_)},
                                            {"temperature", profile_->temperature},
                                            {"n_predict", max_tokens},
                                            {"cache_prompt", true},
                                            {"stop", profile_->stop_strings},
                                            {"stream", true}},
                             &*profile_)
                .dump();
    std::map<int, nlohmann::json> tool_calls;
    bool finished_with_tool_calls = false;
    auto result = stream_request(
        raw ? "/completion" : "/v1/chat/completions", body, [&](const std::string &chunk) {
            auto parsed = nlohmann::json::parse(chunk);
            if (raw) {
                callback({parsed.value("content", std::string{}), parsed.value("stop", false),
                          "assistant"});
                return;
            }
            if (!parsed.contains("choices") || parsed.at("choices").empty())
                return;
            auto choice = parsed.at("choices").at(0);
            auto delta = choice.value("delta", nlohmann::json::object());
            if (!delta.is_object())
                throw std::runtime_error("Invalid streaming delta");
            if (delta.contains("tool_calls")) {
                if (!delta.at("tool_calls").is_array())
                    throw std::runtime_error("Invalid streaming tool calls");
                for (const auto &call : delta.at("tool_calls")) {
                    auto index = call.at("index").get<int>();
                    if (index < 0 || index >= 64)
                        throw std::runtime_error("Streaming tool call index out of range");
                    auto &target =
                        tool_calls
                            .try_emplace(index, nlohmann::json{{"type", "function"},
                                                               {"function",
                                                                {{"name", ""}, {"arguments", ""}}}})
                            .first->second;
                    if (call.contains("id"))
                        target["id"] = call.at("id");
                    if (call.contains("type") && call.at("type") != "function")
                        throw std::runtime_error("Unsupported streaming tool call type");
                    if (call.contains("function")) {
                        const auto &function = call.at("function");
                        if (function.contains("name"))
                            target["function"]["name"] =
                                target["function"]["name"].get<std::string>() +
                                function.at("name").get<std::string>();
                        if (function.contains("arguments"))
                            target["function"]["arguments"] =
                                target["function"]["arguments"].get<std::string>() +
                                function.at("arguments").get<std::string>();
                    }
                }
            }
            auto text = delta.value("content", std::string{});
            if (!text.empty())
                callback({text, false, "assistant"});
            auto finish = choice.value("finish_reason", nlohmann::json(nullptr));
            if (!finish.is_null()) {
                finished_with_tool_calls = finish == "tool_calls";
                callback({"", true, "assistant"});
            }
        });
    if (!result.is_ok())
        return result;
    if (!tool_calls.empty()) {
        if (!finished_with_tool_calls)
            return util::Result<util::Void>::fail("Incomplete streamed tool call batch");
        nlohmann::json calls = nlohmann::json::array();
        for (auto &[index, call] : tool_calls) {
            (void)index;
            if (call["function"]["name"].get<std::string>().empty())
                return util::Result<util::Void>::fail("Streamed tool call has no function name");
            calls.push_back(std::move(call));
        }
        callback(
            {nlohmann::json{{"tool_calls", std::move(calls)}}.dump(), true, "assistant", true});
    }
    return result;
} catch (const std::exception &e) {
    return util::Result<util::Void>::fail(e.what());
}
} // namespace platypus::llm
