// Bounded llama-server startup, readiness, restart, and shutdown.
#include "server_manager.hpp"

#include <curl/curl.h>
#include <memory>
#include <reproc++/reproc.hpp>
#include <thread>
#include <vector>

namespace platypus::platform {
namespace {
class ReprocServerProcess final : public ServerProcess {
  public:
    explicit ReprocServerProcess(std::unique_ptr<reproc::process> process)
        : process_(std::move(process)) {}

    bool is_running() override {
        const auto [status, error] = process_->wait(reproc::milliseconds(0));
        (void)status;
        return error == std::make_error_code(std::errc::timed_out);
    }

    void stop() noexcept override {
        reproc::stop_actions actions{{reproc::stop::terminate, reproc::milliseconds(1500)},
                                     {reproc::stop::kill, reproc::milliseconds(1500)},
                                     {reproc::stop::noop, reproc::milliseconds(0)}};
        (void)process_->stop(actions);
    }

  private:
    std::unique_ptr<reproc::process> process_;
};

util::Result<std::shared_ptr<ServerProcess>> launch_server(const ServerConfig &config) {
    using Return = util::Result<std::shared_ptr<ServerProcess>>;
    if (config.executable.empty() || config.model.empty() || config.port == 0 ||
        !std::filesystem::is_regular_file(config.model))
        return Return::fail("llama-server requires an executable and an existing model file");
    auto process = std::make_unique<reproc::process>();
    reproc::options options;
    options.redirect.discard = true;
    const std::vector<std::string> arguments{config.executable.string(),
                                             "--model",
                                             config.model.string(),
                                             "--host",
                                             "127.0.0.1",
                                             "--port",
                                             std::to_string(config.port)};
    const auto error = process->start(arguments, options);
    if (error)
        return Return::fail("Cannot start llama-server: " + error.message());
    std::shared_ptr<ServerProcess> managed =
        std::make_shared<ReprocServerProcess>(std::move(process));
    return Return::ok(std::move(managed));
}

bool health_request(const std::string &endpoint) {
    static const bool curl_initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!curl_initialized)
        return false;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl)
        return false;
    curl_easy_setopt(curl.get(), CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 500L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 1000L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(
        curl.get(), CURLOPT_WRITEFUNCTION,
        +[](char *, size_t size, size_t count, void *) -> size_t { return size * count; });
    if (curl_easy_perform(curl.get()) != CURLE_OK)
        return false;
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    return status == 200;
}
} // namespace

ServerManager::ServerManager(ServerConfig config, ServerLauncher launcher,
                             ServerHealthProbe health_probe)
    : config_(std::move(config)), launcher_(launcher ? std::move(launcher) : launch_server),
      health_probe_(health_probe ? std::move(health_probe) : health_request) {}

ServerManager::~ServerManager() { stop(); }

std::string ServerManager::endpoint() const {
    return "http://127.0.0.1:" + std::to_string(config_.port);
}

util::Result<util::Void> ServerManager::start() {
    using Return = util::Result<util::Void>;
    if (config_.readiness_timeout <= std::chrono::milliseconds::zero() ||
        config_.readiness_timeout > std::chrono::minutes(10) || config_.port == 0)
        return Return::fail("llama-server readiness timeout or port is invalid");
    if (config_.maximum_oom_restarts > 3)
        return Return::fail("llama-server OOM restart limit cannot exceed three");
    if (process_ && process_->is_running())
        return Return::fail("llama-server is already running");
    process_.reset();
    auto launched = launcher_(config_);
    if (!launched.is_ok())
        return Return::fail(launched.error());
    process_ = std::move(launched).value();
    if (!process_)
        return Return::fail("llama-server launcher returned no process");
    const auto deadline = std::chrono::steady_clock::now() + config_.readiness_timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!process_->is_running()) {
            process_.reset();
            return Return::fail("llama-server exited before becoming ready");
        }
        if (health_probe_(endpoint()))
            return Return::ok();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    stop();
    return Return::fail("llama-server readiness timed out");
}

util::Result<util::Void> ServerManager::restart_after_out_of_memory() {
    if (oom_restarts_ >= config_.maximum_oom_restarts)
        return util::Result<util::Void>::fail("llama-server OOM restart limit reached");
    ++oom_restarts_;
    stop();
    return start();
}

void ServerManager::stop() noexcept {
    if (process_) {
        process_->stop();
        process_.reset();
    }
}

} // namespace platypus::platform
