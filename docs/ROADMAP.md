# Platypus — Roadmap

Read `CLAUDE.md` first: it holds the rules that apply to every phase. This file says **what** to
build in each phase and how the phase is judged.

Each phase has an **exit criterion**. Criteria marked *(human)* need real models and hardware; the
human measures them with the benchmark. Your job for those is the tooling and a written test
protocol in the phase report.

Status audited 2026-11-24: benchmark/core implementation has resumed. No whole-phase human
exit criterion is satisfied yet. Unchecked compound items can contain working components;
the progress records identify their remaining requirements.

Phase 1 is split into sub-phases (1a–1d) because it does not fit in one session. Treat each
sub-phase as a phase: its own plan, progress file, branch and report.

---

## Model facts

Facts from the model cards that affect the design. Profiles must encode them; code must not
hardcode them. Re-check them against the model card when you write a profile, and mark anything
you cannot check `UNVERIFIED` (see `CLAUDE.md`, section 3).

- **Ornith 1.0 9B** is a reasoning model: by default the assistant turn opens with a
  `<think> ... </think>` block. Its reference serving recipe uses vLLM's `qwen3_coder` tool-call
  parser, which is an XML-style syntax, not JSON. Its chat template is an adjusted Qwen template
  published with the model (`chat_template.jinja`).
- **Gemma 4** (E4B, 12B) has a thinking mode, native function calling with its own syntax, and
  native system-prompt support. Max context: 128K (E4B), 256K (12B). On our hardware the usable
  context stays 16k–32k.
- **Memory:** Gemma 4 12B at Q4_0 needs about 6.7 GB before the KV cache, so it does not fit the
  6 GB tier. 12B at Q6 with a 32k context is tight on 12 GB; KV cache quantization
  (`--cache-type-k q8_0 --cache-type-v q8_0`) is the first lever.

Consequences:

1. A profile describes a **tool-call syntax family** (for example `json_tagged`, `qwen_xml`,
   `gemma`), not just delimiters. Each family is one codec in `src/llm/` that can render tool
   definitions, parse tool calls, and (phase 2) generate a grammar.
2. A profile says how to handle **reasoning blocks**: turned on or off, and whether they are kept
   in the history. Decide the default in an ADR during phase 1a. The trade-off: keeping them costs
   context; dropping them must happen *when the turn is appended* (never by rewriting older
   history), so the prefix stays stable up to the last assistant turn.

---

## Phase 0 — Benchmark harness

**Goal:** measure what makes local models fail, before writing the agent.
**Language:** Python 3.11+, standard library only. No C++ in this phase.
**Concept docs to write:** `tokens-and-context.md`, `kv-cache-and-prefill.md`,
`measuring-agents.md` (why runs are noisy, why we repeat them, how to read an interval).

Follow `docs/benchmark-protocol.md` for task trust, isolation limits, recording redaction, and
reproducibility metadata. The server version is pinned by ADR-002.

### 0.1 Measurement architecture: a recording proxy

Third-party clients do not report prompt tokens, prefill time or invalid tool calls. Do not parse
their logs. Instead, every run goes through **`bench/proxy.py`**, an HTTP proxy that sits between
the client and `llama-server`:

```
client under test ──HTTP──▶ bench/proxy.py ──HTTP──▶ llama-server
                              │
                              └── writes one JSONL record per request/response
```

- Forwards `/v1/chat/completions`, `/completion`, `/v1/completions`, `/health`, `/tokenize`, and
  passes everything else through unchanged.
- Supports streaming (SSE): forwards chunks as they arrive, reassembles the full response for the
  record.
- Records per request: timestamp, endpoint, request body, reassembled response, and llama-server's
  `timings` block (`prompt_n`, `cache_n`, `prompt_ms`, `predicted_n`, `predicted_ms`). Check field
  names against the README of the pinned llama.cpp version.
- The same recordings later become `FakeBackend` fixtures in phase 1, so keep the format simple and
  documented (`bench/RECORDING_FORMAT.md`).

### 0.2 Metric definitions

All metrics are computed from the proxy recordings and the run outcome, the same way for every
client.

