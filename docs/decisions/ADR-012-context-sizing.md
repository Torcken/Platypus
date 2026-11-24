# ADR-012: Budget the rendered prompt and label estimates

Status: accepted (2026-11-24).

## Context

The loop previously compared history bytes with a token limit. This ignored native response-frame
overhead and unnecessarily reduced useful native context. The terminal displayed no context usage.

## Decision

`agent::ContextManager` owns the profile context limit and reply reservation: up to 2048 tokens,
or one quarter of a smaller context. Before generation it asks the backend to size the prompt.
Native llama-server profiles tokenize the exact rendered prompt using `/tokenize` with
`add_special: true` and `parse_special: true`, including model-configured special tokens. Count
failures stop the task rather than silently guessing. Generic server-side chat templates and
FakeBackend use a conservative serialized-byte estimate with wrapper allowances, labelled
`estimated`. This estimate cannot account exactly for an arbitrary hidden server template.

Sizing never edits history or compacts mid-task. A full budget fails before generation, retaining
the last usage for display. The UI copies a synchronous worker callback under its existing mutex.
Keep the old `core::ContextManager` include as a compatibility alias during migration.

API fields are documented in the [pinned server README](https://github.com/ggml-org/llama.cpp/blob/2d8a32502909ca065c41c4cf5f2fb1c427c047fc/tools/server/README.md).

## Consequences

Raw sessions make one extra tokenizer request per generation. Benchmark completion metrics exclude
these requests, while wall time includes them. Token counts are pre-generation input counts, not
observed server prefill/cache metrics. Tokenizer/inference parity on a real deployed model remains
an acceptance check; arbitrary server template/metadata overrides can affect it. No model-visible
context optimization or measured performance gain is claimed.
