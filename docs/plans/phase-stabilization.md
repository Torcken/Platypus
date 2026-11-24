# Stabilization and completion plan

The human requested implementation through project completion on 2026-11-24, following the
repository audit. This authorizes implementation and progression through the roadmap; the old
Phase 0 assignment and review-only role no longer constrain this assignment. Preserve existing
uncommitted work and AGENTS.md. Work sequentially, with no sub-agents or remote model use.

## Implementation order

1. Restore the C++ build, dev/ASan/UBSan presets, pinned dependencies, executable, and meaningful
   tests. Repair existing tests without dropping their behavioral checks.
2. Finish the Phase 0 measurement pipeline: HTTP/SSE recording, real subprocess adapters,
   disposable task copies, verification, metrics, edit experiments, and statistical reporting.
3. Complete Phase 1 inference, profiles/codecs, bounded context, safe tools, durable snapshots,
   undo, sessions, cancellation, terminal UI, and benchmark adapter.
4. Implement Phase 2 reliability features behind explicit experimental controls until paired
   real-model measurements satisfy the roadmap's promotion rule.
5. Complete Phase 3 plan/import/check/review workflow and optional remote protocols.
6. Implement Phase 4 setup, catalog/download/server lifecycle, packaging and CI; then Phase 5
   permissions, localization, documentation and explicit failure export.

Each numbered roadmap phase retains its own plan/progress/report. Phase 6 remains conditional,
after v1.0, and requires benchmark evidence rather than speculative implementation.

## Interfaces and verification

Converge on the canonical interfaces in CLAUDE.md: LlmBackend, tools::Tool, ContextManager, and
SnapshotStore. Preserve compatibility during migration where needed to keep existing tests.
Use Catch2 v3 and fake HTTP/backend fixtures. Run Python unittest and all three CMake presets.
Run integration tests in disposable repositories; never delete local benchmark results in tests.

## Completion evidence

Software completion and human exit criteria are separate. Do not claim real-model improvements,
two weeks without lost work, five-machine installation, beta outcomes, or macOS/GPU compatibility
without the corresponding evidence. Document exact manual protocols and unresolved checks.
