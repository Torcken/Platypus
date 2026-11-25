# Platypus

A C++20 terminal coding agent for open-weight models served locally with llama-server.
The current milestone restores a working headless/terminal core and a measured benchmark pipeline.
**The full roadmap and v1.0 are not complete.** See the phase records for verified scope and gaps.

## Build and test

Requires CMake 3.24+, Ninja, a C++20 compiler with std::jthread, Python 3.11+, Git, libcurl and
SQLite development packages. CPM fetches pinned JSON, Catch2 v3, toml++, reproc and FTXUI sources.
The `grep` tool additionally requires ripgrep (`rg`) on `PATH` at runtime.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
python3 -m unittest discover -s bench/tests -v
```

Repeat the CMake commands with `asan` and `ubsan` for sanitizers. Initial dependency downloads
need network access; cached builds and fixture tests run without external services or a GPU.

## Run locally

Start your llama-server separately, using the release pinned in ADR-002.

```sh
./build/dev/platypus
./build/dev/platypus run --headless --yes --project /path/to/repo --endpoint http://127.0.0.1:8080 "Fix the failing test"
./build/dev/platypus --profile profiles/ornith-1.0.toml
./build/dev/platypus --resume 1
./build/dev/platypus undo --project /path/to/repo
```

An interactive terminal opens FTXUI; redirected input uses a line interface. Read/list/grep are
read-only. Writes/edits and shell commands require explicit approval; `--yes` approves and logs
all actions for that invocation. Commands have deadlines/output limits. Snapshots and append-only
sessions live in `.platypus/`, independent of your Git history. `/undo` previews a restore.
The shell is not a sandbox. Source-derived profiles still require real-model compatibility checks.
Set `PLATYPUS_DEBUG_PREFIX=1` to log when the rendered system prefix changes between generations.

The interactive plan commands manage a validated, project-local `PLAN.md`: `/plan` shows it,
`/milestone <title>` adds a milestone and captures a review baseline, `/task` adds a task,
`/done TASK-0001` completes one, `/review` prints a bounded review
bundle, and `/import <relative-markdown-file>` previews and asks before merging a plan. Import
paths must stay inside the project and cannot traverse symlinks. Persisting check results and
review decisions is still being implemented.

Project checks run after the model finishes a task and require approval. Platypus detects CMake,
Cargo, npm, Go, and configured pytest projects; `.platypus.toml` can replace detection with direct
argument arrays, for example:

```toml
[checks]
commands = [{ name = "unit tests", argv = ["python3", "-m", "unittest"] }]
```

No shell is added around configured arguments. A failing check is returned to the model as
untrusted output for up to three repair attempts.

## Project documentation

Read `SESSION_PROMPT.md`, `CLAUDE.md`, `docs/ROADMAP.md`, then accepted ADRs in `docs/decisions/`.
[Architecture](docs/ARCHITECTURE.md) explains the current modules.
[Benchmark guide](bench/README.md) documents five verified task fixtures and edit experiments.
[Development progress](docs/plans/phase-stabilization-progress.md) records the resumption milestone.
Real-model baselines, hardware installation, and sustained-use exit criteria remain unverified.

## Optional fuzzing

With Clang and libFuzzer installed, build the parser and exact-edit fuzz targets in a separate
directory. Regex editing is intentionally excluded because the standard-library regex engine does
not guarantee bounded runtime for arbitrary patterns.

```sh
cmake -S . -B build/fuzz -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DPLATYPUS_BUILD_FUZZERS=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build/fuzz --target fuzz_tool_calls fuzz_edit_format
./build/fuzz/fuzz_tool_calls tests/fuzz/corpus/tool_calls -runs=10000 -max_len=65537 -timeout=5
./build/fuzz/fuzz_edit_format tests/fuzz/corpus/edit_format -runs=10000 -max_len=1024 -timeout=2
```
