// Strategy boundary for search-and-replace semantics. Fuzzy matching stays disabled until
// the phase-0 model experiment provides comparable evidence.
#pragma once
#include "util/result.hpp"
#include <memory>
#include <string>

namespace platypus::tools {
class EditFormat {
  public:
    virtual ~EditFormat() = default;
    virtual util::Result<std::string> apply(const std::string &content, const std::string &old_text,
                                            const std::string &new_text) const = 0;
};

std::unique_ptr<const EditFormat> make_edit_format(const std::string &name);
} // namespace platypus::tools
