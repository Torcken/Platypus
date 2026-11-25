# Phase 3 Progress

- [x] Constrained `PLAN.md` Markdown schema, parser/renderer, atomic storage, task IDs, and import
  merge API with duplicate-ID remapping.
- [x] Interactive `/plan`, `/milestone`, `/task`, `/done`, and `/import` commands with explicit
  approval for mutations and project-contained import paths.
- [ ] Plan-first proposal flow with approve/edit/reject behavior.
- Proposal generation now has a bounded structured-output API and FakeBackend regression coverage;
  CLI/TUI approval, editing, persistence, and execution wiring remain.
- [x] `/review` emits a bounded diff/plan bundle for the latest milestone baseline.
- [x] Persist latest-task check statuses in private `.platypus` metadata and include them in review
  bundles (ADR-021).
- [ ] Capture review decisions, copy bundles to clipboard, and import review comments as new tasks.
- [x] Detect CMake/Cargo/npm/Go/pytest checks, load `.platypus.toml` argv overrides, require
  approval, return untrusted failures to the model, and cap repair attempts at three (ADR-021).
- [ ] Optional remote providers with estimate-before-send and fake transport tests.
- [ ] FakeBackend integration test for the full workflow cycle.
- [x] Dev, ASan, and UBSan suites (9/9 each), benchmark tests (41/41), and formatter checks pass.
  Focused clang-tidy is not clean: it reports existing logger, registry, and `SnapshotStore`
  warnings plus a stop-token-by-value convention warning in the new check API.

Plan storage and basic interactive management are locally implemented. Plan-first execution,
checks, review bundles, provider integration and the Phase 3 exit cycle remain incomplete. Track
external model/provider and human workflow evidence separately from offline implementation and
tests.
