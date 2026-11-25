# ADR-024: Cost-Gated Remote Plan and Review

## Context

Phase 3 allows a frontier provider to draft a plan and review a milestone bundle while local
inference remains responsible for code execution. Sending project text externally must be an
explicit, per-request choice, and displayed pricing cannot rely on hard-coded or stale provider
rates.

## Decision

Configure an optional `[remote]` table in `.platypus.toml` with protocol, HTTPS endpoint, model,
API-key environment variable name, input/output USD rates per million tokens, and output-token
limit. Read the credential from the environment and never persist it. Estimate input tokens from
UTF-8 byte counts and output tokens from the configured cap; show the calculated charge estimate
and require the existing workspace confirmer before every request. Support OpenAI-compatible Chat
Completions and Anthropic Messages behind an injectable transport. Use remote calls only for plan
proposals and `/review --remote`; retain the local backend for tool execution.

## Consequences

Rates are user-maintained, input token estimates may differ from provider billing, and no billing
cap is promised. Offline tests use a fake transport. Real credentials and provider behavior require
manual verification. API request shapes follow the official [OpenAI Chat Completions reference]
and [Anthropic Messages reference].

[OpenAI Chat Completions reference]: https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create
[Anthropic Messages reference]: https://platform.claude.com/docs/en/api/messages
