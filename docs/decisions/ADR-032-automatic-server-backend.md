# ADR-032: Automatic llama-server Backend Selection

Status: accepted

## Context

The Linux source installer built a CPU-only server unless the user manually selected CUDA or Vulkan.
The macOS build relies on llama.cpp's Metal default. This left first-run performance dependent on
the user knowing which backend to request.

## Decision

When `PLATYPUS_LLAMA_BACKEND` is unset or `auto`, select Metal on macOS. On Linux, select CUDA only
when both `nvcc` and a working `nvidia-smi -L` are available; otherwise select Vulkan when the Vulkan
development package, `glslc`, and a DRM render node are present; fall back to CPU. An explicit
`PLATYPUS_LLAMA_BACKEND` value continues to override detection.

## Consequences

The source installer chooses a backend for the machine on which it builds, then first-run setup uses
that installed server without another backend prompt. Selection is a pure helper with regression
coverage for CUDA/Vulkan priority, CPU fallback, Metal, and unsupported OSes. Automatic selection
depends on build tools and device nodes being visible during installation. The choice has not yet
been validated on the five target machines.
