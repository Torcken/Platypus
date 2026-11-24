# Phase 1b: Tools and Agent Loop

## Goals
- Implement robust Tool-call Codecs to handle multiple response formats (JSON, XML, raw text).
- Expand the Toolset with production-ready logic for core filesystem and shell operations.
- Implement a "Headless" mode for non-interactive execution.
- Refine the Execution Engine's "think-act-observe" loop for better error recovery and state management.

## Deliverables
- `src/core/codec.hpp` / `src/core/codec.cpp`: Parsing logic for tool calls.
- `src/tools/tool.hpp`, `registry.hpp`, `tool.cpp`, `builtins.cpp`: canonical Tool/ToolResult,
  shared validation/output caps and the six tools `read`, `write`, `edit`, `list`, `grep`, `run`.
- `src/core/execution_engine.cpp`: Refined loop logic.
- `src/main.cpp`: Headless mode flag and integration.

## Success Criteria
- The agent can successfully parse tool calls from multiple different LLM outputs.
- All core tools (read, write, list, grep, run) function correctly and respect safety boundaries (ADR-003).
- The agent can complete a multi-step task in a fully non-interactive mode.
