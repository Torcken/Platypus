# Phase 3 Progress

- [x] Constrained `PLAN.md` Markdown schema, parser/renderer, atomic storage, task IDs, and import
  merge API with duplicate-ID remapping.
- [x] Interactive `/plan`, `/milestone`, `/task`, `/done`, and `/import` commands with explicit
  approval for mutations and project-contained import paths.
- [ ] Plan-first proposal flow with approve/edit/reject behavior.
- [ ] `/review` milestone bundle and comment-import command integration.
- [ ] Configurable automatic project checks with a three-attempt ceiling.
- [ ] Token-bounded milestone review bundle and comment import.
- [ ] Optional remote providers with estimate-before-send and fake transport tests.
- [ ] FakeBackend integration test for the full workflow cycle.
- [x] Dev, ASan, and UBSan suites (9/9 each), benchmark tests (41/41), and formatter checks pass.
  Focused clang-tidy reports one existing stop-token copy warning in `snapshot_store.hpp`.

Plan storage and basic interactive management are locally implemented. Plan-first execution,
checks, review bundles, provider integration and the Phase 3 exit cycle remain incomplete. Track
external model/provider and human workflow evidence separately from offline implementation and
tests.
