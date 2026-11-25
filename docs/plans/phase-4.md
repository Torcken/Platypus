# Phase 4: Zero Configuration and Distribution

## Objective

Make Platypus discover compatible hardware, recommend a verified local model, download it safely,
manage its inference server, and provide supported installation paths for Linux x86_64 and macOS
arm64.

## Work Sequence

1. Record the Windows v1.0 support decision (ADR-025). Keep OS-specific detection and process work
   behind `src/platform/` interfaces.
2. Define and validate a versioned model catalog with source revision, model/profile URLs, SHA-256,
   supported backend, and minimum hardware tier. Add deterministic fast/balanced/quality
   recommendation tests using injectable hardware facts.
3. Detect CPU, RAM, GPU architecture, and VRAM on supported platforms. Hardware detection must
   degrade to unknown values when optional GPU drivers are absent.
4. Add resumable downloads with progress callbacks, strict HTTPS/host validation, size bounds, and
   SHA-256 verification. Use fake HTTP/filesystem seams for unit and interruption tests.
5. Manage the pinned llama-server process: readiness check, bounded restart on out-of-memory, and
   cleanup on shutdown. Keep manual endpoint mode available.
6. Add first-run setup, Linux install script, Homebrew formula, updater, and the Linux/macOS CI
   matrix. CI must validate artifacts without downloading multi-gigabyte models.

## Verification and Exit

Every catalog, recommendation, download, and lifecycle test must run without GPU or network access.
Run dev, ASan, UBSan, and Python suites. Report CI jobs that cannot be triggered locally.
The phase's human exit criterion remains a successful assisted-free install on five machines,
including AMD 6 GB, M1 16 GB, and Titan V hardware.
