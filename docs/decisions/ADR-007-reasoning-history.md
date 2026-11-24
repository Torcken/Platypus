# ADR-007: Reasoning and profile defaults

**Status:** accepted — 2026-11-24

Ship text-only Ornith and Gemma profiles using their published role delimiters and explicit
thinking-disabled generation prefixes. The project default is not to retain reasoning in
history: strip it once when the new assistant turn is appended, never rewrite old turns.
This reduces latency and context cost; it is an experiment setting, not a publisher sampling
recommendation or a measured reliability claim. Profiles can preserve reasoning explicitly.

Client-side templates repeat the same assistant generation prefix in history, preserving the
previous prompt byte prefix. Native tool syntax parsers are selected by profile, not model
name. Full native tool-history parity, quantization/context tuning and real-model task gains
remain unverified until the human baseline. Temperature 0.3 and 16K are project defaults.
