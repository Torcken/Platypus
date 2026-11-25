// Validates the versioned model catalog before recommendations use downloaded artifact metadata.
// The catalog is data, so malformed or hostile entries fail closed at this boundary.
#include "model_catalog.hpp"
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace platypus::platform {
namespace {
constexpr std::size_t kMaximumCatalogBytes = 256 * 1024;
constexpr std::size_t kMaximumModelCount = 256;
constexpr std::uint64_t kMaximumArtifactBytes = 100ULL * 1024 * 1024 * 1024;

bool bounded_text(const std::string &value, std::size_t maximum = 256) {
    if (value.empty() || value.size() > maximum)
        return false;
    return std::none_of(value.begin(), value.end(), [](unsigned char character) {
        return character < 0x20 || character == 0x7f;
    });
}

bool valid_sha256(const std::string &value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
               return std::isxdigit(ch) != 0;
           });
}

bool pinned_huggingface_url(const std::string &url) {
    constexpr std::string_view prefix = "https://huggingface.co/";
    if (!url.starts_with(prefix))
        return false;
    const auto resolve = url.find("/resolve/", prefix.size());
    if (resolve == std::string::npos)
        return false;
    const auto repository = url.substr(prefix.size(), resolve - prefix.size());
    const auto repository_separator = repository.find('/');
    if (repository_separator == std::string::npos || repository_separator == 0 ||
        repository_separator + 1 == repository.size() ||
        repository.find('/', repository_separator + 1) != std::string::npos ||
        repository.find("..") != std::string::npos)
        return false;
    const auto revision_start = resolve + std::string_view("/resolve/").size();
    const auto artifact_separator = url.find('/', revision_start);
    if (artifact_separator == std::string::npos || artifact_separator - revision_start != 40 ||
        artifact_separator + 1 == url.size())
        return false;
    const auto revision = url.substr(revision_start, 40);
    if (!std::all_of(revision.begin(), revision.end(),
                     [](unsigned char ch) { return std::isxdigit(ch) != 0; }))
        return false;
    const auto artifact = url.substr(artifact_separator + 1);
    if (artifact.find("..") != std::string::npos || artifact.find('\\') != std::string::npos)
        return false;
    return true;
}

std::optional<int> rank(const nlohmann::json &ranks, const char *name) {
    if (!ranks.contains(name) || ranks.at(name).is_null())
        return std::nullopt;
    if (!ranks.at(name).is_number_integer())
        throw std::runtime_error(std::string("rank '") + name + "' must be an integer or null");
    if (ranks.at(name).is_number_unsigned()) {
        const auto value = ranks.at(name).get<std::uint64_t>();
        if (value < 1 || value > 10000)
            throw std::runtime_error(std::string("rank '") + name + "' is out of range");
        return static_cast<int>(value);
    }
    const auto value = ranks.at(name).get<std::int64_t>();
    if (value < 1 || value > 10000)
        throw std::runtime_error(std::string("rank '") + name + "' is out of range");
    return static_cast<int>(value);
}
} // namespace

