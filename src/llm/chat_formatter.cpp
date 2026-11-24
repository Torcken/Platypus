#include "chat_formatter.hpp"
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace platypus::llm {
std::string ChatFormatter::render(const std::vector<std::pair<std::string, std::string>> &messages,
                                  const ModelProfile &profile) {
    std::string result;
    for (const auto &[role, content] : messages) {
        auto prefix = profile.turn_prefix;
        auto marker = prefix.find("{role}");
        if (marker == std::string::npos)
            throw std::runtime_error("Profile turn_prefix requires {role}");
        prefix.replace(marker, 6, role == "assistant" ? profile.assistant_role : role);
        result += prefix + (role == "assistant" ? profile.generation_suffix : "") + content +
                  profile.turn_suffix;
    }
    auto prefix = profile.turn_prefix;
    auto marker = prefix.find("{role}");
    if (marker == std::string::npos)
        throw std::runtime_error("Profile turn_prefix requires {role}");
    prefix.replace(marker, 6, profile.assistant_role);
    return result + prefix + profile.generation_suffix;
}

std::string
ChatFormatter::to_json(const std::vector<std::pair<std::string, std::string>> &messages) {
    nlohmann::json formatted = nlohmann::json::array();
    for (const auto &[role, content] : messages)
        formatted.push_back({{"role", role}, {"content", content}});
    return formatted.dump();
}

std::string
ChatFormatter::to_raw_string(const std::vector<std::pair<std::string, std::string>> &messages,
                             const std::string &system_prompt) {
    std::stringstream ss;

    if (!system_prompt.empty()) {
        ss << "System: " << system_prompt << "\n\n";
    }

    for (const auto &[role, content] : messages) {
        ss << role << ": " << content << "\n\n";
    }

    return ss.str();
}

} // namespace platypus::llm
