# ADR-031: Bounded Managed-Server Startup OOM Detection

Status: accepted

## Context

The managed-server lifecycle could retry after an out-of-memory condition only when a caller
classified the failure, but no production caller had access to the child diagnostics. Capturing
output in a temporary file would make classification possible but could consume unbounded disk
space while a healthy server ran.

## Decision

Drain the server's stdout and stderr continuously through reproc pipes into a 64 KiB in-memory tail.
When the child exits before readiness, classify only known allocation phrases from the pinned
llama.cpp source, including CUDA, Vulkan, Metal, and context-allocation failures. Retry matching
startup failures within the existing limit, halving context on each retry. Generic exits remain
failures. This does not classify OOM during later inference.

## Consequences

The reader drains output throughout the child lifetime, avoiding pipe backpressure and unbounded log
storage. The process wrapper serializes pipe polling and shutdown. Tests cover classification,
context reduction, and the real process/pipe path with a fake child. Runtime inference OOM recovery
GPU allocation diagnostics outside the pinned phrases and runtime inference OOM recovery remain
outside this decision.
