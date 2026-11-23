#include "chat_formatter.hpp"
#include <sstream>
#include <iomanip>

namespace platypus::llm {

std::string ChatFormatter::to_json(const std::vector<std::pair<std::string, std::string>>& messages) {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < messages.size(); ++i) {
        ss << "{\"role\":\"" << messages[i].first << "\",\"content\":\"" << messages[i].second << "\"}";
        if (i < messages.size() - 1) ss << ",";
    }
    ss << "]";
    return ss.str();
}

std::string ChatFormatter::to_raw_string(const std::vector<std::pair<std::string, std::string>>& messages, 
                                         const std::string& system_prompt) {
    std::stringstream ss;
    
    if (!system_prompt.empty()) {
        ss << "System: " << system_prompt << "\n\n";
    }

    for (const auto& [role, content] : messages) {
        ss << role << ": " << content << "\n\n";
    }
    
    return ss.str();
}

} // namespace platypus::llm
