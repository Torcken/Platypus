# ADR-020: Build a bounded ranked repository map

Status: accepted (2026-11-24).

## Context

Phase 2 needs a syntax-aware index across C, C++, Python, JavaScript/TypeScript, Rust, and Go. A
whole file listing is noisy, and parsing must not consume unbounded time or prompt space. The map is
supplemental context; file reads and tool results remain the source of truth.

## Decision

Use the pinned Tree-sitter runtime v0.25.10 and pinned language grammars declared in the top-level
CMake file. Index named functions, types, and namespaces from supported source extensions, then
rank declarations by identifier overlap with the current request and up to four recent user or
assistant messages. Exclude generated/dependency directories, skip files over 256 KiB, cap scanning
at 10,000 filesystem entries, 2,000 source files, 32 MiB of total source, and 10,000 declarations;
skip symlinks and cap output at 4 KiB.

For each task, insert the map as an explicitly untrusted user-context message after the stable
system prefix. Count that transient message against the prompt budget; do not persist it in the
session log. If indexing fails, continue without a map and log the reason.

## Consequences

Offline tests exercise all six requested language families, ranking output bounds, context sizing,
stable prompt placement, and non-persistence. Indexing occurs once per task, so edits made during a
task do not refresh the map. Relevance and performance on real repositories still require the Phase
2 paired benchmark; Tree-sitter does not establish semantic relevance or protect against malformed
source text acting as prompt injection.
