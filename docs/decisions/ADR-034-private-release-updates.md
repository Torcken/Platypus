# ADR-034: Private Release and Update Channel

## Context

Phase 4 needs versioned Linux and macOS builds and `platypus update`. The repository will remain
private until the product is production grade, so beta users need authenticated release access.
Installations also need to preserve their selected llama-server backend across updates. The backend
marker must be written only when the installed binary matches the target's OS and toolchain.

## Decision

Publish version-tagged GitHub Releases containing one archive per supported OS/backend target and a
`SHA256SUMS` asset. The archive contains only the Platypus executable, pinned llama-server, updater,
model profiles/catalog, and an install-target marker. The updater uses the GitHub Releases API, accepts
`GH_TOKEN` or obtains a token from `gh auth token` for private access, and permits unauthenticated
access for a future public repository. It validates the selected asset and checksum before making
same-filesystem staged replacements with rollback on failure. It never updates model files or user
project data.

## Consequences

Private beta updaters need authorized GitHub access (a token or GitHub CLI login). Release assets
remain private with the repository. Once the repository is public, unauthenticated update access
works. Releases must use the documented archive names and target marker; arbitrary backend
switching is not part of an update.
