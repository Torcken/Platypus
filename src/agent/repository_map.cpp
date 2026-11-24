#include "repository_map.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string_view>
#include <tree_sitter/api.h>

extern "C" {
const TSLanguage *tree_sitter_c();
const TSLanguage *tree_sitter_cpp();
const TSLanguage *tree_sitter_python();
const TSLanguage *tree_sitter_javascript();
const TSLanguage *tree_sitter_typescript();
const TSLanguage *tree_sitter_tsx();
const TSLanguage *tree_sitter_rust();
const TSLanguage *tree_sitter_go();
}

namespace platypus::agent {
namespace {
using Return = util::Result<std::string>;
constexpr std::uintmax_t kMaxSourceBytes = std::uintmax_t{256} * 1024;
constexpr std::uintmax_t kMaxTotalSourceBytes = std::uintmax_t{32} * 1024 * 1024;
constexpr std::size_t kMaxFilesystemEntries = 10000;
constexpr std::size_t kMaxSourceFiles = 2000;
constexpr std::size_t kMaxNodesPerFile = 50000;
constexpr std::size_t kMaxDeclarations = 10000;
constexpr std::size_t kMaxSignatureBytes = 200;

struct Language {
    const char *name;
    const char *extension;
    const TSLanguage *(*load)();
    std::set<std::string> declarations;
};

const std::vector<Language> &languages() {
    static const std::vector<Language> values{
        {"C",
         ".c",
         tree_sitter_c,
         {"function_definition", "struct_specifier", "enum_specifier", "type_definition"}},
        {"C++",
         ".cpp",
         tree_sitter_cpp,
         {"function_definition", "class_specifier", "struct_specifier", "enum_specifier",
          "namespace_definition", "type_definition"}},
        {"Python", ".py", tree_sitter_python, {"function_definition", "class_definition"}},
        {"JavaScript",
         ".js",
         tree_sitter_javascript,
         {"function_declaration", "generator_function_declaration", "class_declaration",
          "method_definition"}},
        {"TypeScript",
         ".ts",
         tree_sitter_typescript,
         {"function_declaration", "class_declaration", "abstract_class_declaration",
          "interface_declaration", "type_alias_declaration", "enum_declaration"}},
        {"TSX",
         ".tsx",
         tree_sitter_tsx,
         {"function_declaration", "class_declaration", "abstract_class_declaration",
          "interface_declaration", "type_alias_declaration", "enum_declaration"}},
        {"Rust",
         ".rs",
         tree_sitter_rust,
         {"function_item", "struct_item", "enum_item", "trait_item", "impl_item", "mod_item",
          "type_item"}},
        {"Go", ".go", tree_sitter_go, {"function_declaration", "method_declaration", "type_spec"}},
    };
    return values;
}

const Language *language_for(const std::filesystem::path &path) {
    auto ext = path.extension().string();
    if (ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx")
        return ext == ".h" ? &languages()[0] : &languages()[1];
    if (ext == ".cc" || ext == ".cxx" || ext == ".C")
        return &languages()[1];
    for (const auto &language : languages())
        if (ext == language.extension)
            return &language;
    if (ext == ".mjs" || ext == ".cjs" || ext == ".jsx")
        return &languages()[3];
    return nullptr;
}

std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> result;
    std::string word;
    unsigned char previous = 0;
    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            if (std::isupper(c) && !word.empty() &&
                (std::islower(previous) || std::isdigit(previous))) {
                result.push_back(std::move(word));
                word.clear();
            }
            word += static_cast<char>(std::tolower(c));
        } else if (!word.empty()) {
            result.push_back(std::move(word));
            word.clear();
        }
        previous = c;
    }
    if (!word.empty())
        result.push_back(std::move(word));
    return result;
}

std::string compact(std::string_view text) {
    std::string result;
    bool pending_space = false;
    for (unsigned char c : text) {
        if (std::isspace(c)) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space)
            result += ' ';
        pending_space = false;
        result += static_cast<char>(c);
        if (result.size() >= kMaxSignatureBytes)
            break;
    }
    return result;
}

std::string node_signature(TSNode node, const std::string &source) {
    auto start = ts_node_start_byte(node);
    auto body = ts_node_child_by_field_name(node, "body", 4);
    auto end = ts_node_is_null(body) ? ts_node_end_byte(node) : ts_node_start_byte(body);
    if (end <= start || end > source.size())
        return {};
    if (ts_node_is_null(body)) {
        auto line_end = source.find('\n', start);
        end = line_end == std::string::npos ? source.size() : line_end;
    }
    return compact(std::string_view(source).substr(start, end - start));
}

