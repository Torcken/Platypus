# Repository Guidelines

## Project Structure & Module Organization

This repository is currently in its planning phase. Read `SESSION_PROMPT.md`,
then `CLAUDE.md`, before starting work; `docs/ROADMAP.md` defines the active phase.
The human is the implementation lead; Ornith or Gemma 4 provide coding assistance, while Claude
or GPT are used only when the human requests review or focused help.
Implementation follows the layout specified in `CLAUDE.md`: C++ sources live in
`src/` (`agent/`, `llm/`, `tools/`, `safety/`, `session/`, `tui/`, `platform/`,
and `util/`); C++ tests live in `tests/unit/`, `tests/integration/`, and
`tests/fixtures/`. The standard-library-only Python benchmark harness is in
`bench/`, with its tests in `bench/tests/`. Put model profiles in `profiles/`
and architecture, ADRs, and phase records under `docs/`.

For each phase, create `docs/plans/phase-N.md` and
`docs/plans/phase-N-progress.md`; keep the progress checklist accurate.

## Build, Test, and Development Commands

Phase 0 uses Python 3.11+ and no third-party packages:

```bash
python3 -m unittest discover -s bench/tests -v
```

From phase 1 onward, configure, build, and test every affected CMake preset:

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev --output-on-failure
cmake --preset asan && cmake --build --preset asan && ctest --preset asan --output-on-failure
cmake --preset ubsan && cmake --build --preset ubsan && ctest --preset ubsan --output-on-failure
```

Report commands that cannot run; do not call them verified.

## Coding Style & Naming Conventions

Use C++20, `.clang-format` (LLVM base, four spaces, 100 columns), and
`clang-tidy`. Use `snake_case` for functions and variables, `PascalCase` for
types, and `kConstant` for constants. Keep one namespace per top-level module
(for example, `platypus::llm`). Prefer small explicit functions and
`util::Result<T>` across module boundaries. Python in `bench/` follows PEP 8,
100 columns, type hints, module docstrings, and Google-style public docstrings.

## Testing Guidelines

Add Catch2 unit tests for C++ parsers, tools, snapshots, and profile loading.
Use `FakeBackend` fixtures for integration sessions; tests must not require a
GPU or network. Add `unittest` coverage for each benchmark module. Never skip,
weaken, or delete a test merely to pass CI.

## Commits & Pull Requests

Work on a phase branch such as `phase-0` or `phase-1a`; do not commit directly
to `main` or push unless asked. Make small coherent conventional commits, e.g.
`feat(agent): add context budget` or `test: cover invalid tool call`. PRs should
state the phase scope, summarize changes, link relevant issues or ADRs, and list
the exact verification results plus anything unverified.
