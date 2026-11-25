# Phase 4 Progress

- [x] Resolve Windows v1.0 support before platform work (ADR-025).
- [x] Define catalog v1, validate pinned HF artifacts and SHA-256 values, and test deterministic
  RAM/VRAM-aware fast/balanced/quality ranking (ADR-026). The memory floors and ranks remain
  estimates pending hardware and model-quality evidence.
- [ ] Add Linux/macOS CPU, memory, GPU architecture, and VRAM detection with graceful fallbacks.
- [ ] Implement resumable, bounded HTTPS downloads with progress and SHA-256 checks.
- [ ] Manage the pinned llama-server lifecycle, readiness, bounded OOM restart, and shutdown.
- [ ] Add setup wizard, Linux installer, Homebrew formula, update command, and distribution docs.
- [ ] Extend CI for the supported Linux/macOS release targets and supported backend builds.
- [ ] Run all local verification presets and document remote CI outcomes.
- [ ] Human: verify unsupported-by-default installation on five machines, including AMD 6 GB,
  M1 16 GB, and Titan V.

No Phase 4 implementation has started. Continue with catalog schema and recommendation tests.
