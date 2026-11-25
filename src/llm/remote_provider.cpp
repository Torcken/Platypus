// Implements cost-gated OpenAI-compatible and Anthropic Messages text requests.
// This service is deliberately separate from the tool-execution backend.
#include "remote_provider.hpp"

#include <cmath>
#include <cstdlib>
#include <curl/curl.h>
#include <iomanip>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <toml++/toml.hpp>

namespace platypus::llm {
namespace {
struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

util::Result<std::string> send_http(const std::string &url, const std::vector<std::string> &headers,
                                    const std::string &body, std::stop_token stop) {
    using Return = util::Result<std::string>;
    static CurlGlobal curl_global;
    (void)curl_global;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl)
        return Return::fail("Cannot initialize remote HTTP client");
    struct Response {
        std::string body;
        std::stop_token stop;
    } response{{}, stop};
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &response);
    curl_easy_setopt(
        curl.get(), CURLOPT_XFERINFOFUNCTION,
        +[](void *data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            return static_cast<Response *>(data)->stop.stop_requested() ? 1 : 0;
        });
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(
        curl.get(), CURLOPT_WRITEFUNCTION,
        +[](char *data, size_t size, size_t count, void *userdata) -> size_t {
            auto &current = *static_cast<Response *>(userdata);
            const auto bytes = size * count;
            if (current.body.size() + bytes > 2 * 1024 * 1024)
                return 0;
            current.body.append(data, bytes);
            return bytes;
        });
    curl_slist *list = nullptr;
    for (const auto &header : headers) {
        auto *next = curl_slist_append(list, header.c_str());
        if (!next) {
            curl_slist_free_all(list);
            return Return::fail("Cannot allocate remote HTTP headers");
        }
        list = next;
    }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_owner(list,
                                                                             curl_slist_free_all);
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, list);
    const auto result = curl_easy_perform(curl.get());
    if (result != CURLE_OK)
        return Return::fail(stop.stop_requested() ? "Cancelled" : curl_easy_strerror(result));
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300)
        return Return::fail("Remote API HTTP " + std::to_string(status) + ": " +
                            response.body.substr(0, 1000));
    return Return::ok(std::move(response.body));
}

std::string request_url(const RemoteProviderConfig &config) {
    auto base = config.endpoint;
    while (!base.empty() && base.back() == '/')
        base.pop_back();
    const auto suffix =
        config.protocol == RemoteProtocol::openai_compatible ? "/chat/completions" : "/messages";
    if (base.ends_with(suffix))
        return base;
    if (config.protocol == RemoteProtocol::openai_compatible && base.ends_with("/v1"))
        return base + suffix;
    if (config.protocol == RemoteProtocol::anthropic_messages && base.ends_with("/v1"))
        return base + suffix;
    return base + (config.protocol == RemoteProtocol::openai_compatible ? "/v1" : "/v1") + suffix;
}

bool valid_config(const RemoteProviderConfig &config) {
    const bool safe_endpoint = config.endpoint.find_first_of("\r\n?#@") == std::string::npos;
    return !config.endpoint.empty() && config.endpoint.starts_with("https://") && safe_endpoint &&
           !config.model.empty() && !config.api_key_environment.empty() &&
           std::isfinite(config.input_usd_per_million) &&
           std::isfinite(config.output_usd_per_million) && config.input_usd_per_million >= 0 &&
           config.output_usd_per_million >= 0 &&
           (config.input_usd_per_million > 0 || config.output_usd_per_million > 0) &&
           config.max_output_tokens > 0 && config.max_output_tokens <= 8192;
}
} // namespace

RemoteProvider::RemoteProvider(RemoteProviderConfig config, RemoteHttpTransport transport)
    : config_(std::move(config)), transport_(transport ? std::move(transport) : send_http) {}

util::Result<RemoteCostEstimate>
RemoteProvider::estimate(const std::vector<std::pair<std::string, std::string>> &messages,
                         std::size_t output_token_limit) const {
    using Return = util::Result<RemoteCostEstimate>;
    if (!valid_config(config_))
        return Return::fail("Remote provider requires HTTPS, a model, rates, and an output limit");
    const auto output_tokens = output_token_limit == 0
                                   ? static_cast<std::size_t>(config_.max_output_tokens)
                                   : output_token_limit;
    if (output_tokens == 0 || output_tokens > static_cast<std::size_t>(config_.max_output_tokens))
        return Return::fail("Remote output limit must be between 1 and the configured cap");
    std::size_t bytes = 0;
    for (const auto &[role, content] : messages) {
        if (bytes > 1024 * 1024 || role.size() + content.size() > 1024 * 1024 - bytes)
            return Return::fail("Remote prompt exceeds 1 MiB");
        bytes += role.size() + content.size();
    }
    // Treat each UTF-8 byte as a token for a conservative estimate; exact provider tokenization
    // is not available offline and underestimation could make the displayed maximum misleading.
    const auto input_tokens = bytes;
    const auto input_cost =
        static_cast<double>(input_tokens) * config_.input_usd_per_million / 1'000'000.0;
    const auto output_cost =
        static_cast<double>(output_tokens) * config_.output_usd_per_million / 1'000'000.0;
    return Return::ok({input_tokens, output_tokens, input_cost + output_cost});
}

