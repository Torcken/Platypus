<!--
For the human — not part of the prompt.

Setup (once):
  1. Put CLAUDE.md at the repository root and ROADMAP.md in docs/.
  2. Commit them on main.
Each session:
  3. Copy the block below into Claude Code, fill in the fields, send.
  4. If the session ends before the phase is done, start a new session with the same prompt:
     Claude resumes from docs/plans/phase-N-progress.md.
-->

## Current assignment

**Phase:** Stabilization, completing the Phase 0/1 foundations
<!-- 0, 1a, 1b, 1c, 1d, 2, 3, 4, 5, 6 -->

**Plan approval:** not required (human authorization, 2026-11-24; ADR-005)
<!-- "required": stop after writing the plan and wait for my answer.
     "not required": write the plan and continue. -->

**Second baseline client (phase 0):** aider
<!-- Any CLI agent that can target an OpenAI-compatible endpoint. -->

**Pinned llama.cpp release:** v1.0.0 (`2d8a32502909ca065c41c4cf5f2fb1c427c047fc`)
<!-- Change only through an ADR. See docs/decisions/ADR-002-llama-cpp-v1.md. -->

**Notes from the human:**
<!-- Results of the previous phase, decisions taken on open questions, priorities, anything that
     overrides the roadmap. Leave empty if none. -->
- The human is the implementation lead and writes the project with assistance from Ornith or
  Gemma 4. Claude or GPT may be used only on demand for review or targeted help; they do not own
  planning or implementation by default. For this assignment, the human explicitly authorized
  GPT to resume implementation through completion; ADR-005 supersedes the review-only restriction.

Follow the session protocol in `CLAUDE.md`, section 2. The phase definition is in
`docs/ROADMAP.md`.
