// Queries basic host resources and optional graphics APIs for zero-configuration setup.
// Vulkan and Linux DRM sysfs are probes only; lack of either never blocks CPU use.
#include "hardware.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <thread>
#include <unistd.h>
#include <utility>

#ifdef PLATYPUS_HAVE_VULKAN
#include <dlfcn.h>
#include <vulkan/vulkan.h>
#endif

#ifdef __linux__
#include <filesystem>
#endif

#ifdef __APPLE__
namespace platypus::platform {
std::vector<GpuInfo> macos_metal_gpus();
}
#endif

namespace platypus::platform {
namespace {
std::string cpu_architecture() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

util::Result<std::uint64_t> system_memory() {
    const auto pages = sysconf(_SC_PHYS_PAGES);
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page_size <= 0 ||
        static_cast<std::uint64_t>(pages) >
            std::numeric_limits<std::uint64_t>::max() / static_cast<std::uint64_t>(page_size))
        return util::Result<std::uint64_t>::fail("System memory size is unavailable");
    return util::Result<std::uint64_t>::ok(static_cast<std::uint64_t>(pages) *
                                           static_cast<std::uint64_t>(page_size));
}

#ifdef PLATYPUS_HAVE_VULKAN
class VulkanLibrary {
  public:
    VulkanLibrary() {
#if defined(__APPLE__)
        constexpr const char *names[] = {"libvulkan.1.dylib", "libvulkan.dylib",
                                         "libMoltenVK.dylib"};
#else
        constexpr const char *names[] = {"libvulkan.so.1", "libvulkan.so"};
#endif
        for (const auto *name : names) {
            handle_ = dlopen(name, RTLD_NOW | RTLD_LOCAL);
            if (handle_)
                break;
        }
    }

    ~VulkanLibrary() {
        if (handle_)
            dlclose(handle_);
    }

    VulkanLibrary(const VulkanLibrary &) = delete;
    VulkanLibrary &operator=(const VulkanLibrary &) = delete;

    template <typename Function> Function symbol(const char *name) const {
        return handle_ ? reinterpret_cast<Function>(dlsym(handle_, name)) : nullptr;
    }

  private:
    void *handle_ = nullptr;
};

std::string vendor_name(std::uint32_t vendor_id) {
    switch (vendor_id) {
    case 0x10de:
        return "NVIDIA";
    case 0x1002:
    case 0x1022:
        return "AMD";
    case 0x8086:
        return "Intel";
    case 0x106b:
        return "Apple";
    default:
        return "GPU vendor " + std::to_string(vendor_id);
    }
}

std::vector<GpuInfo> vulkan_gpus() {
    std::vector<GpuInfo> result;
    VulkanLibrary library;
    const auto create_instance = library.symbol<PFN_vkCreateInstance>("vkCreateInstance");
    const auto destroy_instance = library.symbol<PFN_vkDestroyInstance>("vkDestroyInstance");
    const auto enumerate_devices =
        library.symbol<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
    const auto get_properties =
        library.symbol<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties");
    const auto get_memory = library.symbol<PFN_vkGetPhysicalDeviceMemoryProperties>(
        "vkGetPhysicalDeviceMemoryProperties");
    if (!create_instance || !destroy_instance || !enumerate_devices || !get_properties ||
        !get_memory)
        return result;
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Platypus";
    application.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &application;
    VkInstance instance = VK_NULL_HANDLE;
    if (create_instance(&create_info, nullptr, &instance) != VK_SUCCESS)
        return result;
    struct InstanceOwner {
        VkInstance value;
        PFN_vkDestroyInstance destroy;
        ~InstanceOwner() { destroy(value, nullptr); }
    } owner{instance, destroy_instance};
    std::uint32_t count = 0;
    if (enumerate_devices(instance, &count, nullptr) != VK_SUCCESS || count == 0 || count > 64)
        return result;
    std::vector<VkPhysicalDevice> devices(count);
    if (enumerate_devices(instance, &count, devices.data()) != VK_SUCCESS)
        return result;
    for (auto device : devices) {
        VkPhysicalDeviceProperties properties{};
        get_properties(device, &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
            continue;
        VkPhysicalDeviceMemoryProperties memory{};
        get_memory(device, &memory);
        std::uint64_t local_bytes = 0;
        for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
            const auto &heap = memory.memoryHeaps[i];
            if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0 &&
                local_bytes <= std::numeric_limits<std::uint64_t>::max() - heap.size)
                local_bytes += heap.size;
        }
        result.push_back(
            {properties.deviceName, vendor_name(properties.vendorID), local_bytes, "Vulkan"});
    }
    return result;
}
#endif

