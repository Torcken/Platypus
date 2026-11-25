# Phase 4 Progress

- [x] Resolve Windows v1.0 support before platform work (ADR-025).
- [x] Define catalog v1, validate pinned HF artifacts and SHA-256 values, and test deterministic
  RAM/VRAM-aware fast/balanced/quality ranking (ADR-026). The memory floors and ranks remain
  estimates pending hardware and model-quality evidence.
- [ ] Add Linux/macOS CPU, memory, GPU architecture, and VRAM detection with graceful fallbacks.
  In progress: CPU/RAM and architecture inventory, optional Vulkan device-local heap probe, Linux
  DRM VRAM fallback, deterministic primary-GPU choice, and GPU-absent tests are implemented. NVML,
  native macOS Metal/unified-memory reporting, and supported-host hardware validation remain.
- [ ] Implement resumable, bounded HTTPS downloads with progress and SHA-256 checks.
- [ ] Manage the pinned llama-server lifecycle, readiness, bounded OOM restart, and shutdown.
- [ ] Add setup wizard, Linux installer, Homebrew formula, update command, and distribution docs.
- [ ] Extend CI for the supported Linux/macOS release targets and supported backend builds.
- [x] Run all local verification presets: dev, ASan, UBSan (9/9 CTest tests each) and Python (41 tests).
- [ ] Document remote CI outcomes when supported-host workflows are available.
- [ ] Human: verify unsupported-by-default installation on five machines, including AMD 6 GB,
  M1 16 GB, and Titan V.

The catalog and recommendation work is complete. Hardware detection is partially implemented as
described above; the remaining Phase 4 deliverables are still outstanding.
