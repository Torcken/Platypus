# Phase 1b progress

- [x] Six bounded tools with flat argument checks, containment, approval and snapshots.
- [x] Agent loop recovery from malformed calls and tool errors; 25-step configurable bound.
- [x] Working headless CLI with explicit --yes, real HTTP backend and no factory-name mismatch.
- [x] Successful edits, malformed-call recovery, failed-edit retry, bounded loops and cancellation tests.
- [x] Original test files registered; assertions remain enabled in every preset.
- [x] Canonical agent::ContextManager budgets rendered prompts, reserves replies and rejects
  overflow without changing history; tokenizer failures stop generation (ADR-012).
- [ ] EditFormat interface and unambiguous fuzzy edit implementation selected by benchmark evidence.
- [ ] Fuzz targets, ripgrep-backed search and remaining canonical Tool/ToolResult migration.
- [x] Per-profile token/context status with tokenizer counts or explicit estimate labels.
- [x] Canonical tools::Tool/ToolResult/ToolContext; registry/built-ins in src/tools/;
  bounded UTF-8 success/error observations with persisted truncation flags (ADR-013).
- [ ] Broader native-codec adversarial regression corpus.
