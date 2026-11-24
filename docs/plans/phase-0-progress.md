# Phase 0 progress

Status: implementation substantially restored; human exit criterion remains pending.

- [x] HTTP/SSE recording proxy, timing reassembly, redaction, JSONL retention.
- [x] Disposable repository/HOME, subprocess timeouts, maintained verification, measured metrics.
- [x] OpenCode, aider, fake and Platypus subprocess adapters; aider flags are UNVERIFIED.
- [x] Five runnable maintained tasks with verifier negative/positive tests.
- [x] Ten edit fixtures and three experiment formats with real application/validation.
- [x] Wilson reporting, paired per-task bootstrap, null/n/a handling.
- [x] Full CPU-only pipeline test: runner → proxy → fake server/client → verifier → JSONL → report.
- [x] Independent native-call scoring and deduplicated edit errors; CLI/proxy fixtures cover
  JSON, Qwen XML and Gemma without changing the model request (ADR-011).
- [x] Excluded warm-ups and seeded task/client randomization for matrix runs.
- [x] Gate baseline publication on complete metadata, checked local artifact hashes/client version,
  matching task sets and compatible serving settings (ADR-009). Remote serving facts remain human-verified.
- [x] Profile sampling settings propagate to the actual chat, SSE and raw backend requests (ADR-016).
- [ ] Verify real client/server compatibility, cross-client sampling equivalence and all timing
  semantics against the pinned server.
- [ ] Human: supply fifteen project tasks and produce the real-model baseline report.

The previous "completed and verified" statement referred to placeholder behavior and is withdrawn.
See phase-stabilization-report.md for actual command results and limitations.
