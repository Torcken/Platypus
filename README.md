# Platypus

A C++20 terminal coding agent for open-weight models served locally with llama-server.
The current milestone restores a working headless/terminal core and a measured benchmark pipeline.
**The full roadmap and v1.0 are not complete.** See the phase records for verified scope and gaps.

## Build and test

Requires CMake 3.24+, Ninja, a C++20 compiler with std::jthread, Python 3.11+, Git, libcurl,
OpenSSL Crypto, and SQLite development packages. CPM fetches pinned JSON, Catch2 v3, toml++, reproc
and FTXUI sources.
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

The Linux installer builds the llama-server revision pinned in ADR-002. For a direct development
build, provide that server separately or pass `--server-bin PATH`. Model commands read the
versioned catalog in `profiles/catalog.json`; set `PLATYPUS_MODEL_CATALOG` to use another catalog.
Downloads go to `~/.local/share/platypus/models/` by default and are size and SHA-256 verified
before finalization. `--managed-model FILE` starts `llama-server` from `PATH` (override with
`--server-bin PATH`) on the selected loopback endpoint and shuts it down when the command exits.
Manual endpoint mode remains available.

```sh
./build/dev/platypus
./build/dev/platypus setup
./build/dev/platypus run --headless --yes --project /path/to/repo --endpoint http://127.0.0.1:8080 "Fix the failing test"
./build/dev/platypus run --managed-model ~/.local/share/platypus/models/ornith-1.0-9b-q4_k_m.gguf --profile profiles/ornith-1.0.toml "Fix the failing test"
./build/dev/platypus --profile profiles/ornith-1.0.toml
./build/dev/platypus --resume 1
./build/dev/platypus undo --project /path/to/repo
./build/dev/platypus models list
./build/dev/platypus models recommend --preference balanced
./build/dev/platypus models download ornith-1.0-9b-q4_k_m
```

`setup` is the interactive first-run path: it detects available hardware, recommends a catalog
model, downloads and verifies it, loads its profile, and starts an interactive session with a
managed loopback `llama-server`. It requires a terminal; use the `models` commands and `run`
options directly for scripted or manual setup.

An interactive terminal opens FTXUI; redirected input uses a line interface. Read/list/grep are
read-only. Writes/edits and shell commands require explicit approval; `--yes` approves and logs
all actions for that invocation. Commands have deadlines/output limits. Snapshots and append-only
sessions live in `.platypus/`, independent of your Git history. `/undo` previews a restore.
The shell is not a sandbox. Source-derived profiles still require real-model compatibility checks.
Set `PLATYPUS_DEBUG_PREFIX=1` to log when the rendered system prefix changes between generations.
Tasks use plan-first execution by default. Review the proposed steps, enter `y` to approve, `n` to
reject, or `e: step one | step two` to edit them. Approved steps are saved to `PLAN.md`, executed
sequentially, and marked complete after success; checks run after each step. Use `/direct <task>`
or `--direct` for an explicit direct request. Headless planned runs require `--yes`.

The interactive plan commands manage a validated, project-local `PLAN.md`: `/plan` shows it,
`/milestone <title>` adds a milestone and captures a review baseline, `/task` adds a task,
`/done TASK-0001` completes one, `/decision <text>` records a milestone decision, `/review` prints
a bounded review bundle, `/import <relative-markdown-file>` previews a plan before merging, and
`/review-import <relative-markdown-file>` previews Markdown bullets before adding them as unchecked
tasks to the latest milestone. `/review --copy` asks before copying the bundle to the desktop
clipboard. Import paths must stay inside the project and cannot traverse symlinks. Latest-task
check statuses, exit codes, and milestone decisions appear in the bundle. Clipboard copying uses
`wl-copy`, `xclip`, or `xsel` on Linux and `pbcopy` on macOS.
With an optional `[remote]` configuration, proposed plans can use a remote model and
`/review --remote` sends the current bounded review bundle for feedback. Each request shows a
configured-rate cost estimate and requires approval. Credentials are read only from the named
environment variable; project files do not store API keys. The byte-based input estimate is not a
provider tokenizer or a billing guarantee. See `docs/decisions/ADR-024-remote-plan-review.md`.

Project checks run after the model finishes a task and require approval. Platypus detects CMake,
Cargo, npm, Go, and configured pytest projects; `.platypus.toml` can replace detection with direct
argument arrays, for example:

```toml
[checks]
commands = [{ name = "unit tests", argv = ["python3", "-m", "unittest"] }]

[remote]
provider = "openai-compatible" # or "anthropic-messages"
endpoint = "https://api.openai.com/v1"
model = "your-model-id"
api_key_env = "PLATYPUS_REMOTE_API_KEY"
input_usd_per_million = 1.0
output_usd_per_million = 4.0
max_output_tokens = 1024
```

Set the named credential in the shell before launching Platypus. Replace the sample rates with
current provider rates; the application does not fetch or guess pricing. The provider must use
HTTPS.

No shell is added around configured arguments. A failing check is returned to the model as
untrusted output for up to three repair attempts.

## Project documentation

Read `SESSION_PROMPT.md`, `CLAUDE.md`, `docs/ROADMAP.md`, then accepted ADRs in `docs/decisions/`.
[Installation](docs/INSTALLATION.md) documents the current Linux source installer and model setup.
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
