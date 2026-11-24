# Benchmark harness

Python 3.11+ and the standard library are sufficient. A C++ compiler is needed by one example
task. Network access is only needed for explicit repository fetching and real-model serving.
Read `docs/benchmark-protocol.md` before running third-party clients. Disposable HOME/workspaces
remove inherited credentials; this is not a sandbox for untrusted repositories.

```sh
python3 -m unittest discover -s bench/tests -v
python3 bench/proxy.py --target http://127.0.0.1:8080 --port 8000
python3 bench/run.py --task bug-python --client opencode --model local --endpoint http://127.0.0.1:8080 --repeats 3
python3 bench/run.py --task bug-python --client platypus --model local --repeats 3
python3 bench/report.py bench/results/runs.jsonl --output bench/results/report.md
python3 bench/edit_formats.py --model local --repeats 3
```

Start the server pinned in ADR-002 before real runs. Select its model/context/GPU layers explicitly;
record the exact launch arguments and `llama-server --version`. To emulate a smaller tier, reduce
GPU layers (`-ngl`) and context (`-c`); compare cache quantization as a separately recorded setting.
Do not infer hardware equivalence from these settings alone.

Five maintained examples are directories under `tasks/`, each containing `task.toml` and a trusted
`verify.sh`. Sources live in `repos/`. Tests prove each original fixture fails verification and its
reference solution passes. Ten small edit-format fixtures live in `edit_tasks/`. Legacy JSON task
stubs are retained as historical inputs and are not executed by the new runner.

Adapters exist for OpenCode, aider, Platypus and the deterministic fake client. OpenCode flags were
checked with installed `--help`; provider configuration follows its current official docs. Aider
is not installed: its flags are explicitly UNVERIFIED. Verify both clients against the pinned
server before producing a baseline. The fake client is only for tests, never a model baseline.

`--config metadata.json` records caller-supplied model SHA-256, quantization, tier, context size,
GPU layers, seed/sampling, server commit/binary SHA-256/launch arguments and client version.
Missing metadata remains UNVERIFIED/unspecified: such runs do not establish roadmap exit criteria.
Recordings are retained under the result directory's `recordings/` child, redacted and ignored by
Git. No client logs are interpreted as metrics. Unknown/inapplicable metrics are null (`n/a`).

The report emits Wilson intervals and `--compare LABEL_A LABEL_B` paired task bootstrap intervals
(seed 42). Freeze task inputs and review metadata before interpreting an interval. The runner CLI excludes warm-ups (`--warmups`, default 1) and randomizes task/client pair order
within each repeat (`--seed`, default 42). Tier calibration remains a manual protocol step.

Use `python3 bench/fetch.py TASK_ID` explicitly for trusted HTTPS task repositories with a pinned
commit. The runner itself never fetches. No raw recordings or private project content should be
committed without human review.

## Baseline provenance gate

Add `--baseline` to both the runner and report command to require complete protocol metadata.
Provide `model_file` and `server_binary` paths: the runner hashes these files incrementally and
checks any supplied `model_file_sha256` / `server_binary_sha256`. It resolves and hashes the client
executable and checks `--version`. Missing metadata is allowed in exploratory mode; hash or version
mismatches always fail. Fixture runs cannot be published as a baseline.

Required metadata includes `model`, `quantization`, `hardware_tier`, `hardware_description`,
`hardware_os`, `context_size`, `gpu_layers`, `cache_type_k`, `cache_type_v`, nonnegative fixed `seed`,
nonempty `sampling` with a finite `temperature`, `llama_cpp_tag`, `llama_cpp_commit`,
`server_build_backend`, `server_version`, and `server_launch_args` as a JSON argv array.
Use `clients` overrides for client-specific `executable`, `client_version`, and `label` values.
Server/model/client SHA-256 fields are filled by artifact checks. Use distinct labels for distinct
configurations; paired comparisons reject changes to task sets, model, hardware or serving settings.
Local artifact checks cannot verify the binary actually running at a remote endpoint. The operator
must still check serving settings and hardware, then freeze the tasks before baseline measurement.
