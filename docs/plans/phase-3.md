# Phase 3: Hybrid Workflow

## Objective

Let users define a reviewable plan, run bounded tasks against it, check the resulting changes,
and prepare/import milestone reviews. All workflow state is stored in a project-local `PLAN.md`;
model output and imported comments are treated as untrusted data.

## Work Sequence

1. Define and validate the `PLAN.md` format: milestones, task checkboxes, stable task IDs, status,
   and safe project-relative paths. Add import/export helpers and atomic writes.
2. Add plan proposal mode to the agent loop. Show proposed tasks before execution; allow approval,
   edits, or rejection. Keep the current direct-request path available as an explicit choice.
3. Add CLI/TUI commands for `/plan`, `/milestone`, `/review`, and `/import`, with headless-friendly
   service APIs and confirmation before applying external plan text.
4. Detect project checks (CMake, Cargo, npm, pytest, Go) with `.platypus.toml` overrides. Run them
   only after explicit user approval, cap attempts at three, and send failures back as untrusted
   observations.
5. Build a token-bounded Markdown review bundle from the milestone diff, plan excerpt, check
   results, and recorded decisions. Support importing comments as new unchecked tasks.
6. Add optional remote plan/review providers only after a cost estimate and explicit consent.

## Verification and Exit

Use `FakeBackend` integration coverage for the plan → execute → check → bundle → import cycle.
Test malformed plans, path traversal, atomicity, cancellation, output bounds, and declined checks.
Run dev, ASan, UBSan, and benchmark tests. Remote-provider tests must use a fake transport.

Phase exit requires the full offline integration cycle. Remote-provider model quality, user approval
of destructive commands, and sustained human workflow testing remain separately documented evidence.
