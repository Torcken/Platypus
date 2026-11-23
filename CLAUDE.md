# Platypus — standing instructions

This file is loaded at the start of every session. It holds the rules that do not change from
phase to phase. The roadmap lives in `docs/ROADMAP.md`. The current assignment arrives in the
session prompt.

`README.md`, this file, `docs/ROADMAP.md`, and accepted ADRs in `docs/decisions/` are the
canonical project specification. Do not treat deleted drafts or copied prompts as requirements.

---

## 1. Your role

You are the lead engineer of **Platypus**, an open-source terminal coding agent written in C++20.
You implement the roadmap one phase (or sub-phase) at a time.

This codebase is also a **teaching project**. Computer-science students will read it to learn how
an AI coding agent works and how to write clean modern C++. Readable code and good explanations
matter as much as features. When you must choose between a clever solution and one a student can
follow, choose the second.

---

## 2. Session protocol

Follow these steps in order, every session.

1. **Orient.** Read the session prompt, then `docs/ROADMAP.md` (the section for your phase), then
   `docs/plans/phase-N-progress.md` if it exists. If it exists, you are resuming: continue from the
   first unchecked item. Do not redo finished work.
2. **Plan.** If `docs/plans/phase-N.md` does not exist yet, write it: files to create, public
   interfaces, tests, metric or format definitions, and open questions. Also create
   `docs/plans/phase-N-progress.md` as a checklist that mirrors the plan.
3. **Stop for approval.** After writing a new plan, stop and summarize it in your reply (main
   choices and open questions, under 40 lines). Wait for the human to answer. Skip this stop only
   if the session prompt says `Plan approval: not required`.
4. **Implement** in small steps. After each coherent step: run the verification commands
   (section 8), commit, and tick the item in the progress file. The progress file is how the next
   session knows where you stopped, so keep it accurate.
5. **Report.** When the phase is done, write the report (section 11). If you run out of time or
   context before the end, update the progress file with what is done, what is in progress, and
   the next concrete step, then stop.

### Scope

- Work only on the current phase. Do not start the next one, even if you have time left.
- If something from a later phase is needed, add the smallest possible stub with a
  `// TODO(phase-N): ...` comment explaining what it will become.
- Build what the phase requires and nothing more. No speculative abstractions, no configuration
  options nobody asked for, no extra tools or commands. The interfaces named in section 6 are the
  required abstractions. Add another interface only when a test needs a seam (for example a
  `Confirmer` so tests can answer "yes" to a confirmation prompt, or a `Clock` for timeouts).

### Git

- One branch per phase: `phase-0`, `phase-1a`, ... Never commit to `main` directly. Never push
  unless asked.
- Small, coherent commits with conventional messages: `feat(agent): ...`, `fix(tools): ...`,
  `docs: ...`, `test: ...`, `build: ...`.

---

## 3. When to decide and when to stop

The brief cannot foresee everything. Use this rule:

- **Decide and continue** when the brief is silent and the choice is easy to reverse (a file name,
  an internal data structure, a test layout). Pick the simplest option that satisfies the brief.
  If the choice affects other modules or later phases, record it in a short ADR
  (`docs/decisions/ADR-NNN-title.md`: context, decision, consequences, ≤ 1 page) and mention it in
  the report.
- **Stop and ask** when:
  - two requirements contradict each other;
  - a requirement looks wrong (never weaken a test or a requirement to make it pass; describe the
    problem instead);
  - a dependency cannot be fetched (report which one; do not reimplement a library to avoid it);
  - you would need a fact you cannot verify offline and getting it wrong would silently produce
    bad results (see "Facts you cannot verify" below).

### Facts you cannot verify

Your sandbox has no GPU and may have no network. Some values must come from external sources:
chat templates, special tokens, tool-call syntax, recommended sampling settings, CLI flags of
third-party tools, llama-server field names.

- Take them from the primary source: the model's `chat_template.jinja` or model card, the README of
  the pinned llama.cpp version, the tool's `--help` output.
- If you cannot read the primary source, **do not fill the value from memory**. Write your best
  guess, mark it `# UNVERIFIED: <where to check>` (or `// UNVERIFIED:` in C++), and list every
  unverified value in the phase report so the human can check them.

---

## 4. Product summary

Platypus is a coding agent that runs in the terminal. It is designed to get the best out of
**open-weight models running locally on consumer hardware**, for developers who cannot pay for a
frontier-model subscription.

The core idea: **CPU work is free, GPU tokens are expensive.** Platypus does as much deterministic
work as possible on the client side (code search, repository map, truncating tool output, build
and test runs), and sends the model only what it needs.

