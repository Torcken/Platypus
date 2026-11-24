# ADR-010: Typed tool observations and native turn rendering

Status: accepted (2026-11-24).

## Context

The original loop stored tool results as user text. This loses their error status and does not
follow the shipped profiles' native response frames. Reasoning can contain example tool calls
which must never execute, even when reasoning is retained in history.

## Decision

Persist tool observations with role `tool` and a JSON envelope containing `name`, `content`, and
`is_error`. Render them at the transport boundary: grouped Qwen `<tool_response>` blocks, or
Gemma tool responses continuing the same model turn. Generic chat transport uses explicit text
observations. Every parsed call receives an observation, including calls withheld after an error.

Native declarations and call instructions use the profile's syntax family. Qwen argument types
come from the declared schema, preserving numeric-looking string paths. Remove a leading explicit
or profile-opened reasoning channel before parsing tools, independently of history preservation.
Refuse an incomplete reasoning channel. Literal reasoning delimiters inside arguments remain data.
Reject mistyped or non-finite profile settings instead of silently taking defaults.

Sources: [Ornith template](https://huggingface.co/ornith-ai/Ornith-1.0-9B/blob/main/chat_template.jinja)
and [pinned Gemma template](https://huggingface.co/google/gemma-4-12B-it/blob/3ba05ed24de502e2b6d1f9a1de1bfed0247c6d8a/chat_template.jinja).

## Consequences

Offline tests check exact response frames, cached prompt prefixes, error batches, reasoning safety,
and actual CLI HTTP sessions for both native families. These checks do not establish real-model
parity or a measured gain. Such acceptance still requires the Phase 0 baseline and model runs.
Old sessions whose observations were saved as user text retain that representation when resumed.
