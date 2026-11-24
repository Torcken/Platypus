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

Streaming continuation: llama-server SSE now assembles fragmented native tool-call batches by index,
rejects incomplete batches, and returns canonical execution content without exposing it as visible
text. Added tests for multiple calls, fragmented names/arguments, optional SSE spacing and incomplete
batches. Verification after this change: Python 41/41; dev, ASan and UBSan CTest each 9/9. The two
sanitizer suites require unsandboxed execution for LeakSanitizer and loopback sockets.

Search continuation: `grep` now invokes ripgrep with an argv vector (no shell interpolation), respects
the recursive flag, excludes Platypus/Git state, and preserves capped-output status. README records
the runtime `rg` dependency. Dev, ASan and UBSan each passed 9/9 CTest groups; Python passed 41/41.

Edit continuation: a typed `EditFormat` strategy now owns exact and regex replacement behavior,
while the existing `use_regex` argument remains supported. Unknown formats (including fuzzy) fail
without a write; fuzzy rollout awaits the phase-0 model comparison. Dev, ASan and UBSan each passed
9/9 CTest groups; Python passed 41/41.

Sampling continuation (ADR-016): strict profile fields now propagate to generic chat, SSE and raw
completion requests; the benchmark request recorder observes those actual values. Focused profile
and loopback request tests pass. Real-server equivalence remains unverified.

Edit fuzz continuation: an exact-replacement libFuzzer entry, seed corpus, CI smoke campaign and
2,000 deterministic regression mutations are in place. Regex is excluded because `std::regex` does
not guarantee bounded runtime. Local coverage-guided execution remains pending Clang/libFuzzer.

Phase 2 continuation (ADR-017): the agent blocks the fourth identical tool action within a task,
returns a corrective observation, and permits recovery with a different call. FakeBackend verifies
the guard and successful recovery. Paired model-quality impact is not measured.

Phase 2 prefix diagnostics (ADR-018): opt-in `PLATYPUS_DEBUG_PREFIX=1` compares the rendered stable
system portion between generations and warns on changes. Formatter and loopback HTTP tests cover
unchanged and changed prefixes; this is a prompt diagnostic, not a KV-cache performance result.

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

Tool continuation (ADR-013): canonical Tool/ToolResult/ToolContext, registry and built-ins migrated
to src/tools/. Success and error text is capped inside a UTF-8 boundary; invalid bytes are replaced,
and error/truncation flags survive session persistence. Dev, ASan and UBSan configure/build and
CTest each passed 9/9; Python unittest passed 41/41. One intermediate overlapping dev rebuild
failed to link; the subsequent build and complete verification passed. SnapshotStore is next.

Snapshot continuation (ADR-014): canonical SnapshotStore, durable IDs/list/restore, stale approval
rejection, manifest/ID validation and failed-restore recovery implemented. All canonical interfaces
are now present with compatibility includes for original callers. Dev/ASan/UBSan configure/build
and CTest each passed 9/9; Python unittest passed 41/41. Remaining foundation work: EditFormat,
fuzzy selection evidence, ripgrep, fuzzing, streaming wire-call parity, sampling propagation, lint
and external model/CI/usage checks. Clang/libFuzzer is not installed in this environment.

Parser continuation (ADR-015): bounded literal native scanning, full Gemma batches, duplicate/depth
checks, no embedded-JSON fallback and a shared fuzz entry implemented. Added seeds, optional
Clang/libFuzzer target and CI smoke job. Dev/ASan/UBSan configure/build and CTest each passed 9/9,
including 2,000 deterministic fuzz-entry mutations; Python passed 41/41. The GCC compiler guard
was checked in a disposable build. Actual libFuzzer and remote CI remain unverified.
