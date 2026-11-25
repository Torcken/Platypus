// Resumable HTTPS transport and artifact verification.
#include "model_download.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <curl/curl.h>
#include <fstream>
#include <memory>
#include <openssl/evp.h>
#include <system_error>

namespace platypus::platform {
namespace {
constexpr std::uint64_t kMaximumModelBytes = 100ULL * 1024 * 1024 * 1024;

bool valid_model(const ModelVariant &model) {
    constexpr std::string_view prefix = "https://huggingface.co/";
    if (!model.url.starts_with(prefix) || model.url.find_first_of("\r\n?#@") != std::string::npos ||
        model.size_hint_bytes == 0 || model.size_hint_bytes > kMaximumModelBytes ||
        model.sha256.size() != 64)
        return false;
    const auto resolve = model.url.find("/resolve/", prefix.size());
    if (resolve == std::string::npos)
        return false;
    const auto repository = model.url.substr(prefix.size(), resolve - prefix.size());
    const auto owner_separator = repository.find('/');
    if (owner_separator == std::string::npos || owner_separator == 0 ||
        owner_separator + 1 == repository.size() ||
        repository.find('/', owner_separator + 1) != std::string::npos ||
        repository.find("..") != std::string::npos)
        return false;
    const auto revision = resolve + std::string_view("/resolve/").size();
    const auto slash = model.url.find('/', revision);
    if (slash == std::string::npos || slash - revision != 40 || slash + 1 == model.url.size())
        return false;
    const auto is_hex = [](unsigned char ch) { return std::isxdigit(ch) != 0; };
    return std::all_of(model.url.begin() + static_cast<std::ptrdiff_t>(revision),
                       model.url.begin() + static_cast<std::ptrdiff_t>(slash), is_hex) &&
           model.url.substr(slash + 1).find("..") == std::string::npos &&
           model.url.substr(slash + 1).find('\\') == std::string::npos &&
           std::all_of(model.sha256.begin(), model.sha256.end(), is_hex);
}

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

struct TransferState {
    std::ofstream file;
    std::uint64_t received = 0;
    std::uint64_t maximum = 0;
    std::uint64_t offset = 0;
    long response_status = 0;
    DownloadProgress progress;
    std::stop_token stop;
};

util::Result<std::uint64_t> curl_transfer(const std::string &url, const std::filesystem::path &path,
                                          std::uint64_t offset, std::uint64_t maximum,
                                          const DownloadProgress &progress, std::stop_token stop) {
    static CurlGlobal global;
    (void)global;
    TransferState state;
    state.received = offset;
    state.maximum = maximum;
    state.offset = offset;
    state.progress = progress;
    state.stop = stop;
    state.file.open(path, std::ios::binary | std::ios::app);
    if (!state.file)
        return util::Result<std::uint64_t>::fail("Cannot open model partial file");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl)
        return util::Result<std::uint64_t>::fail("Cannot initialize download client");
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXFILESIZE_LARGE,
                     static_cast<curl_off_t>(maximum - offset));
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    if (offset != 0) {
        const auto range = std::to_string(offset) + "-";
        curl_easy_setopt(curl.get(), CURLOPT_RANGE, range.c_str());
    }
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &state);
    curl_easy_setopt(
        curl.get(), CURLOPT_HEADERFUNCTION,
        +[](char *data, size_t size, size_t count, void *opaque) -> size_t {
            if (size != 0 && count > SIZE_MAX / size)
                return 0;
            const auto bytes = size * count;
            std::string_view line(data, bytes);
            if (line.starts_with("HTTP/")) {
                const auto separator = line.find(' ');
                if (separator != std::string_view::npos) {
                    long status = 0;
                    const auto start = line.data() + separator + 1;
                    const auto end = line.data() + line.size();
                    const auto parsed = std::from_chars(start, end, status);
                    if (parsed.ec == std::errc{})
                        static_cast<TransferState *>(opaque)->response_status = status;
                }
            }
            return bytes;
        });
    curl_easy_setopt(
        curl.get(), CURLOPT_WRITEFUNCTION,
        +[](char *data, size_t size, size_t count, void *opaque) -> size_t {
            auto &current = *static_cast<TransferState *>(opaque);
            if (size != 0 && count > SIZE_MAX / size)
                return 0;
            const auto bytes = size * count;
            const auto expected_status = current.offset == 0 ? 200 : 206;
            if (current.response_status != expected_status || current.stop.stop_requested() ||
                bytes > current.maximum - std::min(current.received, current.maximum))
                return 0;
            current.file.write(data, static_cast<std::streamsize>(bytes));
            if (!current.file)
                return 0;
            current.received += bytes;
            if (current.progress)
                current.progress(current.received, current.maximum);
            return bytes;
        });
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(
        curl.get(), CURLOPT_XFERINFOFUNCTION,
        +[](void *opaque, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
            auto &current = *static_cast<TransferState *>(opaque);
            if (current.stop.stop_requested())
                return 1;
            if (total > 0 && static_cast<std::uint64_t>(total) > current.maximum)
                return 1;
            if (current.progress && now >= 0)
                current.progress(current.received, current.maximum);
            return 0;
        });
    const auto result = curl_easy_perform(curl.get());
    state.file.close();
    if (result != CURLE_OK)
        return util::Result<std::uint64_t>::fail(
            stop.stop_requested() ? "Download cancelled" : curl_easy_strerror(result));
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if ((offset == 0 && status != 200) || (offset != 0 && status != 206))
        return util::Result<std::uint64_t>::fail("Download server returned HTTP " +
                                                 std::to_string(status));
    return util::Result<std::uint64_t>::ok(state.received);
}

