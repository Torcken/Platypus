# ADR-003: Phase 1 mutation and command-safety contract

**Status:** accepted — 2026-11-20

## Decision

All tool paths are resolved from the project root using canonical paths. Reads, lists, greps,
writes, and edits reject a path whose resolved target—or, for a new file, resolved parent—escapes
that root; symlink escapes are rejected. A failed containment or snapshot check prevents mutation.

Before every write or edit, Platypus creates a snapshot and renders a unified diff. Interactive mode
requires confirmation before applying it. `run` always displays the exact command, working
directory, timeout, and output cap and requires confirmation. A noninteractive invocation with
`--yes` is the user's explicit approval for both edits and commands for that one invocation; it
logs every approved action and cannot be implied by configuration.

Snapshots exclude `.git/` and `.platypus/`, preserve regular files, symlinks, modes, and untracked
files, and are stored only in `.platypus/snapshots`. `/undo` previews the restore diff and asks for
confirmation. The agent never silently performs a partial restore.

## Consequences

`run` remains intentionally powerful after explicit user approval; it is not a sandbox. Benchmark
tests use a fake confirmer. A later permission policy may refine defaults but cannot weaken these
containment and visibility guarantees.
