# ADR-033: Bounded Runtime Server OOM Recovery

Status: accepted

## Context

ADR-031 detects allocation failures while the managed server starts. Inference can also fail after
the server is ready, either in an HTTP error body or in the managed child diagnostics. Retrying a
stream after text has reached the user would duplicate output.

## Decision

The managed `ServerManager` classifies runtime errors using the pinned allocator phrases and the
same bounded diagnostic tail used at startup. A recognized failure restarts the server with the
existing context-halving and restart limits. The local backend retries the completion once only
when the failed attempt emitted no response tokens. If any streamed text has escaped, the error is
returned without restarting or replaying the request. Manual endpoints do not receive a recovery
callback.

## Consequences

Transient allocation failures before output can recover without duplicate text. Failures after
partial output remain visible errors, and a reduced context may still be insufficient for the
request. Fake HTTP/process tests cover classification, the bounded restart, a successful retry, and
the no-replay rule; real-model runtime OOM behavior remains a hardware acceptance test.
