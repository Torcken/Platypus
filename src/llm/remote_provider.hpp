// Remote text completion for the optional hybrid workflow.
// The provider accepts plan or review prompts only and requires per-request cost approval.
#ifndef PLATYPUS_LLM_REMOTE_PROVIDER_HPP
#define PLATYPUS_LLM_REMOTE_PROVIDER_HPP

#include "backend.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace platypus::llm {

/// Identifies the supported message protocol.
enum class RemoteProtocol { openai_compatible, anthropic_messages };

/// User-supplied endpoint and pricing; prices are USD per million tokens.
struct RemoteProviderConfig {
    RemoteProtocol protocol = RemoteProtocol::openai_compatible;
    std::string endpoint;
    std::string model;
    std::string api_key_environment;
    double input_usd_per_million = 0;
    double output_usd_per_million = 0;
    int max_output_tokens = 1024;
};

/// Estimate the maximum charge using a conservative bytes-per-token approximation.
struct RemoteCostEstimate {
    std::size_t input_tokens;
    std::size_t output_tokens;
    double maximum_usd;
};

using RemoteHttpTransport = std::function<util::Result<std::string>(
    const std::string &, const std::vector<std::string> &, const std::string &, std::stop_token)>;
using RemoteApproval = std::function<bool(const std::string &)>;

/// Calls a configured text API after showing a bounded, rate-based maximum cost estimate.
class RemoteProvider {
  public:
    /// Use the production HTTP transport when no test transport is supplied.
    explicit RemoteProvider(RemoteProviderConfig config, RemoteHttpTransport transport = {});
    /// Return the configured-rate estimate for the prompt and output cap.
    util::Result<RemoteCostEstimate>
    estimate(const std::vector<std::pair<std::string, std::string>> &messages,
             std::size_t output_token_limit = 0) const;
    /// Complete a prompt only after the approval callback accepts the estimate.
    util::Result<CompletionResult>
    complete(const std::vector<std::pair<std::string, std::string>> &messages,
             const RemoteApproval &approve, std::stop_token stop = {},
             std::size_t output_token_limit = 0) const;

  private:
    RemoteProviderConfig config_;
    RemoteHttpTransport transport_;
};

/// Read optional `[remote]` settings from the project's `.platypus.toml` file.
util::Result<std::optional<RemoteProviderConfig>>
load_remote_provider_config(const std::filesystem::path &project_root);

} // namespace platypus::llm

#endif
