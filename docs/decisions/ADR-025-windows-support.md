# ADR-025: Windows Support in v1.0

## Context

Phase 4 requires a platform scope decision before adding hardware discovery, managed inference,
installers, and CI. The documented v1.0 target platforms are Linux x86_64 and macOS arm64. The
current process, terminal, snapshots, clipboard, and shell tooling are POSIX-oriented; adding
Windows now would multiply the hardware, lifecycle, packaging, and acceptance matrix.

## Decision

Do not support native Windows in Platypus v1.0. Keep Linux x86_64 and macOS arm64 as the supported
targets. Windows users may use WSL2 only as an unverified workaround; do not claim or test it as a
supported installation. Revisit native Windows after v1.0 with a dedicated platform plan.

## Consequences

Phase 4 can focus on the two existing documented targets. OS-specific work remains behind
`src/platform/`, so a later Windows port can replace platform implementations without changing the
agent or provider interfaces. This decision limits current distribution reach and does not imply
that WSL2 works on every host.
