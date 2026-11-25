// Human review decisions are bounded private metadata keyed to immutable milestone baselines.
#include "review_store.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <unistd.h>

namespace platypus::core {
namespace {
constexpr std::size_t kMaxDecisionBytes = 1024;
constexpr std::size_t kMaxDecisionsPerMilestone = 50;
constexpr std::size_t kMaxReviewStoreBytes = std::size_t{128} * 1024;

bool valid_baseline(const std::string &id) {
    return (id.size() == 40 || id.size() == 64) && std::all_of(id.begin(), id.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

bool valid_decision(const std::string &text) {
    return !text.empty() && text.size() <= kMaxDecisionBytes &&
           std::none_of(text.begin(), text.end(),
                        [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

nlohmann::json empty_document() { return {{"version", 1}, {"reviews", nlohmann::json::array()}}; }

util::Result<nlohmann::json> load_document(const std::filesystem::path &project_root) try {
    using Return = util::Result<nlohmann::json>;
    auto root = std::filesystem::canonical(project_root);
    auto state = root / ".platypus";
    std::error_code error;
    auto state_status = std::filesystem::symlink_status(state, error);
    if (error == std::errc::no_such_file_or_directory)
        return Return::ok(empty_document());
    if (error || std::filesystem::is_symlink(state_status) ||
        !std::filesystem::is_directory(state_status))
        return Return::fail("Unsafe .platypus review storage");
    auto path = state / "review-decisions.json";
    auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return Return::ok(empty_document());
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
        return Return::fail("Unsafe review decision file");
    auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaxReviewStoreBytes)
        return Return::fail("Review decision file exceeds the storage limit");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return Return::fail("Cannot read review decisions");
    std::string contents(static_cast<std::size_t>(size), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(contents.size()))
        return Return::fail("Review decision file changed while being read");
    auto document = nlohmann::json::parse(contents);
    if (!document.is_object() || document.value("version", 0) != 1 ||
        !document.contains("reviews") || !document["reviews"].is_array() ||
        document["reviews"].size() > kMaxDecisionsPerMilestone * 100)
        return Return::fail("Invalid review decision metadata");
    for (const auto &review : document["reviews"]) {
        if (!review.is_object() || !review.contains("baseline") ||
            !review["baseline"].is_string() || !review.contains("decisions") ||
            !review["decisions"].is_array() ||
            review["decisions"].size() > kMaxDecisionsPerMilestone ||
            !valid_baseline(review["baseline"].get<std::string>()))
            return Return::fail("Invalid milestone review record");
        for (const auto &decision : review["decisions"])
            if (!decision.is_string() || !valid_decision(decision.get<std::string>()))
                return Return::fail("Invalid milestone decision");
    }
    return Return::ok(std::move(document));
} catch (const std::exception &error) {
    return util::Result<nlohmann::json>::fail("Cannot load review decisions: " +
                                              std::string(error.what()));
}

util::Result<util::Void> save_document(const std::filesystem::path &project_root,
                                       const nlohmann::json &document) try {
    using Return = util::Result<util::Void>;
    auto contents = document.dump(2);
    if (contents.size() > kMaxReviewStoreBytes)
        return Return::fail("Review decisions exceed the storage limit");
    auto root = std::filesystem::canonical(project_root);
    auto state = root / ".platypus";
    std::error_code error;
    auto state_status = std::filesystem::symlink_status(state, error);
    if (error == std::errc::no_such_file_or_directory) {
        error.clear();
        std::filesystem::create_directory(state, error);
        if (error)
            return Return::fail("Cannot create review metadata directory: " + error.message());
        std::filesystem::permissions(state, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, error);
        if (error)
            return Return::fail("Cannot secure review metadata directory: " + error.message());
        state_status = std::filesystem::symlink_status(state, error);
    }
    if (error || std::filesystem::is_symlink(state_status) ||
        !std::filesystem::is_directory(state_status))
        return Return::fail("Unsafe .platypus review storage");
    auto destination = state / "review-decisions.json";
    auto destination_status = std::filesystem::symlink_status(destination, error);
    if (!error && (std::filesystem::is_symlink(destination_status) ||
                   !std::filesystem::is_regular_file(destination_status)))
        return Return::fail("Unsafe review decision file");
    if (error != std::errc::no_such_file_or_directory && error)
        return Return::fail("Cannot inspect review decisions: " + error.message());
    auto temporary = (state / ".review-decisions.tmp.XXXXXX").string();
    int descriptor = ::mkstemp(temporary.data());
    if (descriptor < 0)
        return Return::fail("Cannot create review decision file: " +
                            std::string(std::strerror(errno)));
    (void)::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
    auto cleanup = [&] {
        (void)::close(descriptor);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    };
    if (::fchmod(descriptor, 0600) != 0) {
        auto message = std::string(std::strerror(errno));
        cleanup();
        return Return::fail("Cannot secure review decision file: " + message);
    }
    std::size_t offset = 0;
    while (offset < contents.size()) {
        auto written = ::write(descriptor, contents.data() + offset, contents.size() - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            auto message = std::string(std::strerror(errno));
            cleanup();
            return Return::fail("Cannot write review decision file: " + message);
        }
        offset += static_cast<std::size_t>(written);
    }
    if (::fsync(descriptor) != 0) {
        auto message = std::string(std::strerror(errno));
        cleanup();
        return Return::fail("Cannot sync review decision file: " + message);
    }
    if (::close(descriptor) != 0) {
        descriptor = -1;
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Return::fail("Cannot close review decision file");
    }
    descriptor = -1;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        auto message = error.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Return::fail("Cannot replace review decisions: " + message);
    }
    return Return::ok();
} catch (const std::exception &error) {
    return util::Result<util::Void>::fail("Cannot store review decisions: " +
                                          std::string(error.what()));
}
} // namespace

ReviewStore::ReviewStore(std::filesystem::path project_root) : root_(std::move(project_root)) {}

util::Result<util::Void> ReviewStore::add_decision(const std::string &baseline,
                                                   const std::string &decision) const {
    using Return = util::Result<util::Void>;
    if (!valid_baseline(baseline) || !valid_decision(decision))
        return Return::fail("Invalid milestone baseline or decision text");
    auto loaded = load_document(root_);
    if (!loaded.is_ok())
        return Return::fail(loaded.error());
    auto document = loaded.value();
    auto &reviews = document["reviews"];
    auto review = std::find_if(reviews.begin(), reviews.end(),
                               [&](const auto &entry) { return entry["baseline"] == baseline; });
    if (review == reviews.end()) {
        reviews.push_back({{"baseline", baseline}, {"decisions", {decision}}});
    } else {
        if ((*review)["decisions"].size() >= kMaxDecisionsPerMilestone)
            return Return::fail("Milestone decision limit reached");
        (*review)["decisions"].push_back(decision);
    }
    return save_document(root_, document);
}

util::Result<std::vector<std::string>> ReviewStore::decisions(const std::string &baseline) const {
    using Return = util::Result<std::vector<std::string>>;
    if (!valid_baseline(baseline))
        return Return::fail("Invalid milestone baseline");
    auto loaded = load_document(root_);
    if (!loaded.is_ok())
        return Return::fail(loaded.error());
    for (const auto &review : loaded.value()["reviews"])
        if (review["baseline"] == baseline)
            return Return::ok(review["decisions"].get<std::vector<std::string>>());
    return Return::ok({});
}

} // namespace platypus::core
