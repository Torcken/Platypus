#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerEditFormatTestOneInput(const std::uint8_t *data, std::size_t size);
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    return LLVMFuzzerEditFormatTestOneInput(data, size);
}
