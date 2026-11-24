#include "tool_grammar.hpp"
#include <algorithm>
#include <set>
#include <sstream>

namespace platypus::llm {
namespace {
using Json = nlohmann::json;

bool identifier(const std::string &value) {
    if (value.empty())
        return false;
    auto first = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    auto rest = [&](unsigned char c) { return first(c) || (c >= '0' && c <= '9'); };
    return first(static_cast<unsigned char>(value.front())) &&
           std::all_of(value.begin() + 1, value.end(),
                       [&](char c) { return rest(static_cast<unsigned char>(c)); });
}

std::string literal(const std::string &value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"')
            result += '\\';
        if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            result += "\\r";
        else if (c == '\t')
            result += "\\t";
        else
            result += c;
    }
    return result + '"';
}

std::string value_rule(const std::string &type, const std::string &syntax) {
    if (type == "string") {
        if (syntax == "qwen_xml")
            return "qwen-string";
        if (syntax == "gemma")
            return "gemma-string";
        return "json-string";
    }
    if (type == "integer")
        return "integer";
    if (type == "number")
        return "number";
    if (type == "boolean")
        return "boolean";
    throw std::runtime_error("Unsupported tool grammar property type: " + type);
}

std::string argument_rule_name(std::size_t index) {
    std::string suffix;
    do {
        suffix.insert(suffix.begin(), static_cast<char>('a' + index % 26));
        index /= 26;
    } while (index != 0);
    return "arguments-" + suffix;
}

std::string arguments_rule(const Json &schema, const std::string &syntax) {
    if (!schema.is_object() || schema.value("type", "") != "object")
        throw std::runtime_error("Tool parameters must have object type");
    const auto properties = schema.value("properties", Json::object());
    const auto required = schema.value("required", Json::array());
    if (!properties.is_object() || !required.is_array() || properties.size() > 5)
        throw std::runtime_error("Tool schema exceeds flat grammar limits");
    std::set<std::string> required_names;
    for (const auto &name : required) {
        if (!name.is_string() || !properties.contains(name.get<std::string>()))
            throw std::runtime_error("Required tool property is not declared");
        required_names.insert(name.get<std::string>());
    }
    std::vector<std::pair<std::string, std::string>> fields;
    for (const auto &[name, property] : properties.items()) {
        if (!identifier(name) || !property.is_object() || !property.contains("type") ||
            !property.at("type").is_string())
            throw std::runtime_error("Invalid flat tool property declaration");
        fields.emplace_back(name, value_rule(property.at("type").get<std::string>(), syntax));
    }

    const bool json = syntax == "json_tagged";
    const std::string open = json ? "{ " : "";
    const std::string close = json ? " }" : "";
    const std::string separator = (json || syntax == "gemma") ? " , " : " ";
    std::vector<std::string> alternatives;
    const auto combinations = std::size_t{1} << fields.size();
    for (std::size_t mask = 0; mask < combinations; ++mask) {
        std::vector<std::size_t> order;
        bool missing_required = false;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            bool selected = (mask & (std::size_t{1} << i)) != 0;
            if (selected)
                order.push_back(i);
            else if (required_names.contains(fields[i].first))
                missing_required = true;
        }
        if (missing_required)
            continue;
        do {
            std::string rule = open;
            for (std::size_t i = 0; i < order.size(); ++i) {
                if (i)
                    rule += separator;
                const auto &[name, value] = fields[order[i]];
                if (json)
                    rule += literal(name) + " ws \":\" ws ";
                else if (syntax == "qwen_xml")
                    rule += literal("<parameter=" + name + ">");
                else
                    rule += name + ":";
                rule += value;
                if (!json && syntax == "qwen_xml")
                    rule += literal("</parameter>");
            }
            rule += close;
            alternatives.push_back(std::move(rule));
        } while (std::next_permutation(order.begin(), order.end()));
    }
    if (alternatives.empty())
        throw std::runtime_error("Tool schema has no valid required-property combination");
    std::ostringstream result;
    result << "arguments ::= ";
    for (std::size_t i = 0; i < alternatives.size(); ++i) {
        if (i)
            result << " | ";
        result << alternatives[i];
    }
    result << '\n';
    return result.str();
}
} // namespace

util::Result<std::string> ToolGrammar::generate(const nlohmann::json &tools,
                                                const std::string &syntax) try {
    if (!tools.is_array() || tools.empty())
        return util::Result<std::string>::fail("Tool grammar requires at least one tool");
    if (syntax != "json_tagged" && syntax != "qwen_xml" && syntax != "gemma")
        return util::Result<std::string>::fail("Unsupported tool grammar syntax: " + syntax);

    std::ostringstream calls;
    std::set<std::string> names;
    std::vector<std::string> alternatives;
    std::size_t tool_index = 0;
    for (const auto &tool : tools) {
        const auto &function = tool.at("function");
        const auto name = function.at("name").get<std::string>();
        if (!identifier(name) || !names.insert(name).second)
            return util::Result<std::string>::fail("Invalid or duplicate tool name");
        auto arguments = arguments_rule(function.at("parameters"), syntax);
        const auto rule_name = argument_rule_name(tool_index++);
        calls << rule_name << " ::= " << arguments.substr(std::string("arguments ::= ").size());
        std::ostringstream call;
        if (syntax == "json_tagged")
            call << literal("{\"tool_call\":{\"name\":\"") << name << literal("\",\"arguments\":")
                 << ' ' << rule_name << ' ' << literal("}}");
        else if (syntax == "qwen_xml") {
            std::string prefix = "<tool_call><function=";
            prefix += name;
            prefix += '>';
            call << literal(prefix) << ' ' << rule_name << ' '
                 << literal("</function></tool_call>");
        } else {
            std::string prefix = "<|tool_call>call:";
            prefix += name;
            prefix += '{';
            call << literal(prefix) << ' ' << rule_name << ' ' << literal("}<tool_call|>");
        }
        alternatives.push_back(call.str());
    }

    std::ostringstream grammar;
    grammar << "root ::= ";
    for (std::size_t i = 0; i < alternatives.size(); ++i) {
        if (i)
            grammar << " | ";
        grammar << alternatives[i];
    }
    grammar
        << '\n'
        << "ws ::= [ \\t\\n\\r]*\n"
        << R"(json-string ::= "\"" ( [^"\\\x00-\x1F] | "\\" ["\\/bfnrt] | "\\" "u" [0-9a-fA-F]{4} )* "\"")"
        << '\n'
        << "qwen-string ::= [^<]*\n"
        << "gemma-string ::= \"<|\\\"|>\" [^<]* \"<|\\\"|>\"\n"
        << "integer ::= \"-\"? (\"0\" | [1-9] [0-9]*)\n"
        << "number ::= integer (\".\" [0-9]+)? ([eE] [+-]? [0-9]+)?\n"
        << "boolean ::= \"true\" | \"false\"\n"
        << calls.str();
    return util::Result<std::string>::ok(grammar.str());
} catch (const std::exception &error) {
    return util::Result<std::string>::fail(error.what());
}
} // namespace platypus::llm
