// Versioned catalog data and deterministic hardware-aware model selection.
// Hardware discovery and downloading are separate platform services.
#ifndef PLATYPUS_PLATFORM_MODEL_CATALOG_HPP
#define PLATYPUS_PLATFORM_MODEL_CATALOG_HPP

#include "../util/result.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace platypus::platform {

/// User-facing model preference, ordered by catalog-curated rank.
enum class ModelPreference { fast, balanced, quality };

/// Hardware facts used for filtering catalog candidates; zero means unavailable.
struct HardwareFacts {
    std::uint64_t system_memory_bytes = 0;
    std::uint64_t gpu_memory_bytes = 0;
};

/// One downloadable GGUF artifact and its project profile.
struct ModelVariant {
    std::string id;
    std::string display_name;
    std::string quantization;
    std::string url;
    std::string sha256;
    std::uint64_t size_hint_bytes;
    std::uint64_t minimum_memory_bytes;
    std::uint64_t minimum_gpu_memory_bytes;
    std::string profile;
    std::optional<int> fast_rank;
    std::optional<int> balanced_rank;
    std::optional<int> quality_rank;
};

/// A validated, versioned list of model artifacts.
struct ModelCatalog {
    int version;
    std::vector<ModelVariant> models;
};

/// Parse version 1 JSON and reject incomplete, unsafe, duplicate, or unbounded entries.
util::Result<ModelCatalog> parse_model_catalog(std::string_view json_text);

/// Select the lowest-ranked artifact for a preference that fits the reported hardware.
util::Result<ModelVariant> recommend_model(const ModelCatalog &catalog, const HardwareFacts &facts,
                                           ModelPreference preference);

} // namespace platypus::platform

#endif
