# Phase 2 progress

- [x] Concept documentation for grammar-constrained decoding and context management.
- [x] Offline GBNF generator for JSON-tagged, Qwen XML and Gemma single calls, with flat schema
  validation, optional fields and any property ordering (ADR-019). llama.cpp validator and model
  compatibility remain unverified; request-path activation is gated on paired evidence.
- [ ] Real-recording parser regression corpus.
- [x] Bounded ranked Tree-sitter map for C, C++, Python, JavaScript/TypeScript, Rust and Go; added
  to the transient prompt after the stable prefix and included in token sizing (ADR-020).
- [ ] Measure map relevance, per-tier context settings and task-boundary compaction behavior.
- [x] Opt-in runtime stable-prefix diagnostics and exact native prefix regressions.
- [x] Fourth-and-later identical actions are blocked with a corrective tool observation (ADR-017).
- [ ] Human paired benchmark exit measurements.
