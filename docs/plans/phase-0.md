# Phase 0 Plan: Benchmark Harness

This document outlines the implementation plan for Phase 0, focusing on building a robust measurement infrastructure for local LLMs.

## 1. Implementation Order: Files to Create

### Documentation & Definitions
- [ ] `docs/tokens-and-context.md`
- [ ] `docs/kv-cache-and-prefill.md`
- [ ] `docs/measuring-agents.md`
- [ ] `bench/RECORDING_FORMAT.md`

### Core Infrastructure
- [ ] `bench/proxy.py` (HTTP proxy for `llama-server`)

### Task & Dataset Preparation
- [ ] `bench/tasks/` (5 example tasks: 1 C++, 1 Python, 3 others)
- [ ] `bench/repos/` (local clones of example repositories)

### Client Adapters & Experiments
- [ ] `bench/adapters/opencode.py`
- [ ] `bench/adapters/[TBD].py`
- [ ] `bench/edit_formats.py` (unified diff vs search/replace experiment)

### Runner & Reporting
- [ ] `bench/run.py` (the primary execution engine)
- [ ] `bench/report.py` (Markdown generation and statistics)
- [ ] `bench/README.md` (instructions)

### Testing
- [ ] `bench/tests/` (Unit and E2E tests)

## 2. Public Functions, Classes, and Data Formats

### `bench/proxy.py`
- `ProxyServer`: Handles `/v1/chat/completions`, `/completion`, `/v1/completions`, `/health`, `/tokenize`.
- **Functionality**: Forwards requests, reassembles SSE chunks, and writes one JSONL record per request/response.

### `bench/run.py`
- `Runner`: Main entry point for executing a task.
- `run_task(task_id, client_name, model_config, repeats)`: Orchestrates the lifecycle of a single benchmark run.

### `bench/report.py`
- `ReportGenerator`: Processes JSONL files to create markdown tables.
- `calculate_wilson_interval(successes, total)`: Computes the 95% Wilson interval.

### Data Formats
- **Recording Format (JSONL)**:
  ```json
  {
    "timestamp": "...",
    "endpoint": "...",
    "request": { ... },
    "response": { ... },
    "timings": {
      "prompt_n": 0,
      "cache_n": 0,
      "prompt_ms": 0,
      "predicted_n": 0,
      "predicted_ms": 0
    }
  }
  ```

## 3. Unit and End-to-End Tests

### Unit Tests
- **Proxy**: Verify correct reassembly of SSE chunks and JSONL field extraction.
- **Edit Formats**: Verify fuzzy matching logic and `difflib` thresholds.
- **Report**: Verify Wilson interval calculations and bootstrap logic.
- **Runner**: Verify timeout handling and failure state recording.

### End-to-End Tests
- **`test_full_pipeline`**: 
  - Runner -> Proxy -> Fake Server -> Fake Client -> `verify.sh` -> JSONL -> Report.
  - Must run without network and without GPU.

## 4. Recording Format & Benchmark-Result Fields

| Field | Description |
| --- | --- |
| `success` | `verify.sh` exits 0 within timeout. |
| `wall_time_s` | Start to finish time. |
| `turns` | Number of completion requests. |
| `prompt_tokens_total` | `sum(prompt_n + cache_n)`. |
| `prompt_tokens_processed` | `sum(prompt_n)`. |
| `cache_hit_ratio` | `sum(cache_n) / prompt_tokens_total`. |
| `prefill_ms` | Median and total `prompt_ms`. |
| `invalid_tool_calls` | Count of malformed tool calls. |
| `failed_edits` | Count of edit tools returning errors. |
| `loops` | Count of repeated actions (4+ times). |

## 5. UNVERIFIED Values

- **opencode CLI flags**: Need to be verified against the installed binary.
- **[TBD] Client CLI flags**: Need to be verified against the second client's help output.
- **`timings` field names**: Need to be verified against `llama-server` v1.0.0 README.

## 6. Risks or Questions Requiring Decision

- **Second Client**: Which specific client should be implemented as the second adapter?
- **Hardware Tiers**: What specific names/labels should be used for the hardware tiers in the report?
- **Proxy Isolation**: Should the proxy run in a separate process or as a thread for the benchmark runner?
- **Bootstrap Seed**: Confirm the fixed seed for the bootstrap interval calculation (defaulting to 42).
