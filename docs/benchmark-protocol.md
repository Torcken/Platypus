# Benchmark Protocol

This protocol applies to every Phase 0 task, run, recording, and report.

## Trusted inputs and isolation

Only maintained task fixtures and repositories fetched by a human from a pinned URL and commit may
run. The runner never fetches from the network. It creates a fresh disposable working copy per run,
uses an empty temporary home directory, and supplies a minimal environment with no credentials,
SSH agent, or user configuration. `verify.sh` is executed only from the maintained task fixture,
with its timeout enforced. The harness is not a security sandbox: use a disposable VM or container
for untrusted repositories, and record that isolation in the result metadata.

## Recording and retention

The proxy removes `Authorization`, `Cookie`, `Set-Cookie`, and configured secret-pattern values
before writing a record. Raw recordings, task clones, and JSONL results are local-only and ignored
by Git. A recording may become a versioned `FakeBackend` fixture only after human review confirms
that it contains no credentials, private source, personal data, or proprietary prompt content.

## Reproducible runs

Each result records task ID and commit; client name and version; llama.cpp tag, commit, binary
SHA-256, and launch arguments; model file SHA-256 and quantization; hardware and OS; context,
GPU-layer, cache, seed, and sampling settings; timeout; repeat number; UTC start time; and whether
the run was isolated. Use a fixed seed when supported, run one warm-up excluded from results, and
randomize the order of task/configuration pairs within each repeat. Keep the task set frozen before
comparing configurations.

Report the number of observations with every interval. Treat a run as incomparable when its model,
server build, task commit, verification command, or sampling configuration differs from the
baseline. Publish aggregate metrics and scrubbed fixtures only; never publish raw local recordings
by default.