| Metric | Definition |
| --- | --- |
| `success` | `verify.sh` exits 0 within the task timeout. |
| `wall_time_s` | From client start to client exit (or timeout). |
| `turns` | Number of completion requests in the run. |
| `prompt_tokens_total` | Sum over requests of `prompt_n + cache_n`. |
| `prompt_tokens_processed` | Sum of `prompt_n` (tokens actually prefilled). |
| `cache_hit_ratio` | `sum(cache_n) / prompt_tokens_total`. Measures prefix stability. |
| `prefill_ms` | Per request `prompt_ms`; report total and median. |
| `invalid_tool_calls` | Responses where the model tried to call a tool and failed: tool-call markers in the text but no parsed call, arguments that are not a JSON object, a tool name not in the request's tool list, or a required property missing / top-level type wrong (minimal checker, no external schema library). |
| `failed_edits` | Calls to the client's edit-like tools whose result (seen in the next request's tool message) is an error. Each adapter declares its edit tool names and an error pattern. |
| `loops` | Number of times the same action occurs a 4th time or more in a run. Same action = same tool name + canonical arguments (JSON with sorted keys, whitespace removed). |

Metrics that do not apply to a client (for example tool calls for a client that edits through
text) are recorded as `null` and shown as `n/a`, never as 0.

### 0.3 Tasks and repositories

- Task format in `bench/tasks/<id>/`: `task.toml` (repository, commit, instruction, category,
  timeout) and `verify.sh` (exit code 0 = success).
- `repository` is either a URL (cloned by `bench/fetch.py` into `bench/.cache/repos/`, run by the
  human with network) or a local path under `bench/repos/`. The runner never needs network.
- Provide **5 example tasks** on small repositories stored under `bench/repos/` (plain source
  trees; the runner makes them git repos at run time). Vary the categories: bug fix, small feature,
  refactor, test writing, multi-file change. At least one C++ and one Python repository. The human
  writes the other 15 from their own project.

### 0.4 Runner — `bench/run.py`

For each (task, client, model config, repeat): restore a fresh copy of the repository at the given
commit, start the proxy, run the client through its adapter with a timeout, run `verify.sh`, and
append one JSON line to `bench/results/<date>-<label>.jsonl`.

- `--repeats N` (default 3). Runs at temperature > 0 are noisy; one run per task is not a
  measurement.
- Records the configuration with each line: client and version, model file, quantization, context
  size, GPU layers, hardware tier label, llama.cpp version.
- A failing or hanging client must never stop the whole run: record the failure and move on.

### 0.5 Client adapters — `bench/adapters/`

One small module per client: how to launch it non-interactively, pointed at the proxy URL, with
the task instruction, in the repository directory; plus the edit tool names and error pattern for
`failed_edits`.

- Required: **opencode**, and the second client named in the session prompt.
- Take CLI flags from the client's `--help` if it is installed. If it is not, write the adapter,
  mark the flags `UNVERIFIED`, and list them in the report.
- A **fake client** (`bench/adapters/fake.py`) plays a scripted sequence of requests. It is used
  by the tests together with a **fake llama-server** (`bench/tests/fake_server.py`) that returns
  canned responses, including `timings`.

### 0.6 Edit-format experiment — `bench/edit_formats.py`

Sends the same editing tasks directly to `llama-server` with three edit formats and compares
success rates: unified diff, exact string replacement, search/replace blocks with fuzzy matching.

- Write 10 small edit tasks in `bench/edit_tasks/<id>/`: `before/` (one file), `instruction.txt`,
  `after/` (expected file). Vary languages and edit types (rename, add parameter, fix off-by-one,
  insert a function, change several separate places).
- One prompt template per format in `bench/edit_formats/`, each with one short example.
- Use `/v1/chat/completions` so the server applies the model's own template; remove reasoning
  blocks before parsing the edit.
- Success = the edit applies and the result equals `after/` after normalizing trailing whitespace.
- Fuzzy matching for search/replace: exact match first; then whitespace-normalized match; then the
  single best candidate with a `difflib` ratio ≥ 0.9. Several candidates above the threshold =
  failure (ambiguous). Record which level matched.
- `--repeats N` as in the runner.

### 0.7 Report — `bench/report.py`

Produces Markdown: one table per hardware tier, rows = client × model × quantization, columns = the
metrics of 0.2.

- Success rate with a **95 % Wilson interval**.
- Comparison between two configurations on the same tasks: **paired** per-task difference with a
  bootstrap 95 % interval (standard `random` module, fixed seed).
- A one-line warning when the number of runs is too small for the interval to exclude 0.

