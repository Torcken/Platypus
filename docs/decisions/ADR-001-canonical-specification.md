# ADR-001: Canonical specification and document lifecycle

**Status:** accepted — 2026-11-20

## Context

The initial repository contained multiple copies of the product brief and roadmap. They differed
in implementation details and would let contributors implement incompatible requirements.

## Decision

The canonical specification is `README.md`, `CLAUDE.md`, `docs/ROADMAP.md`, and accepted ADRs in
this directory. `SESSION_PROMPT.md` selects the active phase and may narrow work, but cannot
silently override a canonical document. `docs/plans/` contains phase-local plans, progress, and
reports; it is not a second architecture source.

When a change affects future phases, interfaces, safety, compatibility, or reproducibility, record
it in a short ADR before implementation and update the canonical document it changes. Superseded
drafts and copied assistant prompts are deleted instead of retained as competing instructions.

## Consequences

Contributors have one unambiguous reading order. Links use the actual `docs/` layout. Historical
context belongs in Git history or an explicitly labelled archive, never in an active duplicate
brief.
