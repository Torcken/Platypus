# ADR-030: Add a Verified Gemma 4 12B Catalog Variant

## Context

Phase 4's target hardware and model facts include Gemma 4 12B, but the initial catalog only offered
Ornith variants. The existing Gemma profile already describes the project's native tool-call codec.

## Decision

Add the official `ggml-org/gemma-4-12B-it-GGUF` Q4_0 artifact at immutable revision
`790d826c6ec7dc8aa0ab23af33e5b8313140a231`. Record the SHA-256 and byte length from the
[revision's LFS metadata](https://huggingface.co/ggml-org/gemma-4-12B-it-GGUF/commit/790d826c6ec7dc8aa0ab23af33e5b8313140a231).
The catalog uses the existing `profiles/gemma-4.toml` profile. The
estimated floors are 12 GiB system memory and 8 GiB GPU memory; balanced rank 1, quality rank 3,
and no fast rank are preliminary selection heuristics, not measured performance claims.

## Consequences

Catalog and CLI tests cover the new artifact and its recommendation on a 16 GiB RAM / 12 GiB GPU
inventory. Real load, tool-call, quality, memory-floor, and recommendation validation remain human
acceptance work. The source model card reports Apache-2.0; users should still follow its terms.
