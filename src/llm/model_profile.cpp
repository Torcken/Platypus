// Validated TOML model profiles; profile values never come from a model-name branch.
// The CLI selects a profile explicitly and the backend renders its role delimiters.
#include "model_profile.hpp"
#include <cmath>
#include <toml++/toml.hpp>
namespace platypus::llm {
std::map<std::string, ModelProfile> ModelProfileManager::profiles_;
util::Result<ModelProfile> ModelProfileManager::read_profile(const std::string &path) {
    using Return = util::Result<ModelProfile>;
    try {
        auto table = toml::parse_file(path);
        ModelProfile profile;
        for (const auto *key : {"name", "model_id", "base_url", "tool_syntax"})
            if (table.contains(key) && !table[key].is_string())
                return Return::fail(std::string(key) + " must be a string");
        for (const auto *key : {"template", "reasoning"})
            if (table.contains(key) && !table[key].is_table())
                return Return::fail(std::string(key) + " must be a table");
        if (table.contains("sampling") && !table["sampling"].is_table())
            return Return::fail("sampling must be a table");
        for (const auto *key :
             {"turn_prefix", "turn_suffix", "assistant_role", "generation_suffix"})
            if (table["template"][key] && !table["template"][key].is_string())
                return Return::fail(std::string("template.") + key + " must be a string");
        for (const auto *key : {"open", "close"})
            if (table["reasoning"][key] && !table["reasoning"][key].is_string())
                return Return::fail(std::string("reasoning.") + key + " must be a string");
        if (table.contains("stop_strings") && !table["stop_strings"].is_array())
            return Return::fail("stop_strings must be an array");
        if (table.contains("context_tokens")) {
            auto context = table["context_tokens"].value<std::int64_t>();
            if (!table["context_tokens"].is_integer() || !context || *context < 1024 ||
                *context > 262144)
                return Return::fail("context_tokens must be an integer in [1024, 262144]");
        }
        if (table.contains("temperature") && !table["temperature"].is_number())
            return Return::fail("temperature must be numeric");
        if (table["reasoning"]["preserve"] && !table["reasoning"]["preserve"].is_boolean())
            return Return::fail("reasoning.preserve must be boolean");
        profile.name = table["name"].value_or(std::string{});
        profile.model_id = table["model_id"].value_or(std::string{});
        profile.base_url = table["base_url"].value_or(std::string("http://127.0.0.1:8080"));
        profile.tool_syntax = table["tool_syntax"].value_or(std::string("json_tagged"));
        profile.context_tokens = table["context_tokens"].value_or(16384);
        profile.temperature = static_cast<float>(table["temperature"].value_or(0.3));
        profile.turn_prefix = table["template"]["turn_prefix"].value_or(std::string{});
        profile.turn_suffix = table["template"]["turn_suffix"].value_or(std::string{});
        profile.assistant_role =
            table["template"]["assistant_role"].value_or(std::string("assistant"));
        profile.generation_suffix = table["template"]["generation_suffix"].value_or(std::string{});
        profile.reasoning_open = table["reasoning"]["open"].value_or(std::string{});
        profile.reasoning_close = table["reasoning"]["close"].value_or(std::string{});
        profile.preserve_reasoning = table["reasoning"]["preserve"].value_or(false);
        if (auto sampling = table["sampling"].as_table()) {
            for (const auto &[key, value] : *sampling) {
                auto name = std::string(key.str());
                if (name == "top_k") {
                    auto number = value.value<std::int64_t>();
                    if (!value.is_integer() || !number || *number < 0 || *number > 1000000)
                        return Return::fail("sampling.top_k must be an integer in [0, 1000000]");
                    profile.sampling[name] = *number;
                } else if (name == "seed") {
                    auto number = value.value<std::int64_t>();
                    if (!value.is_integer() || !number || *number < -1 || *number > 4294967295LL)
                        return Return::fail("sampling.seed must be an integer in [-1, 4294967295]");
                    profile.sampling[name] = *number;
                } else if (name == "top_p" || name == "min_p" || name == "typical_p") {
                    auto number = value.value<double>();
                    if (!value.is_number() || !number || !std::isfinite(*number) || *number < 0 ||
                        *number > 1)
                        return Return::fail("sampling." + name + " must be numeric in [0, 1]");
                    profile.sampling[name] = *number;
                } else if (name == "repeat_penalty") {
                    auto number = value.value<double>();
                    if (!value.is_number() || !number || !std::isfinite(*number) || *number < 0 ||
                        *number > 10)
                        return Return::fail("sampling.repeat_penalty must be numeric in [0, 10]");
                    profile.sampling[name] = *number;
                } else if (name == "presence_penalty" || name == "frequency_penalty") {
                    auto number = value.value<double>();
                    if (!value.is_number() || !number || !std::isfinite(*number) || *number < -2 ||
                        *number > 2)
                        return Return::fail("sampling." + name + " must be numeric in [-2, 2]");
                    profile.sampling[name] = *number;
                } else {
                    return Return::fail("Unsupported sampling field: " + name);
                }
            }
        }
        if (auto stops = table["stop_strings"].as_array())
            for (const auto &value : *stops) {
                auto stop = value.value<std::string>();
                if (!stop)
                    return Return::fail("Stop strings must be strings");
                profile.stop_strings.push_back(*stop);
            }
        if (profile.name.empty() || profile.context_tokens < 1024 ||
            profile.context_tokens > 262144 || profile.temperature < 0 || profile.temperature > 2 ||
            !std::isfinite(profile.temperature))
            return Return::fail("Invalid profile name, context size or temperature");
        if (profile.tool_syntax != "json_tagged" && profile.tool_syntax != "qwen_xml" &&
            profile.tool_syntax != "gemma")
            return Return::fail("Unsupported tool syntax family");
        if (profile.reasoning_open.empty() != profile.reasoning_close.empty())
            return Return::fail("Reasoning markers must be configured together");
        if (!profile.turn_prefix.empty() && profile.turn_prefix.find("{role}") == std::string::npos)
            return Return::fail("turn_prefix requires {role}");
        return Return::ok(profile);
    } catch (const std::exception &e) {
        return Return::fail(e.what());
    }
}
std::string ModelProfileManager::load_profile(const std::string &path) {
    auto profile = read_profile(path);
    if (!profile.is_ok())
        return "Profile error: " + profile.error();
    profiles_[profile.value().name] = profile.value();
    return "Loaded profile: " + profile.value().name;
}
ModelProfile ModelProfileManager::get_profile(const std::string &name) {
    auto found = profiles_.find(name);
    return found == profiles_.end() ? ModelProfile{} : found->second;
}
} // namespace platypus::llm
