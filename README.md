# Platypus

A C++20 terminal coding agent for open-weight models served locally with llama-server.
The current milestone restores a working headless/terminal core and a measured benchmark pipeline.
**The full roadmap and v1.0 are not complete.** See the phase records for verified scope and gaps.

## Build and test

Requires CMake 3.24+, Ninja, a C++20 compiler with std::jthread, Python 3.11+, Git, libcurl and
SQLite development packages. CPM fetches pinned JSON, Catch2 v3, toml++, reproc and FTXUI sources.

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

## Project documentation

Read `SESSION_PROMPT.md`, `CLAUDE.md`, `docs/ROADMAP.md`, then accepted ADRs in `docs/decisions/`.
[Architecture](docs/ARCHITECTURE.md) explains the current modules.
[Benchmark guide](bench/README.md) documents five verified task fixtures and edit experiments.
[Development progress](docs/plans/phase-stabilization-progress.md) records the resumption milestone.
Real-model baselines, hardware installation, and sustained-use exit criteria remain unverified.
