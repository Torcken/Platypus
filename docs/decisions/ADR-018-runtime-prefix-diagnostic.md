# ADR-018: Diagnose native prompt-prefix changes

**Status:** accepted — 2026-11-24

## Decision

When `PLATYPUS_DEBUG_PREFIX=1` is set, the llama-server backend compares the rendered leading
system prompt across generations. A change emits a warning to standard error and updates the
comparison point. The diagnostic is opt-in so normal terminal output remains unchanged. Its scope
is the stable system portion before user/task turns, not the full prompt.

## Verification limits

Offline formatter tests assert byte-identical native prefixes across changing user turns, and the
loopback backend test verifies a changed system prefix emits the warning. This diagnoses prompt
construction; it does not measure server-side KV-cache reuse.
