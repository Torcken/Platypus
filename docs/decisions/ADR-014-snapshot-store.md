# ADR-014: Addressable snapshots and validated restores

Status: accepted (2026-11-24).

## Context

The durable snapshot implementation exposed only a boolean capture and stack-based undo through
`util::SnapshotManager`. The required safety interface needs snapshot IDs, listing and restore by
ID. Approval previews also needed checks against changes made while the user considered a diff.

## Decision

Move the implementation into `safety::SnapshotStore`, retaining the original include/type alias.
`snapshot(reason)` returns a full Git object ID. Read-only `list()` returns captured IDs newest
first; it creates no state for a workspace without snapshots. `restore(id)` accepts only complete
IDs available in that workspace and keeps a pre-restore capture for undo. Ordinary `rollback()`
retains the existing stack-consuming behavior.

Validate ID syntax, metadata paths, object types and permission-bit ranges before approval or
workspace mutation. Reject symlink sidecars. Persist permission manifests through a temporary file
and rename. Use the validated in-memory target manifest during restore. Compare staged content
and filesystem metadata before and after restore approval; re-read the target file after write
approval. Refuse stale previews rather than overwrite the changed state.

If Git restoration or metadata application fails, restore the captured prior tree and metadata,
and reinstate the original undo stack. Report recovery failures explicitly. Snapshot listing
retains captures even after stack entries have been consumed.

## Consequences

Tests cover persistent IDs, older-ID restore and undo, denied/invalid/corrupt restores, hostile
sidecars, approval-time content and empty-directory changes, and recovery after metadata application
fails. Existing mode/symlink/directory tests remain registered. The new ID API is internal; CLI
`undo` and terminal `/undo` keep their existing behavior. This is not an atomic transaction against
arbitrary concurrent external writers: checks narrow the approval race but do not lock other
editors or processes. No sustained-use or lost-work acceptance result is implied.