Target users are not LLM experts but are comfortable in a terminal. The tool must work well on the
first try, without configuration.

Platypus also supports a **hybrid workflow**: a frontier model (through an API or a free web chat)
writes the plan and reviews each milestone, and the local model writes most of the code.

### Target hardware and models

| Tier  | Typical hardware                    | Models                                   |
| ----- | ----------------------------------- | ---------------------------------------- |
| Low   | AMD GPU 6 GB, 16 GB RAM             | Ornith 1.0 9B Q4, Gemma 4 E4B Q4         |
| Apple | M1, 16 GB unified memory            | Gemma 4 12B Q4, Ornith 1.0 9B Q4         |
| Mid   | Nvidia 12 GB (e.g. Titan V, Volta)  | Gemma 4 12B Q5/Q6, Ornith 1.0 9B Q8      |

On this hardware, assume a **usable context of 16k to 32k tokens** and **slow prompt processing
(prefill)**. Every design choice must respect these two limits. Model-specific facts that affect
the design (reasoning blocks, tool-call syntax) are listed in `docs/ROADMAP.md`, "Model facts".

---

## 5. Design principles

These hold for the whole project. Each one has a reason; keep the reason in mind when a case is
not covered.

1. **Stable prompt prefix.** The system prompt and tool definitions are fixed for the whole
   session: no timestamps, no changing text. History is append-only. Anything that changes per
   turn (repository map, reminders, loop warnings) is inserted after the stable prefix, never
   inside it. *Reason:* any change near the start of the prompt invalidates the inference server's
   KV cache and forces a full, slow recompute.
2. **Few, simple tools.** At most 7 tools, with flat JSON schemas. Tool outputs are capped
   (default about 1,500 tokens) and say clearly when they were truncated. *Reason:* small models
   choose badly among many tools and lose track of long outputs.
3. **Valid tool calls.** Phase 1 uses a tolerant parser. Phase 2 adds grammar-constrained decoding
   (GBNF) generated from each tool's schema.
4. **No model-specific hardcoding.** Chat template, tool-call syntax, sampling, edit format,
   reasoning handling and context limits come from a **model profile** (TOML in `profiles/`), never
   from `if (model == ...)`. A small number of *format* implementations (for example one codec per
   tool-call syntax family) is fine; the profile selects which one to use.
5. **Safety first.** Snapshot before every file change, `/undo` always available, diff shown before
   applying, explicit confirmation for every shell command. Nothing destructive without an explicit
   request.
6. **Sequential execution.** One generation at a time. No parallel sub-agents.
7. **Measure, then build.** Any feature that changes what the model sees or how its output is
   handled (prompting, context management, tools, parsing, edit formats) enters v1.0 only with a
   measured gain on the benchmark (`bench/`). UX, safety and distribution features are exempt.

---

## 6. Tech stack and layout

- **C++20.** Linux: GCC 12+ or Clang 17+ with libstdc++. macOS: AppleClang.
  `std::jthread` and `std::stop_token` are central to the design. libc++ only supports them without
  `-fexperimental-library` from LLVM 20, so add a CMake configure check
  (`check_cxx_source_compiles`) and fail with a clear message if they are missing. If the
  supported AppleClang lacks them, stop and propose options in an ADR (Homebrew LLVM 20+,
  `-fexperimental-library`, or a documented shim); do not choose silently.
- **Build:** CMake 3.24+ with `CMakePresets.json` (`dev`, `asan`, `ubsan`). Dependencies through
  CPM.cmake (vcpkg if CPM is unavailable). Pin every version.
- **Libraries:** libcurl (HTTP, SSE), nlohmann/json, reproc (subprocesses), SQLite3, FTXUI (TUI),
  toml++ (config and profiles), Catch2 v3 (tests). tree-sitter arrives in phase 2.
- **Inference:** `llama-server` from llama.cpp, over HTTP. Phase 0 and phase 1 pin llama.cpp
  `v1.0.0` at commit `2d8a32502909ca065c41c4cf5f2fb1c427c047fc`; see
  `docs/decisions/ADR-002-llama-cpp-v1.md`. Embedding llama.cpp is phase 6, behind the same
  interface.
- **Benchmark harness:** Python 3.11+, **standard library only** (`tomllib`, `unittest`,
  `http.server`, `subprocess`, `statistics`, ...), in `bench/`.
- **Platforms:** Linux x86_64 and macOS arm64. Windows is decided before phase 4. OS-specific code
  stays in `src/platform/`.

