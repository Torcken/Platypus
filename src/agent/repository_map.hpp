#ifndef PLATYPUS_AGENT_REPOSITORY_MAP_HPP
#define PLATYPUS_AGENT_REPOSITORY_MAP_HPP

#include "../util/result.hpp"
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace platypus::agent {

/// Build a bounded ranked index of declarations relevant to the task and recent conversation.
class RepositoryMap {
  public:
    static util::Result<std::string> build(const std::filesystem::path &root,
                                           const std::string &request,
                                           const std::vector<std::string> &recent_context,
                                           std::size_t max_output_bytes = 4096,
                                           std::stop_token stop = {});
};

} // namespace platypus::agent

#endif // PLATYPUS_AGENT_REPOSITORY_MAP_HPP
