# Phase 4 Progress

- [x] Resolve Windows v1.0 support before platform work (ADR-025).
- [x] Define catalog v1, validate pinned HF artifacts and SHA-256 values, and test deterministic
  RAM/VRAM-aware fast/balanced/quality ranking (ADR-026, ADR-030). The catalog includes four
  Ornith 1.0 variants and Gemma 4 12B Q4_0. Memory floors and ranks remain estimates pending
  hardware and model-quality evidence. On the current host, the CLI recommends Ornith Q4_K_M for
  fast and balanced, and reports that no quality model fits.
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
  startup OOM diagnostics are continuously drained into a 64 KiB in-memory tail and matching
  pinned allocation errors trigger one context-halving retry by default (ADR-031). Generic startup
  exits fail without retry. Unit and CLI end-to-end fake-child tests cover these paths. Guaranteed
  shutdown is implemented with fake process and health probes. `run
  --managed-model` integrates process start/readiness/shutdown into CLI runs. A source build script
  now builds the ADR-002-pinned server with automatic Metal/CUDA/Vulkan/CPU selection and explicit
  backend overrides (ADR-032); its Linux CPU binary was built and version-checked locally. Runtime
  inference OOM errors, GPU server builds, and hosted CI remain unverified.
- [ ] Add setup wizard, Linux installer, Homebrew formula, update command, and distribution docs.
  In progress: `platypus models list`, `recommend`, and `download` use the validated catalog,
  detected host facts, and verified downloader; source-tree and install catalog lookup are wired.
  A local Linux x86_64 source installer builds both Platypus and the pinned server; the installation
  guide documents CPU/CUDA/Vulkan choices. `platypus setup` now
  recommends, downloads and verifies the selected model, loads its profile, and enters a managed
  interactive session. Its full download/server path still needs a real-model installation test.
  The pinned-server build selects Metal, CUDA, Vulkan, or CPU from the build host by default;
  explicit backend overrides remain available. Homebrew formula, release updater, and assisted-free
  acceptance remain. A temporary-prefix Linux installer run built and installed both binaries; its
  smoke wrapper stopped because it compared llama-server's `1.0.0-dev` version label with Platypus's
  `0.1.0`. Direct CLI checks pass: `platypus --version` prints `0.1.0`, and `platypus models list`
  lists all five catalog entries. The CI install/catalog assertions also pass locally against a
  temporary prefix using the dev build.
- [ ] Extend CI for the supported Linux/macOS release targets and supported backend builds.
  Existing Linux GCC/Clang/sanitizer and macOS arm64 jobs now provision OpenSSL Crypto; an optional
  Linux Vulkan job builds the pinned server with Vulkan; CUDA 12.9.1 and CUDA 13.0.0 container jobs
  build the pinned CUDA server; macOS arm64 also builds it with the native Metal backend, alongside
  the pinned CPU server and install/catalog smoke step. The Linux pinned CPU job now runs the full
  source installer and checks the installed CLI version, catalog, and server revision. The locally
  installed Platypus binary and model catalog smoke check pass. Hosted CI outcomes and supported GPU
  execution remain unverified.
- [x] Run all local verification presets: dev, ASan, UBSan (10/10 CTest tests each) and Python (41 tests).
- [ ] Document remote CI outcomes when supported-host workflows are available.
- [ ] Human: verify unsupported-by-default installation on five machines, including AMD 6 GB,
  M1 16 GB, and Titan V.

The catalog and recommendation work is complete. Hardware detection is partially implemented as
described above; the remaining Phase 4 deliverables are still outstanding.
