#ifndef PLATYPUS_LLM_MODEL_PROFILE_HPP
#define PLATYPUS_LLM_MODEL_PROFILE_HPP

#include <string>
#include <vector>
#include <map>

namespace platypus::llm {

/**
 * @brief Represents the configuration for a specific model.
 */
struct ModelProfile {
    std::string name;
    std::string model_id;
    std::string base_url;
    std::map<std::string, std::string> parameters;
    
    // Example parameters: "temperature", "max_tokens", "top_p"
};

/**
 * @brief Utility to load and manage model profiles.
 */
class ModelProfileManager {
public:
    /**
     * @brief Loads a model profile from a configuration file.
     * @param path Path to the profile file.
     * @return A result containing the loaded profile.
     */
    static std::string load_profile(const std::string& path);

    /**
     * @brief Retrieves a profile by name.
     * @param name Name of the profile.
     * @return The profile if found, otherwise an empty profile.
     */
    static ModelProfile get_profile(const std::string& name);

private:
    static std::map<std::string, ModelProfile> profiles_;
};

} // namespace platypus::llm

#endif // PLATYPUS_LLM_MODEL_PROFILE_HPP
