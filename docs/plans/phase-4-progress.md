# Phase 4 Progress

## Review fixes — 2026-11-25

The repository and `phase-4` branch are public. Tag `v0.1.0` points to `d0eaf5f`; its
release verification failed, so no successful release is claimed. No versioned release
bundles are published yet.

This review fixes fresh llama.cpp clones being rejected as dirty, allows no modified
source at the pinned revision, installs ripgrep in CI, resolves installed catalogs
relative to the executable, and retains updater recovery files if rollback fails.
Interruptions now attempt restoration as well. See [the review record](review-2026-11-25.md).

Verification after these fixes: dev, ASan, and UBSan each passed 10/10 CTest tests;
41 benchmark tests and 9 installer/updater tests passed. Hosted Linux GCC/Clang and sanitizer
jobs passed. Hosted macOS exposed test timeout assumptions, now hardened locally. Loopback
fixture tests required execution outside the filesystem/network sandbox. Current hosted outcomes
and follow-up fixes are recorded below.

Hosted run `45708539468` on `673e4fa` passed Linux GCC, Clang, ASan, UBSan, fuzz, and the macOS
client build, CTest, and Python suites. Remaining failures exposed two CI assumptions: llama-server
prints its version to stderr, and CUDA binaries cannot be executed on runners without NVIDIA driver
libraries. CI now checks the embedded pinned revision in the built artifact. The macOS installer
also exposed a missing `<memory>` include in the pinned Metal source under the Homebrew LLVM toolchain;
the build script now supplies that header without modifying the pinned checkout. Hosted validation
of these corrections is pending. Locally, dev CTest passed 10/10, updater tests passed 9/9, shell
syntax and Python compilation passed.


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
  backend overrides (ADR-032); its Linux CPU binary was built and version-checked locally. Managed
  inference requests now classify HTTP and child diagnostics and retry once at reduced context only
  before any streamed output escapes (ADR-033). Fake HTTP/process tests cover retry bounds and
  partial-stream no-replay. Real-model OOM reproduction, GPU server builds, and hosted CI remain
  unverified.
- [ ] Complete setup wizard, source installers, private release/update path, Homebrew formula, and
  distribution docs.
  In progress: `platypus models list`, `recommend`, and `download` use the validated catalog,
  detected host facts, and verified downloader; source-tree and install catalog lookup are wired.
  The Linux x86_64 installer has passed a local temporary-prefix build and install. A macOS arm64
  installer is added and awaits the hosted macOS CI run; the installation guide documents both
  paths. `platypus setup` now
  recommends, downloads and verifies the selected model, loads its profile, starts a managed server,
  and runs a read-only README summary task before the interactive session. A failed sample task is
  reported while setup still opens the session. Its full download/server/sample path still needs a
  real-model installation test.
  The pinned-server build selects Metal, CUDA, Vulkan, or CPU from the build host by default;
  explicit backend overrides remain available. The canonical source URL is
  `https://github.com/Torcken/Platypus` and is recorded in CMake metadata and installation docs.
  Versioned release bundles and `platypus update` are implemented for Linux CPU, Vulkan,
  CUDA 12/13, and macOS Metal. The release workflow gates publishing on Linux verification, checks
  the project tag, packages each target, emits SHA-256 checksums, and publishes the assets; offline
  tests cover archive validation and rollback. A successful tag workflow and published release have
  not yet been verified. The repository is public and `phase-4` is pushed; `main` remains separate.
  Homebrew and assisted-free macOS setup remain open.
  Assisted-free acceptance remains. A temporary-prefix Linux installer run built and installed both
  binaries; its
  smoke wrapper stopped because it compared llama-server's `1.0.0-dev` version label with Platypus's
  `0.1.0`. Direct CLI checks pass: `platypus --version` prints `0.1.0`, and `platypus models list`
  lists all five catalog entries. The CI install/catalog assertions also pass locally against a
  temporary prefix using the dev build.
- [ ] Validate CI for the supported Linux/macOS release targets and supported backend builds.
  Existing Linux GCC/Clang/sanitizer and macOS arm64 jobs now provision OpenSSL Crypto; an optional
  Linux Vulkan job builds the pinned server with Vulkan; CUDA 12.9.1 and CUDA 13.0.0 container jobs
  build the pinned CUDA server; macOS arm64 also builds it with the native Metal backend, alongside
  the pinned CPU server and install/catalog smoke step. The Linux pinned CPU job now runs the full
  source installer and checks the installed CLI version, catalog, and server revision. The locally
  installed Platypus binary and model catalog smoke check pass. Hosted CI outcomes and supported GPU
  execution remain unverified. A tag-triggered release workflow now covers the five distribution
  targets; it awaits a tagged run on GitHub.
- [x] Run all local verification presets: dev, ASan, UBSan (10/10 CTest tests each) and Python.
  All three CTest presets passed 10/10; Python passed 41 benchmark tests and 6 private release/update
  tests. Fake HTTP/process tests verify one reduced-context retry and no replay after
  partial output. Real-model OOM and setup sample execution remain unverified.
- [ ] Document final remote CI outcomes after the corrected macOS test timeouts and revision checks
  pass on the supported-host workflow.
- [ ] Human: verify unsupported-by-default installation on five machines, including AMD 6 GB,
  M1 16 GB, and Titan V.

The catalog and recommendation work is complete. Hardware detection is partially implemented as
described above; the remaining Phase 4 deliverables are still outstanding.
