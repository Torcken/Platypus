# ADR-006: POSIX process-group cancellation

**Status:** accepted — 2026-11-24

The pinned reproc 14.2.5 API terminates its direct child and does not expose creation and
termination of an entire POSIX process group. Shell tools can launch descendants which keep
pipes open after their parent exits. Linux/macOS therefore use a small platform-local
posix_spawn wrapper with POSIX_SPAWN_SETPGROUP, nonblocking drain, deadlines, and group kill.
No detached task outlives the call. reproc stays pinned for future portable process work;
Windows remains undecided until the Phase 4 platform ADR. This is not a command sandbox.
