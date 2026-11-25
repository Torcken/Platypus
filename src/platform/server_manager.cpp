// Bounded llama-server startup, readiness, restart, and shutdown.
#include "server_manager.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <reproc++/reproc.hpp>
#include <thread>
#include <vector>

namespace platypus::platform {
namespace {
constexpr std::size_t kDiagnosticTailBytes = 64 * 1024;

class ReprocServerProcess final : public ServerProcess {
  public:
    explicit ReprocServerProcess(std::unique_ptr<reproc::process> process)
        : process_(std::move(process)), output_thread_([this] { capture_output(); }) {}

    ~ReprocServerProcess() override { stop(); }

    bool is_running() override { return running_.load(); }

    std::string diagnostic_output() const override {
        std::lock_guard lock(output_mutex_);
        return output_tail_;
    }

    void stop() noexcept override {
        stop_requested_.store(true);
        {
            std::lock_guard lock(process_mutex_);
            reproc::stop_actions actions{{reproc::stop::terminate, reproc::milliseconds(1500)},
                                         {reproc::stop::kill, reproc::milliseconds(1500)},
                                         {reproc::stop::noop, reproc::milliseconds(0)}};
            (void)process_->stop(actions);
        }
        running_.store(false);
        if (output_thread_.joinable())
            output_thread_.join();
    }

  private:
    void append_output(const std::uint8_t *bytes, std::size_t size) {
        if (size == 0)
            return;
        std::lock_guard lock(output_mutex_);
        if (size >= kDiagnosticTailBytes) {
            output_tail_.assign(reinterpret_cast<const char *>(bytes + size - kDiagnosticTailBytes),
                                kDiagnosticTailBytes);
            return;
        }
        const auto overflow = output_tail_.size() + size > kDiagnosticTailBytes
                                  ? output_tail_.size() + size - kDiagnosticTailBytes
                                  : 0;
        output_tail_.erase(0, overflow);
        output_tail_.append(reinterpret_cast<const char *>(bytes), size);
    }

    void capture_output() noexcept {
        bool exited = false;
        bool stdout_closed = false;
        bool stderr_closed = false;
        std::uint8_t buffer[4096]{};
        while (!stop_requested_.load()) {
            std::lock_guard lock(process_mutex_);
            const int interests =
                reproc::event::out | reproc::event::err | (exited ? 0 : reproc::event::exit);
            const auto [events, poll_error] = process_->poll(interests, reproc::milliseconds(100));
            if (poll_error && poll_error != std::errc::timed_out &&
                poll_error != std::errc::broken_pipe) {
                break;
            }
            if (!exited && (events & reproc::event::exit)) {
                (void)process_->wait(reproc::milliseconds(0));
                exited = true;
            }
            if (events & reproc::event::out) {
                const auto [count, read_error] =
                    process_->read(reproc::stream::out, buffer, sizeof(buffer));
                if (!read_error)
                    append_output(buffer, count);
                stdout_closed = read_error == std::errc::broken_pipe;
            }
            if (events & reproc::event::err) {
                const auto [count, read_error] =
                    process_->read(reproc::stream::err, buffer, sizeof(buffer));
                if (!read_error)
                    append_output(buffer, count);
                stderr_closed = read_error == std::errc::broken_pipe;
            }
            if (exited && stdout_closed && stderr_closed)
                break;
        }
        running_.store(false);
    }

    std::unique_ptr<reproc::process> process_;
    std::atomic<bool> running_{true};
    std::atomic<bool> stop_requested_{false};
    mutable std::mutex output_mutex_;
    std::mutex process_mutex_;
    std::string output_tail_;
    std::thread output_thread_;
};

bool indicates_out_of_memory(std::string diagnostic) {
    std::transform(
        diagnostic.begin(), diagnostic.end(), diagnostic.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    // These phrases come from the ADR-002-pinned llama.cpp allocator and context setup paths.
    return diagnostic.find("out of memory") != std::string::npos ||
           diagnostic.find("out of device memory") != std::string::npos ||
           diagnostic.find("insufficient memory") != std::string::npos ||
           diagnostic.find("failed to allocate compute pp buffers") != std::string::npos ||
           diagnostic.find("failed to allocate compute tg buffers") != std::string::npos;
}

util::Result<std::shared_ptr<ServerProcess>> launch_server(const ServerConfig &config) {
    using Return = util::Result<std::shared_ptr<ServerProcess>>;
    if (config.executable.empty() || config.model.empty() || config.port == 0 ||
        !std::filesystem::is_regular_file(config.model))
        return Return::fail("llama-server requires an executable and an existing model file");
    auto process = std::make_unique<reproc::process>();
    reproc::options options;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::pipe;
    const std::vector<std::string> arguments{config.executable.string(),
                                             "--model",
                                             config.model.string(),
                                             "--host",
                                             "127.0.0.1",
                                             "--port",
                                             std::to_string(config.port),
                                             "--ctx-size",
                                             std::to_string(config.context_tokens)};
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
    const auto health_url = endpoint + "/health";
    curl_easy_setopt(curl.get(), CURLOPT_URL, health_url.c_str());
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
    if (config_.minimum_context_tokens == 0 ||
        config_.context_tokens < config_.minimum_context_tokens)
        return Return::fail("llama-server context size is below the configured minimum");
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
        const auto running = process_->is_running();
        if (!running) {
            const auto diagnostics = process_->diagnostic_output();
            const bool out_of_memory = indicates_out_of_memory(diagnostics);
            process_->stop();
            process_.reset();
            if (out_of_memory)
                return restart_after_out_of_memory();
            return Return::fail("llama-server exited before becoming ready");
        }
        const auto ready = health_probe_(endpoint());
        if (ready)
            return Return::ok();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    stop();
    return Return::fail("llama-server readiness timed out");
}

util::Result<util::Void> ServerManager::restart_after_out_of_memory() {
    if (oom_restarts_ >= config_.maximum_oom_restarts)
        return util::Result<util::Void>::fail("llama-server OOM restart limit reached");
    const auto reduced_context = config_.context_tokens / 2;
    if (reduced_context < config_.minimum_context_tokens)
        return util::Result<util::Void>::fail(
            "llama-server cannot reduce context below its minimum");
    ++oom_restarts_;
    config_.context_tokens = reduced_context;
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
