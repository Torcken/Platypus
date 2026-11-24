# ADR-009: Benchmark provenance and comparison gates

**Status:** accepted — 2026-11-24

## Decision

Exploratory runs may have incomplete metadata, but their reports must say so. `--baseline` on the
runner and report command requires all protocol metadata, matching ADR-002's server pin, local
model/server/client artifact hashes checked from files, and client version output checked from
the selected executable. A hash/version mismatch is an error even outside baseline mode.
Per-client configuration overrides prevent a matrix from assigning one version to all clients.

Paired comparisons require identical task commits/verifiers and serving/model/hardware/sampling
settings. Never silently intersect task sets or pool incompatible runs under one display label.
Warm-ups are excluded from reports even if a caller supplies them directly.

## Limits

Checked local artifacts and complete metadata cannot prove which binary a remote endpoint runs,
or verify operator-supplied hardware/launch statements. Baseline mode validates provenance and
comparability; human compatibility checks, frozen tasks and real hardware evidence remain required
before treating a result as a roadmap exit. Fake-client runs cannot become baseline publications.
