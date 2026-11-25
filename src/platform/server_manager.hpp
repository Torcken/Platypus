// Lifecycle owner for a locally managed llama-server child process.
#ifndef PLATYPUS_PLATFORM_SERVER_MANAGER_HPP
#define PLATYPUS_PLATFORM_SERVER_MANAGER_HPP

#include "../util/result.hpp"
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace platypus::platform {

struct ServerConfig {
    std::filesystem::path executable;
    std::filesystem::path model;
    unsigned short port = 8080;
    std::size_t context_tokens = 32768;
    std::size_t minimum_context_tokens = 4096;
    std::chrono::milliseconds readiness_timeout{60000};
    unsigned int maximum_oom_restarts = 1;
};

class ServerProcess {
  public:
    virtual ~ServerProcess() = default;
    virtual bool is_running() = 0;
    /// Return the bounded diagnostic tail used to classify a child startup failure.
    virtual std::string diagnostic_output() const = 0;
    virtual void stop() noexcept = 0;
};

using ServerLauncher =
    std::function<util::Result<std::shared_ptr<ServerProcess>>(const ServerConfig &config)>;
using ServerHealthProbe = std::function<bool(const std::string &endpoint)>;

/// Starts and owns llama-server, waits for readiness, and permits bounded OOM recovery.
class ServerManager {
  public:
    explicit ServerManager(ServerConfig config, ServerLauncher launcher = {},
                           ServerHealthProbe health_probe = {});
    ~ServerManager();
    ServerManager(const ServerManager &) = delete;
    ServerManager &operator=(const ServerManager &) = delete;

    util::Result<util::Void> start();
    util::Result<util::Void> restart_after_out_of_memory();
    void stop() noexcept;
    std::string endpoint() const;

  private:
    ServerConfig config_;
    ServerLauncher launcher_;
    ServerHealthProbe health_probe_;
    std::shared_ptr<ServerProcess> process_;
    unsigned int oom_restarts_ = 0;
};

} // namespace platypus::platform

#endif
