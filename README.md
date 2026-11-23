# Platypus

Platypus is a planned C++20 terminal coding agent for open-weight models running on consumer
hardware. It prioritizes small-model reliability, safe repository changes, and measurable gains.

## Project status

The repository starts at Phase 0: a Python 3.11+ benchmark harness. There is no product source
code yet; do not begin C++ work until Phase 0's benchmark gate is met.

## Canonical documentation

Read these files in order:

1. `SESSION_PROMPT.md` — current phase and approval state.
2. `CLAUDE.md` — standing engineering rules, architecture, and verification requirements.
3. `docs/ROADMAP.md` — phase deliverables and exit criteria.
4. `docs/decisions/` — accepted architecture decisions.

`docs/benchmark-protocol.md` governs benchmark execution and data handling. Plans, progress, and
reports belong in `docs/plans/`.

## Version pin

Phase 0 and Phase 1 use `llama.cpp` `v1.0.0` at commit
`2d8a32502909ca065c41c4cf5f2fb1c427c047fc`. Change it only through an ADR and rerun affected
benchmarks.
