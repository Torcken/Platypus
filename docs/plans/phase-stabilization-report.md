# Development resumption checkpoint — 2026-11-24

## Latest continuation — streamed native tool calls

The llama-server streaming path now assembles indexed OpenAI tool-call deltas, including fragmented
names and arguments, and returns one canonical tool-call payload to the agent. That payload replaces
interim stream text for execution and is not sent to the visible token callback. Incomplete batches
are rejected, and SSE `data:` lines with or without one optional space are accepted. The loopback
fixture verifies multiple calls, fragmented deltas, embedded JSON in interim text, ordinary text
streaming, and incomplete-batch rejection.

Verification for this continuation: Python 41/41; dev, ASan and UBSan each 9/9 CTest groups. The
sanitizer suites were run with the permissions needed by LeakSanitizer and the loopback fixtures.

## Built

- Restored the executable and dev/ASan/UBSan presets with pinned CPM dependencies and a real
  std::jthread compile check. Registered all original C++ test sources and kept assertions active.
- Implemented actual libcurl HTTP/SSE, raw completion, health/tokenizer requests, validated TOML
  profiles, native syntax parsing, reasoning handling, recording-format FakeBackend replay and
  a stable-template-prefix regression.
- Repaired headless invocation, backend ownership, malformed-call recovery, failed-tool retry,
  argument validation, conservative context limits and six bounded tools.
- Added explicit approvals, atomic file replacement, full shadow-Git snapshots, durable undo
  history, permission/empty-directory metadata, excluded user Git/state paths, and bounded
  cancellable process groups. SQLite sessions are append-only and resumable.
- Added FTXUI streaming/input/history/approval/cancellation, a pseudoterminal smoke test and
  full CLI integration tests. No detached generation worker can outlive its UI.
- Replaced fabricated benchmark results with subprocess execution, disposable repositories/HOME,
  streaming proxy recordings, maintained verification, derived metrics, JSONL retention, Wilson
  and paired bootstrap reporting. Added five tested tasks, ten edit fixtures, real edit appliers,
  warm-up exclusion and randomized task/client matrix execution.
- Reconciled stale roadmap claims, restored the safety/session/TUI subphase records and documented
  the actual architecture. AGENTS.md and pre-existing build_e2e changes were preserved.

## Verification

Commands executed on the final implementation (socket/ASan tests required sandbox escalation):

```sh
python3 -m unittest discover -s bench/tests -v
cmake --preset dev && cmake --build --preset dev && ctest --preset dev --output-on-failure
cmake --preset asan && cmake --build --preset asan && ctest --preset asan --output-on-failure
cmake --preset ubsan && cmake --build --preset ubsan && ctest --preset ubsan --output-on-failure
rg --files src tests -g '*.cpp' -g '*.hpp' -0 | xargs -0 clang-format --dry-run --Werror
git diff --check
```

Python: 23 tests passed. C++/integration: nine CTest groups per preset; final results recorded in
phase-stabilization-progress.md. The groups include CLI write/deny/undo/resume and actual PTY
startup/exit; Catch2 regressions cover recovery, cancellation, modes, symlinks and directory undo.
A scoped clang-tidy run completed but reported style/modernization diagnostics; the code is not
lint-clean. Linux/macOS CI is configured but has not been run remotely. No GPU or real model was
used, and no external model service received repository content.

## Decisions and deviations

ADRs 005–008 record the explicit implementation authorization, process-group wrapper, reasoning
history policy and macOS toolchain proposal. Existing core names remain during canonical API
migration. POSIX process groups are used because reproc lacks the required descendant cleanup API.
The generic transport uses server-side templates; explicit profiles use client-side raw completion.
The default edit tool remains exact/regex; the fuzzy benchmark implementation is not promoted
without model measurements. Phase 6 remains post-v1.0 and conditional.

## Unverified values and limitations

- Aider flags must be checked with installed --help. OpenCode CLI help and official provider docs
  were consulted, but real-client/server compatibility and sampling propagation remain unverified.
- Ornith/Gemma delimiters were read from publisher templates; full native tool-history parity,
  reasoning quality, BOS behavior and per-tier tuning still need actual model runs.
- Baseline metadata must include verified binary/model hashes, versions, launch/sampling settings
  and task identities before interpreting gains. The current runner records supplied metadata but
  does not enforce completeness for every real-model run.
- Finish EditFormat/fuzzy editing, ripgrep-backed search, edit-format fuzzing, a local coverage-guided
  campaign, sampling propagation checks, and lint cleanup before closing Phase 1.
- Filesystem race/failure injection, interrupted restore recovery and sustained interactive behavior
  need broader validation. The CLI/TUI are functional development software, not a v1.0 release.
- Phases 2–5 remain unimplemented. Their human installation/beta/usage exit criteria cannot be
  inferred from unit tests or satisfied by this checkpoint.

## Manual baseline protocol and continuation

1. Supply a server built from the ADR-002 commit and a target GGUF. Record --version, SHA-256,
   hardware, quantization, launch options, context, GPU layers, cache types and sampling in JSON.
   The installed server here reports version 0/unknown and cannot initialize its GPU backend.
2. Start that server on the provided endpoint. Run all five maintained tasks plus the fifteen
   human project tasks with OpenCode and aider, three or more repeats, one excluded warm-up.
3. Run the same tasks with --client platypus and an explicit validated profile. Produce the report
   and edit-format comparison. Freeze the task/configuration set for paired comparisons.
4. Exercise terminal approvals, denied writes/commands, Ctrl-C/Escape, /undo and --resume in a
   disposable real project. Keep the two-week no-lost-work acceptance log separately.
5. Complete the remaining Phase 0/1 software checks before implementing/promoting the Phase 2
   reliability changes. Native compatibility and a verified model/server endpoint are the next
   required external inputs; the user has been asked for these asynchronously.
