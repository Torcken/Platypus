# ADR-011: Benchmark observability for native calls

Status: accepted (2026-11-24).

## Context

Raw `/completion` requests render tools into a prompt rather than a JSON `tools` field. Native
responses also contain text rather than wire `tool_calls`. Treating these sessions as tool-free
produced unavailable tool metrics despite tools being used.

## Decision

The benchmark adapter opts into a versioned HTTP metadata header containing declared schemas,
syntax/reasoning settings and typed observation identities/error flags. The recording proxy strips
it upstream and retains it alongside the unchanged request and response. Ordinary CLI sessions
send no metadata. This is measurement instrumentation, not an inference protocol extension.

Metrics independently parse actual responses with strict format-specific parsers and a shared
minimal argument checker. No tolerant repairs are credited as valid output. Deduplicate observed
results by stable history index. Reject corrupt metadata as unavailable metrics with explicit
errors. Observe errors through the next request, matching the existing wire-tool-message rule.

## Consequences

CPU-only CLI → proxy → HTTP fixture tests cover all three syntax families, a failed edit and
successful retry, known timings, and removal of the metadata header before forwarding. The
recording remains replayable by FakeBackend. Client-reported error status is not independent
verification; only the maintained task verifier determines success. This does not establish
real-model compatibility or sampling equivalence with third-party clients.
