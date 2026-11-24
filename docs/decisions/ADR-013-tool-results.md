# ADR-013: Canonical tool results and bounded UTF-8 text

Status: accepted (2026-11-24).

## Context

The original tool registry returned only Result<string>. Individual tools truncated bytes before
the registry could observe the original size. This lost truncation status and could split a UTF-8
character, causing later JSON serialization to fail. Error outputs also escaped the common cap.

## Decision

Move built-ins and the registry into `src/tools/`. Every production tool implements
`tools::Tool`: fixed name, description and JSON schema, plus synchronous `execute(args, context)`.
The context borrows the safety store and specifies an output byte limit. `ToolResult` carries
content, error status and truncation status. Apply common flat-schema validation before dispatch;
catch handler exceptions at this boundary.

Normalize observations into valid UTF-8, replacing invalid byte sequences with U+FFFD. Cap both
successful and failed output without splitting code points, including a visible truncation marker
inside the byte limit. Preserve the flags in SQLite tool-observation envelopes. Native prompt
rendering still uses the observation text and explicit error prefix. The `core/` header retains
aliases and a Result<string> registry adapter for original callers.

## Consequences

Tests cover invalid arguments without execution, cancellation, error/exception caps, invalid and
multibyte UTF-8, very small limits, and truncation status surviving a complete agent session.
Binary tool output is represented as readable replacement characters; file writes retain their
original content. This interface/correctness repair does not select a new editing policy or claim
a measured model gain. EditFormat, fuzzing and the SnapshotStore migration remain separate work.
