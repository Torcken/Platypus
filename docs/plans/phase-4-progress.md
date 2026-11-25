# Phase 4 Progress

- [x] Resolve Windows v1.0 support before platform work (ADR-025).
- [x] Define catalog v1, validate pinned HF artifacts and SHA-256 values, and test deterministic
  RAM/VRAM-aware fast/balanced/quality ranking (ADR-026). The memory floors and ranks remain
  estimates pending hardware and model-quality evidence.
- [ ] Add Linux/macOS CPU, memory, GPU architecture, and VRAM detection with graceful fallbacks.
  In progress: CPU/RAM and architecture inventory, optional Vulkan device-local heap probe, Linux
  DRM VRAM fallback, NVML NVIDIA name/architecture/VRAM probing, deterministic primary-GPU choice,
  native macOS Metal unified-memory capacity, and GPU-absent tests are implemented. Supported-host
  hardware validation remains; Metal compilation awaits the macOS CI run.
- [ ] Implement resumable, bounded HTTPS downloads with progress and SHA-256 checks.
  In progress: catalog-only Hugging Face HTTPS URLs, size cap, resumable `.part` files, HTTPS-only
  redirects, progress and cancellation callbacks, OpenSSL SHA-256 verification, and atomic
  no-overwrite finalization are implemented with offline transfer-seam tests and wired into both
  `models download` and `setup`. Full real-model download acceptance remains.
- [ ] Manage the pinned llama-server lifecycle, readiness, bounded OOM restart, and shutdown.
  In progress: reproc-backed child ownership, loopback-only launch, bounded readiness polling,
  explicit bounded OOM recovery by the caller (each restart halves context to a configured floor),
  and guaranteed shutdown are implemented with fake process and health probes. `run
  --managed-model` integrates process start/readiness/shutdown into CLI runs. A source build script
  now builds and installs the ADR-002-pinned CPU server, with opt-in CUDA/Vulkan configuration; its
  Linux CPU binary was built and version-checked locally. The caller still must classify model-load
  errors, GPU server builds and hosted CI remain unverified.
- [ ] Add setup wizard, Linux installer, Homebrew formula, update command, and distribution docs.
  In progress: `platypus models list`, `recommend`, and `download` use the validated catalog,
  detected host facts, and verified downloader; source-tree and install catalog lookup are wired.
  A local Linux x86_64 source installer builds both Platypus and the pinned server; the installation
  guide documents CPU/CUDA/Vulkan choices. `platypus setup` now
  recommends, downloads and verifies the selected model, loads its profile, and enters a managed
  interactive session. Its full download/server path still needs a real-model installation test.
  Homebrew formula, release updater, and assisted-free acceptance remain.
- [ ] Extend CI for the supported Linux/macOS release targets and supported backend builds.
  Existing Linux GCC/Clang/sanitizer and macOS arm64 jobs now provision OpenSSL Crypto; an optional
  Vulkan-header Linux job and install/catalog smoke step were added. Hosted CI has not been
  triggered from this environment.
- [x] Run all local verification presets: dev, ASan, UBSan (9/9 CTest tests each) and Python (41 tests).
- [ ] Document remote CI outcomes when supported-host workflows are available.
- [ ] Human: verify unsupported-by-default installation on five machines, including AMD 6 GB,
  M1 16 GB, and Titan V.

The catalog and recommendation work is complete. Hardware detection is partially implemented as
described above; the remaining Phase 4 deliverables are still outstanding.
