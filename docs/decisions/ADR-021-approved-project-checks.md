# ADR-021: Run approved project checks after tasks

Status: accepted (2026-11-25).

## Context

Phase 3 asks Platypus to detect common project checks, run them after an agent task, and return
failures to the model for a bounded repair cycle. Build and test commands execute project-controlled
code and may modify files or access the network.

## Decision

Detect CMake, Cargo, npm (only when `scripts.test` exists), Go, and pytest manifests. A project can
replace detection in `.platypus.toml`:

```toml
[checks]
commands = [{ name = "unit tests", argv = ["python3", "-m", "unittest"] }]
```

An empty `commands` array disables checks. Each configured check is a name and a bounded argv array;
Platypus invokes it directly without adding a shell. It shows the exact argv and requires approval
before every check. Each process has a five-minute deadline and 16 KiB output cap. Nonzero status,
start errors, and bounded output return to the model as explicitly untrusted task context. The agent
may make up to three check-and-repair attempts per task, then returns the last failure to the user.

## Consequences

Offline tests cover supported manifest detection, overrides, disabled checks, approval and decline,
direct argv execution, and the three-attempt feedback limit. Check results are not yet persisted for
review bundles; human acceptance and model effects remain unmeasured.
