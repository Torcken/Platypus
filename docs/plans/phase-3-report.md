# Phase 3 Report: Hybrid Workflow

## Delivered

Platypus now covers plan proposal and approval, validated `PLAN.md` milestones and imports,
approved checks with bounded repair attempts, milestone baselines and review bundles, persisted
decisions, review-comment imports, and clipboard copy. Optional remote plan and review calls support
OpenAI-compatible Chat Completions and Anthropic Messages. Their endpoint, model, rates, and output
limit are explicit in `.platypus.toml`; credentials are environment-only. Every send displays a
configured-rate estimate and requires approval. Local inference remains responsible for coding
tasks. ADR-024 records the boundary and estimate limitations.

## Verification

- `cmake --build --preset dev -j2` and `ctest --preset dev --output-on-failure`: passed, 9/9.
- `cmake --build --preset asan -j2` and `ctest --preset asan --output-on-failure`: passed, 9/9.
- `cmake --build --preset ubsan -j2` and `ctest --preset ubsan --output-on-failure`: passed, 9/9.
- `python3 -m unittest discover -s bench/tests -v`: passed, 41/41.
- Remote provider regression coverage uses injected fake transports; no external request or key was
  used. Formatting and `git diff --check` passed.

## External Evidence Still Needed

The full offline plan → execute → check → review → decision → comment-import cycle satisfies the
Phase 3 software exit criterion. Live OpenAI-compatible and Anthropic requests, provider billing
versus the configured-rate estimate, real-model planning/review quality, and clipboard availability
on supported desktop environments have not been verified. These checks require a human-supplied
account, current model rates, real model/server, and target desktop platforms. Later roadmap human
criteria (real baseline, sustained use, hardware installs, and beta outcomes) remain open.
