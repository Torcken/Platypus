# Phase 1b progress

- [x] Six bounded tools with flat argument checks, containment, approval and snapshots.
- [x] Agent loop recovery from malformed calls and tool errors; 25-step configurable bound.
- [x] Working headless CLI with explicit --yes, real HTTP backend and no factory-name mismatch.
- [x] Successful edits, malformed-call recovery, failed-edit retry, bounded loops and cancellation tests.
- [x] Original test files registered; assertions remain enabled in every preset.
- [x] Canonical agent::ContextManager budgets rendered prompts, reserves replies and rejects
  overflow without changing history; tokenizer failures stop generation (ADR-012).
- [ ] EditFormat interface and unambiguous fuzzy edit implementation selected by benchmark evidence.
- [x] Streaming OpenAI tool-call deltas are assembled by index and emitted as one canonical agent
  payload; incomplete batches fail closed (tested against the loopback HTTP fixture).
- [x] Bounded native/JSON parser, duplicate/depth checks, full Gemma batches and no embedded-JSON
  fallback after native failures; 2,000 mutation smoke cases in every preset (ADR-015).
- [x] Tool-call libFuzzer target, seed corpus and CI smoke job written.
- [ ] Execute a coverage-guided campaign on Clang/libFuzzer; unavailable locally.
- [ ] Edit-format fuzz target.
- [x] ripgrep-backed search with workspace containment, bounded output and explicit truncation.
- [x] Per-profile token/context status with tokenizer counts or explicit estimate labels.
- [x] Canonical tools::Tool/ToolResult/ToolContext; registry/built-ins in src/tools/;
  bounded UTF-8 success/error observations with persisted truncation flags (ADR-013).
- [x] Native-codec adversarial regressions for long payloads, invalid bytes, duplicate keys,
  nested/encoded JSON and malformed batches.