### 0.8 Documentation — `bench/README.md`

How to install, fetch repositories, start `llama-server`, run the benchmark and produce the report.
Include how to emulate a smaller tier on a larger card: fewer GPU layers (`-ngl`), smaller context
(`-c`), KV cache quantization.

### Exit criterion

- `python3 -m unittest discover -s bench/tests -v` is green.
- One end-to-end test runs the whole pipeline (runner → proxy → fake server → fake client →
  `verify.sh` → JSONL → report) with no network and no GPU.
- *(human)* The human produces the baseline report with real models.

---

## Phase 1 — MVP core with a `llama-server` client

**Goal:** an agent usable every day on a real project, safely.
**Exit criterion for the whole phase:** all tests green on all presets, the five integration
scenarios of `CLAUDE.md` section 9 covered. *(human)* Success rate at least equal to the best
phase-0 baseline at the same model and tier, and two weeks of real use with no lost work.

...
## Phase 1a — Skeleton and LLM layer (partially verified)
- [x] CMake presets, pinned CPM dependencies, `std::jthread` compile check, CI workflow and
   `.clang-format` configuration.
- [ ] Remote CI and clean clang-tidy run remain outstanding.
- [x] `util::Result<T>`, logging and token-estimate helpers.
- [x] Generation/UI worker ownership, stop-token cancellation and bounded process groups.
- [x] Fake and llama-server backends, SSE/raw transport, tokenizer/health requests and
   profile-driven client-side templates, including validated sampling propagation (ADR-016).
- [x] TOML Ornith/Gemma profiles cover syntax, delimiters, stop strings, sampling, reasoning and
   context limits.
- [ ] Per-tier tuning and real-model template/quality acceptance remain human work.

## Phase 1b — Tools and agent loop (partially verified)
- [x] JSON, Qwen XML and Gemma tool-call codecs; tolerant bounded parsing, with duplicate/depth
   checks, adversarial regressions and a libFuzzer entry/corpus/CI job. Actual local coverage-guided
   fuzzing remains unavailable until Clang/libFuzzer is installed.
- [x] Six bounded tools (`read`, `list`, `grep`, `edit`, `write`, `run`) with project-root
   containment, approval, snapshots, output limits and error recovery. Search is ripgrep-backed.
   The `EditFormat` strategy supports exact and regex; fuzzy matching waits for benchmark evidence.
- [x] Agent loop builds prompts, generates, parses/executes calls and appends observations; it stops
   on a final answer or after a configurable 25-step bound. Canonical context budgeting now counts
   rendered prompts, reserves reply tokens and fails closed on tokenizer errors (ADR-012).
- [x] **Headless mode:** `platypus run --headless --yes "<instruction>"` runs one task without the
   TUI. Integration tests and the bench adapter use it.
- [x] Integration tests: successful edit, recovered invalid tool call, retried failed edit, stopped
   loop, native profile sessions and streamed tool-call batches.

## Phase 1c — Safety and sessions

- [x] Durable shadow Git snapshots, approved diffs, undo, path containment and command confirmation.
- [x] Append-only SQLite sessions and resume.
- [x] SnapshotStore API, invalid/corrupt restore and recovery tests, approval-time change checks.
- [ ] Broader filesystem failure/concurrency validation and sustained use without lost work.

## Phase 1d — TUI and bench adapter

- [x] Minimal FTXUI history/input, streamed generation, approval prompts and cancellation.
- [x] Headless Platypus benchmark adapter and architecture document.
- [x] Context status from profile budgets; native tokenizer counts and explicit fallback estimates.
- [ ] Sustained interactive testing and real-model benchmarks.

## Stabilization & Foundation (Current Phase)
- [x] Enhance CMake configuration (ctest, presets).
- [x] Address safety contract (snapshots, diffs, `run_command` safety).
- [x] Resolve CLI/Factory mismatch.
- [x] Update documentation.
- [x] Refine production toolset and error handling.

These checkmarks record the resumption milestone, not completion of the whole MVP.
See `docs/plans/phase-stabilization-progress.md` for remaining code and human exit criteria.

## Phase 2 — Reliability with small models
...
Concept docs: `grammar-constrained-decoding.md`, `context-management.md`.