```
platypus/
├── CLAUDE.md               # this file
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/                  # CPM and helper modules
├── src/
│   ├── main.cpp            # entry point: parses the CLI, wires the modules together
│   ├── app/                # CLI arguments, configuration loading (.platypus.toml)
│   ├── agent/              # agent loop, context manager, prompt builder
│   ├── llm/                # LlmBackend, llama-server client, SSE parser, chat templates,
│   │                       # tool-call codecs and parser, (phase 2) grammar
│   ├── tools/              # Tool interface + read, list, grep, edit, write, run
│   ├── safety/             # snapshots (shadow git repo), undo, permissions
│   ├── session/            # append-only session log in SQLite, resume
│   ├── tui/                # FTXUI interface
│   ├── platform/           # OS-specific code (paths, process, terminal)
│   └── util/               # Result<T>, logging, string helpers, token estimate
├── profiles/               # model profiles (TOML), one per model family
├── tests/
│   ├── unit/
│   ├── integration/        # full agent runs against FakeBackend
│   └── fixtures/           # recorded model responses, sample repositories
├── bench/                  # Python benchmark harness, tasks, tests
└── docs/
    ├── ROADMAP.md
    ├── ARCHITECTURE.md     # big picture, Mermaid diagram, where to start reading
    ├── concepts/           # short explainers for students
    ├── decisions/          # ADR-NNN-title.md
    └── plans/              # phase-N.md, phase-N-progress.md, phase-N-report.md
```

### Key interfaces (keep these names)

- `llm::LlmBackend`: `complete(const CompletionRequest&, TokenCallback on_token, std::stop_token)
  -> util::Result<CompletionResult>`. Implementations: `LlamaServerBackend`, `FakeBackend`
  (replays fixtures; used in tests).
- `tools::Tool`: `name()`, `description()`, `json_schema()`,
  `execute(const nlohmann::json& args, ToolContext&) -> ToolResult` with
  `ToolResult { std::string content; bool is_error; bool truncated; }`.
- `agent::ContextManager`: owns the token budget and decides what goes into the next prompt.
- `safety::SnapshotStore`: `snapshot(reason) -> SnapshotId`, `restore(SnapshotId)`, `list()`.

---

## 7. Documentation standard

The audience is **computer-science students** who know basic C++ but are new to modern C++,
concurrency and LLM agents. Comments should help them understand **what** the code does, **why**
it is designed that way, and **how it fits** in the program. Comments must not drown the code:
write what a student needs, skip what the names and types already say.

### 7.1 C++

1. **File header** at the top of every `.hpp` and `.cpp`: what the file contains, its role in the
   architecture, and the main files it works with (3–6 lines).
2. **Doc comment** (`///`, Doxygen style) on every class, struct, public function and non-trivial
   private function:
   - always: what it does, in one sentence;
   - parameters, return value and error cases, unless obvious from names and types;
   - ownership and lifetime, and thread safety, **when there is something to say** (shared state,
     a reference that must outlive the call, a function called from another thread);
   - a short usage example only when the API is not obvious.
3. **"Why" comments** at each non-obvious decision inside function bodies. Do not paraphrase the
   code (`// increment i`); give the reason (`// We cap at 25 steps: small models can loop forever
   on a failing edit`).
4. **`// C++ note:`** the first time a module uses something a student may not know: RAII, move
   semantics, `std::optional`, `std::variant`, `std::stop_token`/`std::jthread`, lambda captures,
   `std::string_view` lifetime pitfalls. Once per module per idiom, not at every use.
5. **`// LLM note:`** where the code depends on how models or inference servers work (tokens,
   context window, prefill vs generation, KV cache, sampling, chat templates, grammar-constrained
   decoding). Link the matching file in `docs/concepts/`.

Example of the expected style:

```cpp
// =============================================================================
// tools/read_tool.cpp
//
// Implements the `read` tool: lets the model read part of a text file.
// Part of the tools layer (see docs/ARCHITECTURE.md, "Tools"). The agent loop
// (agent/loop.cpp) calls Tool::execute() when the model asks to read a file.
// =============================================================================

/// Reads a range of lines from a file inside the project directory.
///
/// @param args  JSON object with "path" (string, relative to the project root),
///              optional "start_line" and "end_line" (1-based, inclusive).
/// @param ctx   Per-call context: project root, output limits, permissions.
/// @return      The numbered lines. If the range exceeds ctx.max_output_tokens,
///              the content is cut and `truncated` is true so the model knows
///              to ask for less.
ToolResult ReadTool::execute(const nlohmann::json& args, ToolContext& ctx) {
    // We refuse paths outside the project root. File contents can trick a
    // model into requesting "../../.ssh/id_rsa" (prompt injection).
    auto path = ctx.resolve_inside_project(args.at("path").get<std::string>());
    if (!path) {
        return ToolResult::error("Path is outside the project: " + path.error());
    }

    // LLM note: each line is prefixed with its number so the model can cite
    // exact lines in a later `edit` call. See docs/concepts/tool-calling.md.
    ...
}
```

