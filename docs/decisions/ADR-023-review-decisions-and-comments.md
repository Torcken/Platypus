# ADR-023: Review Decisions and Comment Imports

## Context

Phase 3 review bundles need recorded decisions, and reviewers need a safe way to turn feedback into
new plan tasks. Decisions do not belong in the constrained task checklist, while imported comments
must never execute as instructions.

## Decision

Store decisions in private `.platypus/review-decisions.json`, keyed by the milestone's immutable
baseline snapshot ID. `/decision <text>` records one single-line decision after confirmation, and
`/review` includes decisions for the selected baseline. `/review-import <relative-file>` accepts a
project-contained Markdown file with one bullet or numbered comment per line, previews it, asks for
confirmation, and atomically adds the comments as unchecked tasks to the latest milestone.

## Consequences

Decision records are bounded, validated, and atomically replaced. Comment imports reject symlinks,
path traversal, malformed lines, and oversized batches; imported text is stored as data and is never
executed. Clipboard integration and external review-provider quality remain open.