1. **Grammar:** a GBNF generator per tool-call codec, from each tool's JSON schema. The output
   grammar allows either free text or exactly one valid tool call in the profile's syntax. Sent in
   the `grammar` field. Unit tests on the generated grammars.
2. **Parser hardening:** the phase-1 tolerant parser stays as the fallback when grammar is off or
   unsupported. Extend it with every failure seen in phase-1 recordings, each as a regression test.
3. **Repository map** with tree-sitter (C, C++, Python, JavaScript/TypeScript, Rust, Go): files,
   classes, functions, signatures, ranked by identifier overlap with the request and recent
   history. Inserted **after** the stable prefix, never inside it.
4. **Context manager:** token budget per tier, tool-output truncation, compaction only at task
   boundaries, recent turns kept verbatim.
5. [x] **Prefix stability check:** `PLATYPUS_DEBUG_PREFIX=1` warns when the rendered stable system
   prefix changes between generations; native tests assert the stable prefix and diagnostic.
6. [x] **Loop guard:** block the fourth and later identical actions (same definition as phase 0)
   and return a corrective observation (ADR-017).

Exit criterion *(human)*: under 1 % invalid tool calls; at least +10 points of success rate over
the best baseline; at least 30 % fewer prompt tokens per task. Each target counts as met only if
the paired 95 % interval from `report.py` supports it.

---

## Phase 3 — Hybrid workflow

1. Plan mode, on by default: the model proposes steps, the user approves or edits them.
2. `PLAN.md` with milestones and checkbox tasks, read and updated by the agent.
3. Import an external plan (pasted or from a file) into `PLAN.md`.
4. Automatic checks after each task: detect the build system (CMake, Cargo, npm, pytest, Go),
   override in `.platypus.toml`. Errors go back to the model, at most 3 attempts before handing
   back to the user.
5. Review bundle at each milestone: diff since the previous milestone, plan excerpt, check results,
   decisions taken. Sized to a target token budget, Markdown, copied to the clipboard.
6. Import review comments as new tasks in `PLAN.md`.
7. Optional remote provider (OpenAI-compatible and Anthropic Messages) for plan and review, with
   the estimated cost shown before sending.
8. Commands: `/plan`, `/milestone`, `/review`, `/import`.

Exit criterion: integration tests for the full plan → execute → check → bundle → import cycle with
`FakeBackend`.

---

## Phase 4 — Zero configuration and distribution

Windows support is decided (ADR) before this phase starts.

1. Hardware detection: GPU, VRAM and architecture (NVML for Nvidia, Vulkan for AMD and others,
   sysctl/Metal on macOS), RAM, CPU.
2. Model catalog (versioned JSON): quantizations, sizes, minimum tier, profile, SHA-256.
3. Automatic recommendation shown as "fast", "balanced", "quality".
4. Download from Hugging Face with resume, progress bar and checksum.
5. Managed `llama-server`: bundled binary, started and health-checked by Platypus, restarted with a
   smaller context on out-of-memory.
6. CI build matrix: Linux x86_64 (CUDA 12, CUDA 13, Vulkan, CPU), macOS arm64 (Metal). CUDA 12 is
   required for Volta GPUs such as the Titan V. Automatic backend choice on first launch.
7. One-line install script and Homebrew formula; `platypus update`.
8. First-run wizard: detect, choose, download, test on a sample task.

Exit criterion *(human)*: successful install without help on 5 different machines, including an
AMD 6 GB GPU, an M1 16 GB and a Titan V.

---

## Phase 5 — Public beta and v1.0

1. UX polish: prefill progress and time estimate, context gauge in plain words, `/` command
   palette, help bar, error messages that propose or apply a fix.
2. Rule-based permissions (action, resource, effect) in `.platypus.toml`, safe defaults.
3. English and French UI strings through a simple message catalog.
4. User docs: install, first project, writing good requests, what a 9B model does well and badly,
   the hybrid workflow.
5. Opt-in failure log export for beta testers. No automatic telemetry.

Exit criterion *(human)*: at least 70 % of beta testers get a useful first result without help,
and no lost work reported in the last two beta weeks.

---

## Phase 6 — Embedded llama.cpp (after v1.0, conditional)

Only if the benchmark shows a need. `LlamaCppBackend` implementing `LlmBackend`, KV cache saved to
disk per session, real prefill progress, speculative decoding experiments (Gemma 4 ships
multi-token-prediction drafters). `llama-server` stays as a fallback.