util::Result<ModelCatalog> parse_model_catalog(std::string_view text) try {
    using Return = util::Result<ModelCatalog>;
    if (text.empty() || text.size() > kMaximumCatalogBytes)
        return Return::fail("Model catalog is empty or exceeds 256 KiB");
    const auto root = nlohmann::json::parse(text);
    if (!root.is_object() || root.value("version", 0) != 1 || !root.contains("models") ||
        !root.at("models").is_array() || root.at("models").empty() ||
        root.at("models").size() > kMaximumModelCount)
        return Return::fail("Model catalog requires version 1 and 1 to 256 models");
    ModelCatalog catalog{1, {}};
    std::set<std::string> ids;
    for (const auto &entry : root.at("models")) {
        if (!entry.is_object())
            return Return::fail("Each model catalog entry must be an object");
        const auto string_field = [&](const char *key) -> std::string {
            if (!entry.contains(key) || !entry.at(key).is_string())
                throw std::runtime_error(std::string("model entry requires string '") + key + "'");
            return entry.at(key).get<std::string>();
        };
        const auto number_field = [&](const char *key) -> std::uint64_t {
            if (!entry.contains(key) || !entry.at(key).is_number_integer())
                throw std::runtime_error(std::string("model entry requires unsigned '") + key +
                                         "'");
            if (entry.at(key).is_number_unsigned())
                return entry.at(key).get<std::uint64_t>();
            const auto signed_value = entry.at(key).get<std::int64_t>();
            if (signed_value < 0)
                throw std::runtime_error(std::string("model entry has negative '") + key + "'");
            return static_cast<std::uint64_t>(signed_value);
        };
        ModelVariant model;
        model.id = string_field("id");
        model.display_name = string_field("display_name");
        model.quantization = string_field("quantization");
        model.url = string_field("url");
        model.sha256 = string_field("sha256");
        model.profile = string_field("profile");
        model.size_hint_bytes = number_field("size_hint_bytes");
        model.minimum_memory_bytes = number_field("minimum_memory_bytes");
        model.minimum_gpu_memory_bytes = number_field("minimum_gpu_memory_bytes");
        if (!bounded_text(model.id, 96) || !ids.insert(model.id).second ||
            !bounded_text(model.display_name) || !bounded_text(model.quantization, 32) ||
            !valid_sha256(model.sha256) || model.url.size() > 2048 ||
            !pinned_huggingface_url(model.url) ||
            model.url.find_first_of("\r\n?#@") != std::string::npos ||
            !model.profile.starts_with("profiles/") ||
            model.profile.find("..") != std::string::npos ||
            model.profile.find_first_of("\\\r\n") != std::string::npos ||
            !model.profile.ends_with(".toml") || model.size_hint_bytes == 0 ||
            model.size_hint_bytes > kMaximumArtifactBytes || model.minimum_memory_bytes == 0 ||
            model.minimum_memory_bytes > 1024ULL * 1024 * 1024 * 1024 ||
            model.minimum_gpu_memory_bytes > 1024ULL * 1024 * 1024 * 1024)
            return Return::fail("Model catalog entry contains unsafe or invalid values");
        if (!entry.contains("ranks") || !entry.at("ranks").is_object())
            return Return::fail("Model catalog entry requires a ranks object");
        const auto &ranks = entry.at("ranks");
        model.fast_rank = rank(ranks, "fast");
        model.balanced_rank = rank(ranks, "balanced");
        model.quality_rank = rank(ranks, "quality");
        if (!model.fast_rank && !model.balanced_rank && !model.quality_rank)
            return Return::fail("Each model needs at least one preference rank");
        catalog.models.push_back(std::move(model));
    }
    return Return::ok(std::move(catalog));
} catch (const std::exception &error) {
    return util::Result<ModelCatalog>::fail("Invalid model catalog: " + std::string(error.what()));
}

util::Result<ModelVariant> recommend_model(const ModelCatalog &catalog, const HardwareFacts &facts,
                                           ModelPreference preference) {
    using Return = util::Result<ModelVariant>;
    if (facts.system_memory_bytes == 0)
        return Return::fail("System memory is unavailable; cannot make a safe recommendation");
    const ModelVariant *selected = nullptr;
    std::optional<int> selected_rank;
    for (const auto &model : catalog.models) {
        const auto candidate_rank = preference == ModelPreference::fast       ? model.fast_rank
                                    : preference == ModelPreference::balanced ? model.balanced_rank
                                                                              : model.quality_rank;
        if (!candidate_rank || facts.system_memory_bytes < model.minimum_memory_bytes ||
            facts.gpu_memory_bytes < model.minimum_gpu_memory_bytes)
            continue;
        if (!selected_rank || *candidate_rank < *selected_rank ||
            (*candidate_rank == *selected_rank && model.id < selected->id)) {
            selected = &model;
            selected_rank = candidate_rank;
        }
    }
    if (!selected)
        return Return::fail("No catalog model fits the detected hardware and preference");
    return Return::ok(*selected);
}

} // namespace platypus::platform