util::Result<std::string> sha256_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return util::Result<std::string>::fail("Cannot read downloaded model");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),
                                                                    EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
        return util::Result<std::string>::fail("Cannot initialize SHA-256 verification");
    std::array<char, 1024 * 1024> buffer{};
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytes = file.gcount();
        if (bytes > 0 &&
            EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(bytes)) != 1)
            return util::Result<std::string>::fail("Cannot calculate model SHA-256");
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (!file.eof() || EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1 ||
        length != 32)
        return util::Result<std::string>::fail("Cannot finish model SHA-256 verification");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (unsigned int i = 0; i < length; ++i) {
        result.push_back(hex[digest[i] >> 4]);
        result.push_back(hex[digest[i] & 0x0f]);
    }
    return util::Result<std::string>::ok(std::move(result));
}
} // namespace

util::Result<std::filesystem::path> download_model(const ModelVariant &model,
                                                   const std::filesystem::path &destination,
                                                   DownloadProgress progress, std::stop_token stop,
                                                   DownloadTransfer transfer) {
    using Return = util::Result<std::filesystem::path>;
    if (!valid_model(model) || destination.empty())
        return Return::fail("Download requires a pinned HTTPS model and a destination");
    if (std::filesystem::exists(destination))
        return Return::fail("Model destination already exists");
    std::error_code error;
    if (!destination.parent_path().empty())
        std::filesystem::create_directories(destination.parent_path(), error);
    if (error)
        return Return::fail("Cannot create model directory: " + error.message());
    auto partial = destination;
    partial += ".part";
    const auto partial_status = std::filesystem::symlink_status(partial, error);
    if (!error && std::filesystem::is_symlink(partial_status))
        return Return::fail("Model partial path cannot be a symlink");
    error.clear();
    const auto offset =
        std::filesystem::exists(partial) ? std::filesystem::file_size(partial, error) : 0;
    if (error || offset > model.size_hint_bytes)
        return Return::fail("Model partial file exceeds the catalog size bound");
    if (!transfer)
        transfer = curl_transfer;
    if (offset != model.size_hint_bytes) {
        auto transferred =
            transfer(model.url, partial, offset, model.size_hint_bytes, progress, stop);
        if (!transferred.is_ok())
            return Return::fail(transferred.error());
        if (transferred.value() != model.size_hint_bytes)
            return Return::fail("Downloaded model size does not match the catalog");
    }
    const auto actual_size = std::filesystem::file_size(partial, error);
    if (error || actual_size != model.size_hint_bytes)
        return Return::fail("Downloaded model size does not match the catalog");
    auto hash = sha256_file(partial);
    auto expected_hash = model.sha256;
    std::transform(expected_hash.begin(), expected_hash.end(), expected_hash.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (!hash.is_ok() || hash.value() != expected_hash) {
        std::filesystem::remove(partial, error);
        return Return::fail("Downloaded model SHA-256 does not match the catalog");
    }
    std::filesystem::create_hard_link(partial, destination, error);
    if (error)
        return Return::fail("Cannot finalize downloaded model: " + error.message());
    std::filesystem::remove(partial, error);
    return Return::ok(destination);
}

} // namespace platypus::platform
