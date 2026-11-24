#include "edit_format.hpp"
#include <regex>

namespace platypus::tools {
namespace {
class ExactEditFormat final : public EditFormat {
  public:
    util::Result<std::string> apply(const std::string &content, const std::string &old_text,
                                    const std::string &new_text) const override {
        if (old_text.empty())
            return util::Result<std::string>::fail("old_string is required");
        auto result = content;
        std::size_t position = 0;
        bool matched = false;
        while ((position = result.find(old_text, position)) != std::string::npos) {
            result.replace(position, old_text.size(), new_text);
            position += new_text.size();
            matched = true;
        }
        if (!matched)
            return util::Result<std::string>::fail("No matching text");
        return util::Result<std::string>::ok(std::move(result));
    }
};

class RegexEditFormat final : public EditFormat {
  public:
    util::Result<std::string> apply(const std::string &content, const std::string &old_text,
                                    const std::string &new_text) const override {
        if (old_text.empty())
            return util::Result<std::string>::fail("old_string is required");
        try {
            std::regex pattern(old_text);
            if (!std::regex_search(content, pattern))
                return util::Result<std::string>::fail("No matching text");
            return util::Result<std::string>::ok(std::regex_replace(content, pattern, new_text));
        } catch (const std::regex_error &error) {
            return util::Result<std::string>::fail("Invalid regular expression: " +
                                                   std::string(error.what()));
        }
    }
};
} // namespace

std::unique_ptr<const EditFormat> make_edit_format(const std::string &name) {
    if (name == "exact")
        return std::make_unique<ExactEditFormat>();
    if (name == "regex")
        return std::make_unique<RegexEditFormat>();
    return nullptr;
}
} // namespace platypus::tools
