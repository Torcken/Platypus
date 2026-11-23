# Benchmark Harness

This directory contains the Phase 0 benchmark harness for evaluating coding agents on local LLM servers (e.g., `llama-server`).

## Overview
The benchmark harness is designed to measure the performance of different coding agents across a variety of software engineering tasks. It captures metrics like wall time, token usage, cache hits, and success rates (verified by automated tests).

## Components
- `run.py`: The primary execution engine.
- `proxy.py`: An HTTP proxy that sits between the agent and the LLM server to record interactions.
- `report.py`: Processes raw results into a Markdown report.
- `adapters/`: Contains client-specific adapters (e.g., `opencode`).
- `edit_formats.py`: Utilities for applying unified diffs and search/replace edits.
- `tasks/`: JSON files defining the tasks to be benchmarked.
- `repos/`: Local copies of the repositories for the tasks.

## Getting Started

### Prerequisites
- Python 3.11+
- A running `llama-server` instance (v1.0.0).
- `opencode` CLI tool (if using the opencode adapter).

### Running a Benchmark
To run the benchmark, execute the `run.py` script with the desired task, client, and model configuration:

```bash
python3 bench/run.py --task task_001_cpp_refactor --client opencode --model "llama-cpp" --repeats 3
```
*(Note: Command line arguments are currently being finalized.)*

### Understanding Results
After completion, a detailed report will be generated in `bench/report.md`. This report includes:
- A summary table with success rates and 95% Wilson confidence intervals.
- Average wall time, turn counts, and token usage.
- Detailed per-repeat statistics.

## Customizing the Benchmark
- **Add a Task**: Create a new JSON file in `bench/tasks/` and a corresponding repo in `bench/repos/`.
- **Add a Client**: Implement a new adapter in `bench/adapters/`.
- **New Metrics**: Update `bench/report.py` and `bench/run.py` to include new KPIs.
