# ADR-027: Graceful Hardware Detection

## Context

The model catalog must remain useful on machines without a GPU driver, while Phase 4 targets Linux
x86_64 and macOS arm64. GPU APIs and drivers are optional and may be absent in CI or headless
systems.

## Decision

Always detect CPU architecture, logical processor count, and physical system memory using portable
host interfaces. When Vulkan is available at build time, enumerate physical devices and sum
device-local heaps. On Linux, fall back to DRM sysfs VRAM totals if Vulkan returns no GPU. Keep GPU
enumeration injectable for tests. Return host facts even when no graphics device can be queried.

## Consequences

CPU-only recommendations remain available without GPU libraries. Vulkan device-local heaps are
capacity hints; integrated/UMA devices may report shared system memory, as defined by the
[Khronos Vulkan memory model](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMemoryProperties.html).
NVIDIA-specific NVML and native macOS Metal/unified-memory probes remain Phase 4 work; no hardware
acceptance is implied.
