# Phase 1c: Safety and sessions

Shadow Git snapshots, approved mutations/commands, durable multi-level undo, SQLite append-only turns and resume. Test persistence, new-file removal, modes, symlinks, denied actions and restore failures.

Implementation is authorized by ADR-005. Exact verification and outstanding checks are recorded
in phase-stabilization-report.md; no human acceptance is implied.

Continuation: move the safety implementation to `src/safety/snapshot_store.*`, preserving the
`util::SnapshotManager` compatibility include. Expose `snapshot(reason) -> Result<SnapshotId>`,
`restore(id) -> Result<bool>`, and read-only `list() -> Result<vector<SnapshotId>>`. Restore by ID
must retain an undo snapshot of the prior state; ordinary undo keeps its existing stack behavior.
Validate IDs/manifests before mutation, use the validated in-memory manifest during restore, and
reject file/workspace changes made while an approval prompt is pending. Tests cover reconnect,
listing and restore by ID, denied/invalid/corrupt restores, hostile sidecars, approval-time changes,
and the existing modes/symlink/empty-directory undo cases on all presets.
