# ADR-026: Versioned Model Catalog

## Context

Phase 4 needs reproducible model choices that can be recommended offline and verified after
download. Mutable repository branches and unpinned profile names are insufficient for safe
installation.

## Decision

Store catalog version 1 in `profiles/catalog.json`. Each artifact records a model ID, quantization,
HTTPS Hugging Face URL pinned to an immutable repository commit, SHA-256, byte size, memory floors,
project profile, and optional fast/balanced/quality ranks. Parse the file with strict size, count,
path, URL, checksum, and rank validation. Recommendation requires known system memory and selects
the lowest rank that fits the reported RAM and VRAM; unavailable facts fail closed.

The initial entries are Ornith 1.0 9B GGUF files from the publisher repository at commit
`d6b5f9b2ef835df5085615e0c405ce0092cc8b53`. The artifact SHA-256 values and byte sizes come from
the [repository's verified LFS pointers](https://huggingface.co/ornith-ai/Ornith-1.0-9B-GGUF/commit/d6b5f9b2ef835df5085615e0c405ce0092cc8b53).
The minimum memory floors and preference ranks are Platypus estimates and require hardware and
quality validation before release.

## Consequences

Tests can exercise catalog selection without a GPU or network. New repositories and quantizations
must pin revisions and independently sourced SHA-256 values. The catalog does not imply that a
model loads successfully on a given GPU; managed-server startup and five-machine acceptance remain
Phase 4 work.