struct Declaration {
    std::string path;
    std::string language;
    std::string kind;
    std::string name;
    std::string signature;
    std::uint32_t line;
    double score = 0.0;
};

std::string identifier_from_declarator(TSNode node, const std::string &source,
                                       std::size_t depth = 0) {
    if (depth > 32)
        return {};
    const auto *type = ts_node_type(node);
    if (std::string_view(type) == "identifier" || std::string_view(type) == "field_identifier" ||
        std::string_view(type) == "property_identifier" ||
        std::string_view(type) == "namespace_identifier" ||
        std::string_view(type) == "type_identifier") {
        auto start = ts_node_start_byte(node);
        auto end = ts_node_end_byte(node);
        return end > start && end <= source.size() && end - start <= 200
                   ? source.substr(start, end - start)
                   : std::string{};
    }
    for (std::uint32_t i = 0; i < ts_node_named_child_count(node); ++i) {
        auto child = ts_node_named_child(node, i);
        auto found = identifier_from_declarator(child, source, depth + 1);
        if (!found.empty())
            return found;
    }
    return {};
}

std::string node_name(TSNode node, const std::string &source) {
    auto name = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name))
        name = ts_node_child_by_field_name(node, "declarator", 10);
    if (ts_node_is_null(name))
        return {};
    if (std::string_view(ts_node_type(name)) != "identifier" &&
        std::string_view(ts_node_type(name)) != "field_identifier" &&
        std::string_view(ts_node_type(name)) != "property_identifier" &&
        std::string_view(ts_node_type(name)) != "namespace_identifier" &&
        std::string_view(ts_node_type(name)) != "type_identifier")
        return identifier_from_declarator(name, source);
    auto start = ts_node_start_byte(name);
    auto end = ts_node_end_byte(name);
    if (end <= start || end > source.size() || end - start > 200)
        return {};
    return source.substr(start, end - start);
}

void index_file(const std::filesystem::path &root, const std::filesystem::path &path,
                const Language &language, std::vector<Declaration> &out,
                std::uintmax_t &total_source_bytes, const std::stop_token &stop) {
    if (stop.stop_requested())
        return;
    std::error_code error;
    auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > kMaxSourceBytes ||
        size > kMaxTotalSourceBytes - total_source_bytes)
        return;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return;
    std::string source(static_cast<std::size_t>(size), '\0');
    input.read(source.data(), static_cast<std::streamsize>(source.size()));
    if (input.gcount() != static_cast<std::streamsize>(source.size()))
        return;
    total_source_bytes += size;

    std::unique_ptr<TSParser, decltype(&ts_parser_delete)> parser(ts_parser_new(),
                                                                  ts_parser_delete);
    if (!parser || !ts_parser_set_language(parser.get(), language.load()))
        return;
    std::unique_ptr<TSTree, decltype(&ts_tree_delete)> tree(
        ts_parser_parse_string(parser.get(), nullptr, source.data(),
                               static_cast<std::uint32_t>(source.size())),
        ts_tree_delete);
    if (!tree)
        return;
    std::vector<TSNode> pending{ts_tree_root_node(tree.get())};
    std::size_t visited = 0;
    while (!pending.empty() && visited++ < kMaxNodesPerFile && out.size() < kMaxDeclarations &&
           !stop.stop_requested()) {
        auto node = pending.back();
        pending.pop_back();
        auto type = ts_node_type(node);
        if (language.declarations.contains(type)) {
            auto name = node_name(node, source);
            if (!name.empty()) {
                auto relative = std::filesystem::relative(path, root, error);
                if (!error)
                    out.push_back({relative.generic_string(), language.name, type, name,
                                   node_signature(node, source),
                                   ts_node_start_point(node).row + 1});
            }
        }
        auto child_count = ts_node_named_child_count(node);
        for (std::uint32_t i = child_count; i > 0; --i)
            pending.push_back(ts_node_named_child(node, i - 1));
    }
}

