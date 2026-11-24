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

Native history continuation (ADR-010): typed tool observations, native declarations and response
frames, schema-aware XML strings, reasoning-safe execution and strict profile validation are
implemented. CLI fixture sessions verify both native families and append-only prompt prefixes.
Verification: configure/build/CTest for dev, ASan and UBSan each passed (9/9 CTest groups);
`python3 -m unittest discover -s bench/tests -v` passed 32/32. Real models remain unverified.

Native benchmark continuation (ADR-011): optional proxy metadata, independent strict call scoring
and deduplicated edit-error observations implemented. Full CLI/proxy tests cover all three syntax
families, a failed edit/retry and known token timings. Verification: dev/ASan/UBSan configure/build
and CTest each passed 9/9; Python unittest passed 41/41. No real-model result is claimed.

Context continuation (ADR-012): canonical agent::ContextManager, native prompt tokenization,
explicit generic estimates, reply reservations and a mutex-protected terminal status implemented.
Boundary/cancellation/error tests and a task-submitting PTY test pass. Verification: configure,
build and CTest on dev/ASan/UBSan each passed 9/9; Python unittest passed 41/41. Next: canonical
Tool/ToolResult and SnapshotStore, edit/fuzz interfaces, and remaining lint/real-model checks.
