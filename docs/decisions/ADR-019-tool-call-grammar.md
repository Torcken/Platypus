# ADR-019: Generate syntax-specific tool-call grammars

Status: accepted (2026-11-24).

## Context

Phase 2 calls for GBNF generated from the native call syntax and the flat schemas used by the
built-in tools. Grammar-constrained generation can reject malformed tool arguments, but a grammar
whose root accepts only a call would also reject ordinary final answers if attached to every model
request.

## Decision

Add an offline `ToolGrammar` generator for `json_tagged`, `qwen_xml`, and `gemma`. It validates
tool identifiers and flat `object` schemas with string, integer, number, or boolean properties;
requires every declared required key; accepts optional keys and any key ordering; and bounds each
schema to five properties. The generated root accepts one call. Existing tolerant parsing and
model requests remain unchanged. Grammar activation requires a separate request-mode design and
paired real-model evidence.

## Consequences

Regression tests cover each syntax family and schema rejection. The generated GBNF has not yet
been checked by llama.cpp's validator or a real model; support for strings containing native Qwen
or Gemma delimiters is therefore not claimed. The phase-2 generation item is implemented as an
offline primitive, with integration and acceptance outstanding.
