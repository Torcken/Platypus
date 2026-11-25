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

The lifecycle service does not infer OOM from arbitrary server output; the generation/setup caller
must classify a model-load failure and request recovery. Manual endpoint mode remains available.
