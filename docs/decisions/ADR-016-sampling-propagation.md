# ADR-016: Validate and send profile sampling settings

**Status:** accepted — 2026-11-24

## Decision

Profiles may set `top_k`, `top_p`, `min_p`, `typical_p`, `repeat_penalty`,
`presence_penalty`, `frequency_penalty`, and `seed` under `[sampling]`. The loader rejects unknown
fields, non-finite numbers, wrong scalar types, and values outside documented ranges. Temperature
remains the existing top-level profile setting. The backend sends validated sampling values on both
OpenAI-compatible chat and raw `/completion` requests, including streaming requests.

The supplied profiles explicitly use `top_k = 40`, `top_p = 0.95`, and `min_p = 0.05`, matching the
pinned llama-server v1.0.0 defaults documented in the locally retained server README. These are
project settings, not model-publisher recommendations. The benchmark proxy records the request
body, so the actual sent configuration is available for provenance checks.

## Verification limits

The loopback fixture asserts fields on chat, streaming, and raw requests; profile tests cover invalid
sampling values. A real pinned server and model have not been used to verify model quality or
cross-client sampling equivalence.