util::Result<CompletionResult>
RemoteProvider::complete(const std::vector<std::pair<std::string, std::string>> &messages,
                         const RemoteApproval &approve, std::stop_token stop,
                         std::size_t output_token_limit) const try {
    using Return = util::Result<CompletionResult>;
    if (!approve)
        return Return::fail("Remote requests require a cost approval handler");
    if (stop.stop_requested())
        return Return::fail("Cancelled");
    auto quote = estimate(messages, output_token_limit);
    if (!quote.is_ok())
        return Return::fail(quote.error());
    std::ostringstream preview;
    preview << "Send project data to "
            << (config_.protocol == RemoteProtocol::openai_compatible ? "OpenAI-compatible"
                                                                      : "Anthropic Messages")
            << " model " << config_.model << " at " << config_.endpoint
            << "?\nEstimated maximum: " << quote.value().input_tokens << " input tokens + "
            << quote.value().output_tokens << " output tokens; estimated "
            << "charge ceiling $" << std::fixed << std::setprecision(6) << quote.value().maximum_usd
            << " USD at configured rates. Input tokens use a byte-based estimate; actual billing "
               "may differ.";
    if (!approve(preview.str()))
        return Return::fail("Remote request declined");
    if (stop.stop_requested())
        return Return::fail("Cancelled");

    const char *key = std::getenv(config_.api_key_environment.c_str());
    if (!key || !*key)
        return Return::fail("Remote API key environment variable is unset: " +
                            config_.api_key_environment);
    nlohmann::json body;
    std::vector<std::string> headers{"Content-Type: application/json"};
    if (config_.protocol == RemoteProtocol::openai_compatible) {
        body = {{"model", config_.model},
                {"messages", nlohmann::json::array()},
                {"max_tokens", quote.value().output_tokens},
                {"temperature", 0.2},
                {"store", false}};
        for (const auto &[role, content] : messages)
            body["messages"].push_back({{"role", role}, {"content", content}});
        headers.push_back("Authorization: Bearer " + std::string(key));
    } else {
        body = {{"model", config_.model},
                {"max_tokens", quote.value().output_tokens},
                {"messages", nlohmann::json::array()}};
        std::string system;
        for (const auto &[role, content] : messages) {
            if (role == "system") {
                if (!system.empty())
                    system += "\n\n";
                system += content;
            } else {
                body["messages"].push_back({{"role", role}, {"content", content}});
            }
        }
        if (!system.empty())
            body["system"] = system;
        headers.push_back("x-api-key: " + std::string(key));
        headers.push_back("anthropic-version: 2023-06-01");
    }
    auto response = transport_(request_url(config_), headers, body.dump(), stop);
    if (!response.is_ok())
        return Return::fail(response.error());
    auto parsed = nlohmann::json::parse(response.value());
    std::string content;
    if (config_.protocol == RemoteProtocol::openai_compatible) {
        content = parsed.at("choices").at(0).at("message").at("content").get<std::string>();
    } else {
        for (const auto &block : parsed.at("content"))
            if (block.value("type", "") == "text")
                content += block.at("text").get<std::string>();
    }
    if (content.empty())
        return Return::fail("Remote provider returned no text");
    return Return::ok({std::move(content)});
} catch (const std::exception &error) {
    return util::Result<CompletionResult>::fail("Invalid remote provider response: " +
                                                std::string(error.what()));
}

util::Result<std::optional<RemoteProviderConfig>>
load_remote_provider_config(const std::filesystem::path &project_root) try {
    using Return = util::Result<std::optional<RemoteProviderConfig>>;
    const auto path = project_root / ".platypus.toml";
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return Return::ok(std::nullopt);
    if (error)
        return Return::fail("Cannot inspect .platypus.toml remote settings: " + error.message());
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
        return Return::fail(".platypus.toml must be a regular project file");
    auto document = toml::parse_file(path.string());
    auto table = document.get_as<toml::table>("remote");
    if (!table)
        return Return::ok(std::nullopt);
    const auto provider = table->get_as<toml::value<std::string>>("provider");
    const auto endpoint = table->get_as<toml::value<std::string>>("endpoint");
    const auto model = table->get_as<toml::value<std::string>>("model");
    const auto key_name = table->get_as<toml::value<std::string>>("api_key_env");
    const auto input_rate = table->get_as<toml::value<double>>("input_usd_per_million");
    const auto output_rate = table->get_as<toml::value<double>>("output_usd_per_million");
    const auto max_output = table->get_as<toml::value<std::int64_t>>("max_output_tokens");
    if (!provider || !endpoint || !model || !key_name || !input_rate || !output_rate || !max_output)
        return Return::fail(
            ".platypus.toml [remote] requires provider, endpoint, model, api_key_env, "
            "input_usd_per_million, output_usd_per_million, and max_output_tokens");
    RemoteProviderConfig config;
    if (provider->get() == "openai-compatible")
        config.protocol = RemoteProtocol::openai_compatible;
    else if (provider->get() == "anthropic-messages")
        config.protocol = RemoteProtocol::anthropic_messages;
    else
        return Return::fail("[remote].provider must be openai-compatible or anthropic-messages");
    config.endpoint = endpoint->get();
    config.model = model->get();
    config.api_key_environment = key_name->get();
    config.input_usd_per_million = input_rate->get();
    config.output_usd_per_million = output_rate->get();
    if (max_output->get() < 1 || max_output->get() > 8192)
        return Return::fail("[remote].max_output_tokens must be between 1 and 8192");
    config.max_output_tokens = static_cast<int>(max_output->get());
    if (!valid_config(config))
        return Return::fail("Invalid [remote] provider settings; endpoint must use HTTPS and "
                            "rates must be non-negative with at least one non-zero rate");
    return Return::ok(std::move(config));
} catch (const std::exception &error) {
    return util::Result<std::optional<RemoteProviderConfig>>::fail(
        "Cannot parse .platypus.toml remote settings: " + std::string(error.what()));
}

} // namespace platypus::llm
