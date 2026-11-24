// Validated TOML model profiles; profile values never come from a model-name branch.
// The CLI selects a profile explicitly and the backend renders its role delimiters.
#include "model_profile.hpp"
#include <toml++/toml.hpp>
namespace platypus::llm {
std::map<std::string, ModelProfile> ModelProfileManager::profiles_;
util::Result<ModelProfile> ModelProfileManager::read_profile(const std::string &path) {
    using Return = util::Result<ModelProfile>;
    try {
        auto table = toml::parse_file(path);
        ModelProfile profile;
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
        if (auto stops = table["stop_strings"].as_array())
            for (const auto &value : *stops) {
                auto stop = value.value<std::string>();
                if (!stop)
                    return Return::fail("Stop strings must be strings");
                profile.stop_strings.push_back(*stop);
            }
        if (profile.name.empty() || profile.context_tokens < 1024 ||
            profile.context_tokens > 262144 || profile.temperature < 0 || profile.temperature > 2)
            return Return::fail("Invalid profile name, context size or temperature");
        if (profile.tool_syntax != "json_tagged" && profile.tool_syntax != "qwen_xml" &&
            profile.tool_syntax != "gemma")
            return Return::fail("Unsupported tool syntax family");
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
