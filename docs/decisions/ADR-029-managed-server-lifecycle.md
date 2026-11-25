# ADR-029: Managed Local Inference Server

## Context

First-run setup needs to launch the pinned llama-server, avoid exposing it on the network, detect
readiness, and terminate it cleanly. Model-load OOMs need a bounded recovery path.

## Decision

Use reproc++ to own the child process. Bind the server to `127.0.0.1`, pass the selected model path
and context size as arguments, poll `/health` until a bounded timeout, and terminate then kill on
shutdown. The caller may request at most three OOM restarts (one by default); each halves the
context down to a configured floor. Process startup and health checks are injectable for offline
tests.

## Consequences

Startup recovery classifies only known allocation diagnostics. Runtime inference recovery uses the
bounded response/diagnostic classification and no-replay rule in ADR-033. Manual endpoint mode
remains available and does not receive a recovery callback.
