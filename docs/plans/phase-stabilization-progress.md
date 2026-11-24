# Stabilization progress

Branch: phase-stabilization. This is a verified development checkpoint, not project completion.

- [x] Audit source/tests/plans/ADRs and preserve existing work and AGENTS.md.
- [x] Restore executable, presets, dependency declarations and all original test registration.
- [x] Correct CLI invocation, backend ownership, tool recovery and bounded context.
- [x] Implement fail-closed approvals, containment, durable snapshots/diffs/undo and process limits.
- [x] Add profiles, real HTTP/SSE, SQLite resume and a functional FTXUI terminal.
- [x] Replace fabricated benchmarking with a verified CPU-only pipeline and statistical reporting.
- [x] Run 23 Python tests and dev/ASan/UBSan build/test commands.
- [x] Reconcile roadmap status and phase records with actual scope.
- [ ] Finish remaining Phase 0/1 items recorded in their progress files.
- [ ] Verify real model/server/native-template behavior and produce the human baseline.
- [ ] Implement and measure Phase 2 reliability, then Phases 3–5.

Final CTest checks: dev 9/9, ASan 9/9, UBSan 9/9 passed, including permission-bit and
empty-directory undo tests. Python: 23/23 passed.
Formatting and whitespace checks passed. A scoped clang-tidy run found outstanding diagnostics.
See phase-stabilization-report.md for exact commands, deviations and manual test protocol.

Next concrete work: native tool-history/profile checks,
fuzz/edit interfaces, and obtain a verified model/server for real measurements.
No measured model gain, macOS/GPU compatibility, sustained-use result or v1.0 readiness is claimed.

Continuation: baseline provenance/comparison gates implemented (ADR-009); 32 Python tests passed.
