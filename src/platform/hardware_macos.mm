// Native Metal inventory for Apple silicon's unified GPU memory.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "hardware.hpp"

namespace platypus::platform {

std::vector<GpuInfo> macos_metal_gpus() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil)
            return {};
        const auto memory_bytes = static_cast<std::uint64_t>(device.recommendedMaxWorkingSetSize);
        return {{device.name.UTF8String ? device.name.UTF8String : "Apple GPU", "Apple",
                 memory_bytes, "Metal recommended working set"}};
    }
}

} // namespace platypus::platform