#ifdef __linux__
std::vector<GpuInfo> linux_drm_gpus() {
    namespace fs = std::filesystem;
    std::vector<GpuInfo> result;
    std::error_code error;
    const fs::path cards("/sys/class/drm");
    if (!fs::is_directory(cards, error))
        return result;
    for (const auto &entry : fs::directory_iterator(cards, error)) {
        if (error)
            break;
        const auto name = entry.path().filename().string();
        if (!name.starts_with("card") || name.size() == 4 ||
            !std::all_of(name.begin() + 4, name.end(),
                         [](unsigned char ch) { return ch >= '0' && ch <= '9'; }))
            continue;
        std::ifstream memory_file(entry.path() / "device/mem_info_vram_total");
        std::uint64_t memory_bytes = 0;
        if (!(memory_file >> memory_bytes) || memory_bytes == 0)
            continue;
        std::string vendor_id;
        std::ifstream vendor_file(entry.path() / "device/vendor");
        vendor_file >> vendor_id;
        std::string vendor = vendor_id == "0x1002" ? "AMD" : "Unknown";
        std::string gpu_name = vendor + " GPU (DRM)";
        std::ifstream product_file(entry.path() / "device/product_name");
        std::string product_name;
        if (std::getline(product_file, product_name) && !product_name.empty())
            gpu_name = product_name;
        result.push_back({std::move(gpu_name), std::move(vendor), memory_bytes, "DRM sysfs"});
    }
    return result;
}
#endif

std::vector<GpuInfo> system_gpus() {
#ifdef __APPLE__
    auto metal_devices = macos_metal_gpus();
    if (!metal_devices.empty())
        return metal_devices;
#endif
#ifdef PLATYPUS_HAVE_VULKAN
    auto devices = vulkan_gpus();
    if (!devices.empty())
        return devices;
#endif
#ifdef __linux__
    return linux_drm_gpus();
#elif defined(__APPLE__)
    return {};
#else
    return {};
#endif
}
} // namespace

HardwareFacts HardwareInventory::catalog_facts() const {
    return {system_memory_bytes, primary_gpu ? primary_gpu->memory_bytes : 0};
}

std::optional<GpuInfo> select_primary_gpu(const std::vector<GpuInfo> &devices) {
    if (devices.empty())
        return std::nullopt;
    const auto selected =
        std::max_element(devices.begin(), devices.end(), [](const auto &left, const auto &right) {
            if (left.memory_bytes != right.memory_bytes)
                return left.memory_bytes < right.memory_bytes;
            return left.name > right.name;
        });
    return *selected;
}

util::Result<HardwareInventory> detect_hardware(GpuEnumerator gpu_enumerator) {
    auto memory = system_memory();
    if (!memory.is_ok())
        return util::Result<HardwareInventory>::fail(memory.error());
    auto devices = gpu_enumerator ? gpu_enumerator() : system_gpus();
    return util::Result<HardwareInventory>::ok({cpu_architecture(),
                                                std::thread::hardware_concurrency(), memory.value(),
                                                select_primary_gpu(devices)});
}

} // namespace platypus::platform
