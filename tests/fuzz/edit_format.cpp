// Coverage-guided entry for the deterministic exact edit strategy. Regex is excluded because
// std::regex does not guarantee bounded runtime for attacker-controlled patterns.
#include "tools/edit_format.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

extern "C" int LLVMFuzzerEditFormatTestOneInput(const std::uint8_t *data, std::size_t size) {
    if (size > 1024)
        return 0;
    auto bytes = std::string(reinterpret_cast<const char *>(data), size);
    auto first = size / 3;
    auto second = 2 * size / 3;
    auto old_text = bytes.substr(0, first);
    auto content = bytes.substr(first, second - first);
    auto new_text = bytes.substr(second);
    auto format = platypus::tools::make_edit_format("exact");
    if (!format)
        std::abort();
    auto result = format->apply(content, old_text, new_text);
    if (!result.is_ok() && result.error().empty())
        std::abort();
    return 0;
}
