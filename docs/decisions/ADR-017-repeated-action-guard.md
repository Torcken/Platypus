# ADR-017: Block the fourth identical tool action

**Status:** accepted — 2026-11-24

## Decision

Within one agent task, count each action by tool name and canonical JSON arguments. The fourth and
later occurrence is returned as an error observation without executing the handler. The observation
asks the model to inspect prior results, choose a different action, or finish. A different action
can recover the task normally. Counts reset for each task.

Object keys use nlohmann JSON's ordered representation, so argument insertion order does not change
the canonical signature. This matches the benchmark loop metric's action identity.

## Verification limits

FakeBackend integration coverage verifies three executions, a blocked fourth call, corrective
observation delivery and successful recovery through a different call. Repeated-action quality and
impact on task success still require paired model benchmarks.
