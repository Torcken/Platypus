#include "model_profile.hpp"
#include "../util/logger.hpp"
#include <iostream>

namespace platypus::llm {

// Initialize the static member
std::map<std::string, ModelProfile> ModelProfileManager::profiles_;

std::string ModelProfileManager::load_profile(const std::string& path) {
    // For now, we will simulate loading a profile from a file.
    // In a production environment, we would use a TOML parser here.
    
    util::Logger::get_instance().debug("Loading model profile from: " + path);
    
    // Mock loading a "llama-70b" profile
    ModelProfile llama_70b;
    llama_70b.name = "llama-70b";
    llama_70b.model_id = "meta-llama/Llama-3.1-70B-Instruct";
    llama_70b.base_url = "http://localhost:8080";
    llama_70b.parameters = {
        {"temperature", "0.7"},
        {"max_tokens", "4096"},
        {"top_p", "0.9"}
    };

    profiles_[llama_70b.name] = llama_70b;
    
    return "Successfully loaded profile: " + llama_70b.name;
}

ModelProfile ModelProfileManager::get_profile(const std::string& name) {
    if (profiles_.find(name) != profiles_.end()) {
        return profiles_[name];
    }
    
    util::Logger::get_instance().error("Profile not found: " + name);
    return ModelProfile{};
}

} // namespace platypus::llm
