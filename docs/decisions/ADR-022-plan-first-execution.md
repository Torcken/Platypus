# ADR-022: Plan-First Task Execution

## Context

Phase 3 requires a user-reviewed plan before project work begins. The existing agent loop accepts a
task and immediately enters its tool loop, so proposals need a separate bounded request and an
explicit decision point.

## Decision

For ordinary CLI and TUI tasks, ask the configured backend for a JSON object containing one to
twelve concise steps. Include only the latest milestone as untrusted reference context, capped at
16 KiB, and reserve no more than 512 output tokens. Present the steps for approval, rejection, or
editing. After approval, atomically append the steps to `PLAN.md`, execute them sequentially, run
the configured project checks after each step, and mark each task complete only after its agent
run succeeds. `--direct` and `/direct` retain an explicit path that skips proposal generation.

## Consequences

Every planned task adds a model request, so benchmark recordings and turn metrics include the plan
proposal. `FakeBackend` tests cover proposal validation, edits, rejection, persistence, sequential
checks, and completion state. Real-model proposal quality and the complete review/import cycle
remain unverified.
