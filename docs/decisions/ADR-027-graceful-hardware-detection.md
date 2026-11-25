# ADR-027: Graceful Hardware Detection

## Context

The model catalog must remain useful on machines without a GPU driver, while Phase 4 targets Linux
x86_64 and macOS arm64. GPU APIs and drivers are optional and may be absent in CI or headless
systems.

## Decision

Always detect CPU architecture, logical processor count, and physical system memory using portable
host interfaces. On Linux, dynamically probe NVML first for NVIDIA device name, architecture, and
memory; fall back to optional Vulkan enumeration and DRM sysfs VRAM totals. On macOS, use Metal's
recommended working set as the unified-memory GPU capacity hint. Keep GPU enumeration injectable
for tests. Return host facts even when no graphics device can be queried.

## Consequences

CPU-only recommendations remain available without GPU libraries. Vulkan device-local heaps are
capacity hints; integrated/UMA devices may report shared system memory, as defined by the
[Khronos Vulkan memory model](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMemoryProperties.html).
NVML's device query interface supplies name, architecture, and total memory; the driver library is
loaded dynamically and remains optional ([NVIDIA NVML API reference](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html)).
Supported-host hardware acceptance is still open; no hardware acceptance is implied.
