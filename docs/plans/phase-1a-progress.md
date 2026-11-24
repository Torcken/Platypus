# Phase 1a progress

- [x] CMake dev/ASan/UBSan, pinned CPM dependencies, compiled jthread check, executable and tests.
- [x] Actual libcurl HTTP/SSE, /completion, /health, /tokenize, stop-token cancellation.
- [x] Validated TOML profiles and client-side role formatting; no model-name branches.
- [x] Ornith/Gemma syntax parsing, reasoning policy, prefix-stability regression tests.
- [x] Typed tool observations, native declarations/responses and CLI fixture sessions (ADR-010).
- [x] Refuse invalid profile types, incomplete reasoning and non-finite sampling values.
- [x] Joined generation/UI workers and platform-local bounded subprocess execution.
- [x] Formatting configuration and Linux/macOS CI workflow written.
- [ ] Run CI, especially macOS toolchain discovery; production clang-tidy cleanup remains.
- [ ] Verify native tool-history/template parity against real models and per-tier settings.
- [x] Replay Phase 0 JSONL fixtures with explicit exhaustion and malformed-input errors.
- [ ] Complete canonical API migration.
  agent::ContextManager and tools::Tool/ToolResult are migrated; SnapshotStore remains.

Not a completed phase: source-derived profiles and local fixtures are not hardware acceptance.
