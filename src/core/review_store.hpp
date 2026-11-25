#ifndef PLATYPUS_CORE_REVIEW_STORE_HPP
#define PLATYPUS_CORE_REVIEW_STORE_HPP

#include "../util/result.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace platypus::core {

/// Stores human review decisions separately from PLAN.md, keyed by milestone snapshot ID.
class ReviewStore {
  public:
    explicit ReviewStore(std::filesystem::path project_root);

    util::Result<util::Void> add_decision(const std::string &baseline,
                                          const std::string &decision) const;
    util::Result<std::vector<std::string>> decisions(const std::string &baseline) const;

  private:
    std::filesystem::path root_;
};

} // namespace platypus::core

#endif // PLATYPUS_CORE_REVIEW_STORE_HPP