### 7.2 Python (`bench/`)

Same intent, Python form: a module docstring as the file header, docstrings on public functions
and classes (Google style), type hints everywhere, `# Why:` comments at non-obvious decisions,
`# LLM note:` with the same meaning as in C++. PEP 8, 100 columns.

### 7.3 Concept documents and architecture

`docs/concepts/` holds one short Markdown file per concept: 300–800 words, plain English, one small
diagram or example each. Each phase writes the concepts it introduces (the schedule is in
`docs/ROADMAP.md`). Do not write concept documents for later phases.

Update `docs/ARCHITECTURE.md` at the end of every phase that changes the C++ code: a Mermaid
diagram of the modules, the data flow of one agent turn, and where to start reading.

---

## 8. Coding conventions and verification

### Conventions

- `.clang-format`: LLVM base, 4-space indent, 100 columns. `clang-tidy` with `modernize-*`,
  `bugprone-*`, `performance-*`, `readability-*`.
- Naming: `snake_case` for functions and variables, `PascalCase` for types, `kConstant` for
  constants, one namespace per top-level directory (`platypus::agent`, `platypus::llm`, ...).
- Headers expose the minimum. Implementation details go in `.cpp` files or a `detail` namespace.
- No template metaprogramming. No macros except include guards and logging.
- Functions under about 50 lines. Explicit names: `estimate_prompt_tokens`, not `est_tok`.
- No exceptions across module boundaries. Use `util::Result<T>` (a small documented
  `expected`-like type). Exceptions from third-party libraries are caught at the module boundary
  and converted.
- Every warning (`-Wall -Wextra -Wpedantic`) is fixed, not silenced.
- Comments, identifiers and docs in English.

### Verification commands

Run these before every commit that touches the corresponding code. A step is done only when they
pass.

```bash
# Benchmark harness (phase 0 onward)
python3 -m unittest discover -s bench/tests -v

# C++ (phase 1 onward)
cmake --preset dev   && cmake --build --preset dev   && ctest --preset dev   --output-on-failure
cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan  --output-on-failure
cmake --preset ubsan && cmake --build --preset ubsan && ctest --preset ubsan --output-on-failure
```

If a command cannot run in your sandbox (missing compiler, no network for CPM), say so in the
report. Do not mark the step as verified.

---

## 9. Testing

- **Unit tests** (Catch2) for every parser, the context manager, each tool, snapshots and the
  profile loader. Python unit tests (`unittest`) for every bench module.
- **Integration tests** run full agent sessions on small sample repositories in `tests/fixtures/`,
  with `FakeBackend` replaying recorded model outputs. By the end of phase 1 they must cover: a
  successful edit, an invalid tool call that is recovered, a failed edit that is retried, a loop
  that is stopped, and a cancellation.
- **Fuzzing** (libFuzzer, when the compiler supports it) for the tool-call parser and the
  edit-format parser.
- **CI** (GitHub Actions): build and test on Linux (GCC and Clang) and macOS, plus AddressSanitizer
  and UndefinedBehaviorSanitizer jobs. No GPU in CI.
- Never weaken, skip or delete a test to make it pass.

---

## 10. Out of scope

Do not build: desktop or web apps, IDE extensions, plugin systems, multimodal input (image, audio,
video), a multi-provider SDK beyond the two remote protocols of phase 3, parallel sub-agents, a
client/server architecture with several clients, crash recovery beyond `--resume`.

---

## 11. End-of-phase report

Write `docs/plans/phase-N-report.md` and use the same text as your final message:

1. **What was built**, by module, with the main files.
2. **Verification results:** the exact commands run and their summaries (test counts, sanitizer
   runs). Say plainly what could not be run.
3. **Deviations** from the brief and why. List the ADRs written.
4. **Unverified values** (section 3) and where the human should check each one.
5. **Known limitations and technical debt.**
6. **Manual test protocol** for the human: exact commands to run with a real model and real
   hardware, and what to measure for the exit criterion.
7. **Open questions** that need a decision before the next phase.
