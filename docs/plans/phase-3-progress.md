# Phase 3 Progress

- [x] Constrained `PLAN.md` Markdown schema, parser/renderer, atomic storage, task IDs, and import
  merge API with duplicate-ID remapping.
- [x] Interactive `/plan`, `/milestone`, `/task`, `/done`, and `/import` commands with explicit
  approval for mutations and project-contained import paths.
- [x] Plan-first proposal flow is the CLI/TUI default, with approve/edit/reject decisions, persisted
  task steps, sequential execution, and completion updates; `--direct` and `/direct` preserve an
  explicit direct-request path.
- [x] `/review` emits a bounded diff/plan bundle for the latest milestone baseline.
- [x] Persist latest-task check statuses in private `.platypus` metadata and include them in review
  bundles (ADR-021).
- [x] Record milestone decisions in private `.platypus` metadata and import approved Markdown
  review bullets as unchecked tasks.
- [ ] Copy review bundles to the clipboard.
- [x] Detect CMake/Cargo/npm/Go/pytest checks, load `.platypus.toml` argv overrides, require
  approval, return untrusted failures to the model, and cap repair attempts at three (ADR-021).
- [ ] Optional remote providers with estimate-before-send and fake transport tests.
- [x] FakeBackend integration test covers the full offline plan → execute → check → review →
  decision → comment-import cycle.
- [x] Dev, ASan, and UBSan suites (9/9 each), benchmark tests (41/41), and formatter checks pass.
  Focused clang-tidy is not clean: it reports logger enum/stream, registry vector-growth,
  `SnapshotStore`/backend/engine stop-token, and TUI event-parameter suggestions.

Plan storage and basic interactive management are locally implemented. Plan-first execution,
checks, review bundles, provider integration and the Phase 3 exit cycle remain incomplete. Track
external model/provider and human workflow evidence separately from offline implementation and
tests.
