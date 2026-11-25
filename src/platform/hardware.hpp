// Cross-platform hardware facts used to filter the model catalog.
// Optional GPU probes may fail; CPU and system memory still support local recommendations.
#ifndef PLATYPUS_PLATFORM_HARDWARE_HPP
#define PLATYPUS_PLATFORM_HARDWARE_HPP

#include "../util/result.hpp"
#include "model_catalog.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace platypus::platform {

/// A discovered graphics processor and its reported device-local memory.
struct GpuInfo {
    std::string name;
    std::string vendor;
    std::uint64_t memory_bytes;
    std::string detection_method;
};

/// Host CPU and memory information plus the best detected GPU, if a driver is available.
struct HardwareInventory {
    std::string cpu_architecture;
    unsigned int logical_cpu_count;
    std::uint64_t system_memory_bytes;
    std::optional<GpuInfo> primary_gpu;

    /// Convert detected values to the catalog's RAM/VRAM constraints.
    HardwareFacts catalog_facts() const;
};

using GpuEnumerator = std::function<std::vector<GpuInfo>()>;

/// Detect host facts; a missing GPU driver does not fail the result.
util::Result<HardwareInventory> detect_hardware(GpuEnumerator gpu_enumerator = {});

/// Choose the GPU with the largest reported memory; name breaks ties deterministically.
std::optional<GpuInfo> select_primary_gpu(const std::vector<GpuInfo> &devices);

} // namespace platypus::platform

#endif