void add_weights(std::map<std::string, double> &weights, const std::string &text, double weight) {
    constexpr std::size_t kMaxTextBytes = 8192;
    auto bounded = std::string_view(text).substr(0, kMaxTextBytes);
    for (const auto &word : words(bounded))
        if (word.size() > 1)
            weights[word] = std::max(weights[word], weight);
}

double relevance(const Declaration &entry, const std::map<std::string, double> &weights) {
    std::set<std::string> identifiers;
    for (const auto &word : words(entry.path + " " + entry.name + " " + entry.signature))
        identifiers.insert(word);
    double score = 0;
    for (const auto &[word, weight] : weights)
        if (identifiers.contains(word))
            score += weight * (word == entry.name ? 3.0 : 1.0);
    return score;
}

bool excluded_directory(const std::filesystem::path &path) {
    const auto name = path.filename().string();
    return name == ".git" || name == ".platypus" || name == "build" || name.starts_with("build-") ||
           name == "node_modules" || name == ".cache" || name == "target" || name == "vendor" ||
           name == "dist" || name == "out" || name == "coverage" || name == ".venv" ||
           name == "venv";
}
} // namespace

util::Result<std::string> RepositoryMap::build(const std::filesystem::path &root,
                                               const std::string &request,
                                               const std::vector<std::string> &recent_context,
                                               std::size_t max_output_bytes,
                                               std::stop_token stop) try {
    if (max_output_bytes == 0)
        return Return::fail("Repository map output budget is empty");
    auto canonical_root = std::filesystem::canonical(root);
    if (!std::filesystem::is_directory(canonical_root))
        return Return::fail("Repository map root is not a directory");
    std::vector<Declaration> declarations;
    std::size_t file_count = 0;
    std::size_t entry_count = 0;
    std::uintmax_t total_source_bytes = 0;
    std::error_code error;
    std::filesystem::recursive_directory_iterator files(
        canonical_root, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && files != end && file_count < kMaxSourceFiles &&
           entry_count++ < kMaxFilesystemEntries) {
        if (stop.stop_requested())
            return Return::fail("Cancelled");
        const auto path = files->path();
        std::error_code status_error;
        if (files->is_symlink(status_error)) {
            files.disable_recursion_pending();
        } else if (files->is_directory(status_error)) {
            if (excluded_directory(path))
                files.disable_recursion_pending();
        } else if (files->is_regular_file(status_error)) {
            if (auto language = language_for(path)) {
                ++file_count;
                index_file(canonical_root, path, *language, declarations, total_source_bytes, stop);
                if (stop.stop_requested())
                    return Return::fail("Cancelled");
            }
        }
        files.increment(error);
    }
    if (stop.stop_requested())
        return Return::fail("Cancelled");

    std::map<std::string, double> weights;
    add_weights(weights, request, 1.0);
    auto recent_count = std::min<std::size_t>(4, recent_context.size());
    for (std::size_t i = 0; i < recent_count; ++i) {
        const auto distance = recent_count - i;
        add_weights(weights, recent_context[recent_context.size() - distance],
                    0.2 / static_cast<double>(distance));
    }
    for (auto &entry : declarations)
        entry.score = relevance(entry, weights);
    std::erase_if(declarations, [](const Declaration &entry) { return entry.score <= 0.0; });
    std::stable_sort(declarations.begin(), declarations.end(),
                     [](const auto &left, const auto &right) {
                         if (left.score != right.score)
                             return left.score > right.score;
                         if (left.path != right.path)
                             return left.path < right.path;
                         return left.line < right.line;
                     });

    std::string result = "Relevant declarations (ranked):\n";
    if (result.size() > max_output_bytes)
        return Return::fail("Repository map output budget is too small");
    for (const auto &entry : declarations) {
        std::ostringstream line;
        line << "- " << entry.path << ':' << entry.line << " [" << entry.language << ' '
             << entry.kind << "] " << entry.name;
        if (!entry.signature.empty())
            line << " — " << entry.signature;
        line << '\n';
        auto rendered = line.str();
        if (result.size() + rendered.size() > max_output_bytes)
            break;
        result += rendered;
    }
    if (result == "Relevant declarations (ranked):\n") {
        const std::string no_match = "(no declaration names matched the task terms)\n";
        if (result.size() + no_match.size() <= max_output_bytes)
            result += no_match;
    }
    return Return::ok(std::move(result));
} catch (const std::exception &error) {
    return Return::fail("Repository map failed: " + std::string(error.what()));
}
} // namespace platypus::agent
