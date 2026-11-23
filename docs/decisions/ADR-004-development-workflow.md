# ADR-004: Human-led development with opt-in external review

**Status:** accepted — 2026-11-20

## Decision

The human is the implementation lead for Platypus. Development work is performed directly by the
human, with Ornith or Gemma 4 used as local coding assistants. Claude and GPT are optional tools
for review or targeted help only when the human asks for them; they do not independently plan or
implement project work.

This governs how Platypus is built. It does not remove any future, user-facing product capability
from the roadmap. Any remote service use must remain explicit and follow the project's data and
permission rules.

## Consequences

Plans, implementation choices, and external sharing stay under the human's control. Contributors
must not assume an external model is available, send project content to one, or wait for one before
continuing local work.
