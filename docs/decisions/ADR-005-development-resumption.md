# ADR-005: Authorized development resumption

**Status:** accepted — 2026-11-24

## Context and decision

Following the full repository review, the human explicitly requested that GPT resume development
through completion of Platypus. For this assignment this supersedes ADR-004's review-only role,
the stale Phase 0 session selection, and per-phase plan approval pauses. Proceed sequentially
through the canonical roadmap, keeping implementation plans and verification records accurate.
Existing AGENTS.md is preserved. No external model calls, publishing, or pushes are authorized.

## Consequences

The assistant may implement and verify project code without repeatedly seeking plan approval.
Hardware measurements and real-use exit criteria still require evidence. Phase 6 is conditional
on post-v1.0 measurement. Existing user changes must be preserved throughout implementation.
