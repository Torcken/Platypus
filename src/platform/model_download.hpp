// Verified, resumable downloads for immutable catalog artifacts.
#ifndef PLATYPUS_PLATFORM_MODEL_DOWNLOAD_HPP
#define PLATYPUS_PLATFORM_MODEL_DOWNLOAD_HPP

#include "../util/result.hpp"
#include "model_catalog.hpp"
#include <filesystem>
#include <functional>
#include <stop_token>

namespace platypus::platform {

using DownloadProgress = std::function<void(std::uint64_t received, std::uint64_t total)>;
using DownloadTransfer = std::function<util::Result<std::uint64_t>(
    const std::string &url, const std::filesystem::path &partial_path, std::uint64_t offset,
    std::uint64_t maximum_bytes, const DownloadProgress &progress, std::stop_token stop)>;

/// Download a pinned catalog artifact to destination, resuming its .part file and verifying size
/// and SHA-256 before atomically making it visible. A transfer seam keeps tests offline.
util::Result<std::filesystem::path> download_model(const ModelVariant &model,
                                                   const std::filesystem::path &destination,
                                                   DownloadProgress progress = {},
                                                   std::stop_token stop = {},
                                                   DownloadTransfer transfer = {});

} // namespace platypus::platform

#endif
