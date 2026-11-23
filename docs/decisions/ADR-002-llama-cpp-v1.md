# ADR-002: Pin llama.cpp v1.0.0

**Status:** accepted — 2026-11-20

## Context

The benchmark proxy and later backend depend on llama-server request fields, response timings, and
streaming behavior. A moving “latest stable” release makes recordings and measurements
non-reproducible.

## Decision

Use `ggml-org/llama.cpp` release `v1.0.0`, commit
`2d8a32502909ca065c41c4cf5f2fb1c427c047fc`, for Phase 0 and Phase 1. Record the exact binary
artifact name, SHA-256, build backend, launch arguments, and `llama-server --version` output with
each real benchmark run. Test `/completion`, `/v1/chat/completions`, `/tokenize`, `/health`, SSE
recording, and the required `timings` fields against this pin before relying on them.

Any upgrade requires a new ADR, an adapter compatibility test, and a before/after benchmark on the
same task set. The upstream tag and target commit are authoritative; see the
[v1.0.0 release](https://github.com/ggml-org/llama.cpp/releases/tag/v1.0.0).

## Consequences

Phase 0 results are comparable. New upstream features do not enter implicitly, and version changes
become measurable decisions.
