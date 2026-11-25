#ifndef PLATYPUS_PLATFORM_CLIPBOARD_HPP
#define PLATYPUS_PLATFORM_CLIPBOARD_HPP

#include "util/result.hpp"
#include <string>
#include <vector>

namespace platypus::platform {

/// Copy bounded plain text to the native desktop clipboard using an argv-only provider.
util::Result<util::Void> copy_text(const std::string &text);

/// Start a clipboard-style stdin consumer; exposed so tests can use a local fake provider.
util::Result<util::Void> copy_text_with_command(const std::vector<std::string> &argv,
                                                const std::string &text);

} // namespace platypus::platform

#endif // PLATYPUS_PLATFORM_CLIPBOARD_HPP
