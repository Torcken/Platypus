// libFuzzer entry point for all tool-call syntax families; never executes a parsed tool.
// The normal regression suite also invokes this entry with deterministic seed mutations.
// Compile separately with sanitizer coverage for an actual fuzz campaign.
#include "core/codec.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

/// Assert parser invariants for arbitrary bytes, including embedded NUL and invalid UTF-8.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    const std::string text(reinterpret_cast<const char *>(data), size);
    static const nlohmann::json kSchemas = {
        {"read",
         {{"properties",
           {{"path", {{"type", "string"}}}, {"start_line", {{"type", "integer"}}}}}}}};
    for (const auto *syntax : {"json_tagged", "qwen_xml", "gemma"}) {
        auto calls = platypus::core::Codec::parse_tool_calls(text, syntax, kSchemas);
        if (calls.size() > 64)
            std::abort();
        for (const auto &call : calls) {
            if (call.tool_name.empty() || !call.arguments.is_object())
                std::abort();
            // Serialization must stay valid for the next HTTP request and session observation.
            static_cast<void>(call.arguments.dump());
        }
    }
    return 0;
}
