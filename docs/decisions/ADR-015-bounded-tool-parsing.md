# ADR-015: Bounded native parsing and fuzz coverage

Status: accepted (2026-11-24).

## Context

Qwen extraction used recursive standard-library regex matching over arbitrary argument text.
Gemma extraction returned only the first call. Native parse failures could fall through to JSON
inside argument data, and duplicate keys or excessive nested input were not explicitly refused.

## Decision

Use literal tag scanning for Qwen. Parse every member of a Gemma batch. A malformed native member
rejects the whole batch without reinterpreting embedded JSON as another call. Keep tolerant JSON
closure/trailing-comma repairs, but reject duplicate keys, invalid UTF-8 arguments and oversized or
deep trees. Bound each response to 64 KiB, JSON depth to 64 and batches to 64 calls. Apply nesting
and duplicate-key checks again when JSON arguments arrive encoded as a string. These bounds are
well above the loop's ordinary 2048-token output reservation.

The nlohmann 3.11.3 parser callback rejects excessive depth before constructing an unbounded tree;
key events use the parsed object depth, one greater than its object-start event. This behavior was
checked against the vendored version's parser_callback_t documentation and json_sax implementation.

Add a shared fuzz entry for all three syntax families, seed corpus, a Clang/libFuzzer CMake target
with ASan/UBSan and a CI smoke campaign. Invoke the same entry with 2,000 deterministic mutations
in ordinary CTest runs. Keep benchmark scoring strict and independent of tolerant repairs.

## Consequences

Focused tests cover long native strings, deep and encoded JSON, duplicate keys, malformed native
batches with embedded calls, multiple Gemma calls and invalid bytes. Local GCC and sanitizer
checks exercise the entry point. Clang/libFuzzer is unavailable locally: the actual coverage-guided
target and CI campaign remain unverified. The edit-format fuzz target is still pending. This is
parser correctness/resource hardening; it does not establish real-model success-rate gains.
