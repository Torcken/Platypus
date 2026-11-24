// Common flat-schema checking and safe text observations for every built-in tool.
// UTF-8 boundaries matter because observations later pass through JSON transport and SQLite.
#include "tool.hpp"
#include <algorithm>
#include <string_view>

namespace platypus::tools {
namespace {
/// Return a valid UTF-8 sequence length, or zero for an invalid/overlong/surrogate sequence.
std::size_t utf8_length(std::string_view text) {
    if (text.empty())
        return 0;
    auto lead = static_cast<unsigned char>(text[0]);
    std::size_t length = lead < 0x80                    ? 1
                         : lead >= 0xc2 && lead <= 0xdf ? 2
                         : lead >= 0xe0 && lead <= 0xef ? 3
                         : lead >= 0xf0 && lead <= 0xf4 ? 4
                                                        : 0;
    if (length == 0 || length > text.size())
        return 0;
    for (std::size_t i = 1; i < length; ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
            return 0;
    if (length >= 3) {
        auto second = static_cast<unsigned char>(text[1]);
        if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
            (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second > 0x8f))
            return 0;
    }
    return length;
}

/// Bound text without splitting code points; invalid bytes become Unicode replacement characters.
std::string text_prefix(std::string_view content, std::size_t limit, bool &truncated) {
    std::string output;
    std::size_t position = 0;
    while (position < content.size()) {
        auto length = utf8_length(content.substr(position));
        auto fragment =
            length ? content.substr(position, length) : std::string_view("\xef\xbf\xbd");
        if (fragment.size() > limit - output.size())
            break;
        output.append(fragment);
        position += length ? length : 1;
    }
    truncated = position != content.size();
    return output;
}

/// Keep status flags and a visible marker when either a tool or the common cap truncates text.
ToolResult bounded(ToolResult result, std::size_t limit) {
    bool capped = false;
    result.content = text_prefix(result.content, limit, capped);
    result.truncated = result.truncated || capped;
    constexpr std::string_view kMarker = "\n[output truncated]";
    if (result.truncated) {
        auto room = limit - std::min(limit, kMarker.size());
        result.content = text_prefix(result.content, room, capped);
        result.content += kMarker.substr(0, limit - result.content.size());
    }
    return result;
}

/// Check the flat schema subset used by Platypus, without implicit argument conversion.
std::string argument_error(const nlohmann::json &args, const nlohmann::json &schema) {
    if (!args.is_object())
        return "Arguments must be an object";
    for (const auto &required : schema.value("required", nlohmann::json::array()))
        if (!args.contains(required.get<std::string>()))
            return "Missing required argument: " + required.get<std::string>();
    auto properties = schema.value("properties", nlohmann::json::object());
    for (const auto &[name, value] : args.items()) {
        if (!properties.contains(name)) {
            if (!schema.value("additionalProperties", true))
                return "Unknown argument: " + name;
            continue;
        }
        auto type = properties.at(name).value("type", std::string{});
        bool valid =
            type.empty() || (type == "string" && value.is_string()) ||
            (type == "integer" && value.is_number_integer()) ||
            (type == "number" && value.is_number()) || (type == "boolean" && value.is_boolean()) ||
            (type == "object" && value.is_object()) || (type == "array" && value.is_array());
        if (!valid)
            return "Wrong type for argument: " + name;
    }
    return "";
}
} // namespace

Tool::Tool(std::string name, std::string description, nlohmann::json schema, Handler handler)
    : name_(std::move(name)), description_(std::move(description)), schema_(std::move(schema)),
      handler_(std::move(handler)) {
    if (schema_.is_string())
        schema_ = nlohmann::json::parse(schema_.get<std::string>());
}

ToolResult Tool::execute(const nlohmann::json &args, ToolContext &context) const {
    try {
        if (context.snapshots.stop_token().stop_requested())
            return bounded(ToolResult::fail("Cancelled"), context.max_output_bytes);
        auto error = argument_error(args, schema_);
        if (!error.empty())
            return bounded(ToolResult::fail(error), context.max_output_bytes);
        if (!handler_)
            return bounded(ToolResult::fail("Tool has no handler"), context.max_output_bytes);
        return bounded(handler_(args, context), context.max_output_bytes);
    } catch (const std::exception &error) {
        return bounded(ToolResult::fail(error.what()), context.max_output_bytes);
    }
}
} // namespace platypus::tools
