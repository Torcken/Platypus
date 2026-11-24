"""Run isolated, verified benchmark tasks through a streaming recording proxy."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import shutil
import signal
import subprocess
import tempfile
import time
import tomllib
from typing import Any

# Support both documented direct invocation and python -m bench.run.
if __package__ in (None, ""):
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bench.adapters.opencode import OpenCodeClient
from bench.adapters.aider import AiderClient
from bench.adapters.fake import FakeClient
from bench.adapters.platypus import PlatypusClient
from bench.metadata import collect_artifacts, configuration_for_client, metadata_errors
from bench.metrics import compute_metrics
from bench.proxy import ProxyServer


def run_process(argv: list[str], cwd: Path, env: dict[str, str], timeout: float) -> dict[str, Any]:
    """Run a subprocess group with bounded retained logs and kill the group at timeout."""
    with tempfile.TemporaryFile() as log:
        try:
            process = subprocess.Popen(argv, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                       stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        except OSError as error:
            return {"exit_code": None, "error": str(error), "timed_out": False}
        timed_out = False
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
        finally:
            # Kill descendants even when the direct child exited first.
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
        log.seek(0)
        output = log.read(65536).decode(errors="replace")
        return {"exit_code": process.returncode, "timed_out": timed_out,
                "error": "Timeout" if timed_out else None, "output": output}


class Runner:
    """Restore a disposable repository for each run; score only the maintained verifier."""

    def __init__(self, proxy_url: str = "http://127.0.0.1:8080", root: str | Path = "bench",
                 output: str | Path | None = None):
        self.proxy_url = proxy_url
        self.root = Path(root).resolve()
        self.output = Path(output).resolve() if output else self.root / "results/runs.jsonl"
        self.clients = {"opencode": OpenCodeClient, "aider": AiderClient, "fake": FakeClient, "platypus": PlatypusClient}

    def _task(self, task_id: str) -> tuple[dict[str, Any], Path]:
        if not task_id or Path(task_id).name != task_id:
            raise ValueError("Task ID must be a single directory name")
        directory = self.root / "tasks" / task_id
        definition = directory / "task.toml"
        if not definition.is_file():
            raise FileNotFoundError(f"Task file not found: {definition}")
        with definition.open("rb") as source:
            task = tomllib.load(source)
        verifier = directory / "verify.sh"
        if not verifier.is_file():
            raise FileNotFoundError(f"Missing verifier: {verifier}")
        return task, directory

    def run_task(self, task_id: str, client_name: str, model_config: dict[str, Any],
                 repeats: int = 3, repeat_start: int = 1, warmup: bool = False,
                 baseline: bool = False) -> list[dict[str, Any]]:
        """Run repeated fresh copies, record measured metrics, and continue after client failure."""
        if client_name not in self.clients:
            raise ValueError(f"Unknown client: {client_name}")
        if repeats < 1:
            raise ValueError("repeats must be positive")
        model_config = configuration_for_client(model_config, client_name)
        task, directory = self._task(task_id)
        source = task["repository"]
        repository = (self.root / source).resolve()
        if source.startswith(("https://", "http://", "git@")):
            repository = self.root / ".cache/repos" / hashlib.sha256(source.encode()).hexdigest()[:16]
        elif not repository.is_relative_to(self.root / "repos"):
            raise ValueError("Local repositories must be inside bench/repos")
        if not repository.is_dir():
            raise FileNotFoundError(f"Fetch or create task repository first: {repository}")
        results = []
        for repeat in range(repeat_start, repeat_start + repeats):
            with tempfile.TemporaryDirectory(prefix="platypus-bench-") as temporary:
                temporary = Path(temporary)
                workspace, home = temporary / "repo", temporary / "home"
                home.mkdir()
                env = {"HOME": str(home), "XDG_CONFIG_HOME": str(home / ".config"),
                       "XDG_DATA_HOME": str(home / ".local/share"), "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
                       "LANG": "C.UTF-8", "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": "/dev/null",
                       "GIT_TERMINAL_PROMPT": "0", "GIT_AUTHOR_NAME": "Benchmark",
                       "GIT_AUTHOR_EMAIL": "bench@localhost", "GIT_COMMITTER_NAME": "Benchmark",
                       "GIT_COMMITTER_EMAIL": "bench@localhost", "GIT_AUTHOR_DATE": "2026-01-01T00:00:00Z",
                       "GIT_COMMITTER_DATE": "2026-01-01T00:00:00Z"}
                shutil.copytree(repository, workspace, symlinks=True)
                for arguments in (["git", "init", "--quiet"], ["git", "add", "--all"],
                                  ["git", "-c", "commit.gpgSign=false", "commit", "--quiet", "--allow-empty", "-m", "Benchmark fixture"]):
                    setup = run_process(arguments, workspace, env, 30)
                    if setup["exit_code"] != 0:
                        raise RuntimeError("Cannot initialize benchmark repository: " + str(setup))
                if task.get("commit"):
                    checkout = run_process(["git", "checkout", "--detach", task["commit"]], workspace, env, 30)
                    if checkout["exit_code"] != 0:
                        raise RuntimeError("Pinned task commit unavailable")
                commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=workspace, env=env, text=True).strip()
                proxy = ProxyServer(model_config.get("endpoint", self.proxy_url), str(temporary / "recordings"), model_config.get("secret_patterns"))
                url = proxy.start()
                try:
                    adapter = self.clients[client_name](url)
                    config = dict(model_config)
                    argv = adapter.command(task["instruction"], workspace, config, home, env)
                    executable = shutil.which(argv[0], path=env["PATH"])
                    version = None
                    if client_name != "fake" and executable:
                        version_check = run_process([executable, "--version"], home, env, 10)
                        if version_check["exit_code"] == 0:
                            version = version_check.get("output", "").strip()
                    config = collect_artifacts(config, executable if client_name != "fake" else None, version)
                    errors = metadata_errors(config) if client_name != "fake" else ["fixture client"]
                    if baseline and errors:
                        raise ValueError("Baseline metadata incomplete: " + "; ".join(errors))
                except Exception:
                    proxy.stop()
                    raise
                timeout = float(task.get("timeout", 120))
                started = datetime.now(timezone.utc).isoformat()
                begin = time.monotonic()
                try:
                    client = run_process(argv, workspace, env, timeout)
                    wall_time = time.monotonic() - begin
                    verification = run_process(["/bin/sh", str(directory / "verify.sh")], workspace, env, timeout)
                finally:
                    proxy.stop()
                records = [json.loads(line) for line in proxy.recording.read_text().splitlines()] if proxy.recording.exists() else []
                recording = self.output.parent / "recordings" / f"{task_id}-{time.time_ns()}.jsonl"
                recording.parent.mkdir(parents=True, exist_ok=True)
                if proxy.recording.exists():
                    shutil.copy2(proxy.recording, recording)
                success = verification["exit_code"] == 0 and not client["timed_out"]
                result = {
                    "metadata_status": "fixture" if client_name == "fake" else ("complete" if not errors else "incomplete"),
                    "metadata_errors": errors, "recording": str(recording), "task_id": task_id, "task_commit": task.get("commit") or commit,
                    "task_fixture_sha256": hashlib.sha256((directory / "task.toml").read_bytes() + (directory / "verify.sh").read_bytes()).hexdigest(),
                    "client": client_name, "client_version": config.get("client_version", "UNVERIFIED"),
                    "config": config, "model": config.get("model", "unspecified"),
                    "hardware_tier": config.get("hardware_tier", "unspecified"),
                    "hardware_os": config.get("hardware_os", platform.platform()),
                    "runner_os": platform.platform(), "timeout": timeout, "repeat": repeat,
                    "started_at": started, "warmup": warmup, "isolated": True, "isolation": "disposable workspace and HOME; not a security sandbox",
                    "status": "success" if success else "failure", "success": success,
                    "wall_time_s": wall_time, "client_exit_code": client["exit_code"],
                    "verify_exit_code": verification["exit_code"], "timed_out": client["timed_out"],
                    "error": client["error"],
                    **compute_metrics(records, adapter.edit_tools, adapter.error_pattern),
                }
                self.output.parent.mkdir(parents=True, exist_ok=True)
                if not warmup:
                    with self.output.open("a", encoding="utf-8") as output:
                        output.write(json.dumps(result) + "\n")
                results.append(result)
        return results


    def run_matrix(self, tasks: list[str], clients: list[str], config: dict[str, Any],
                   repeats: int = 3, warmups: int = 1, seed: int = 42,
                   baseline: bool = False) -> list[dict[str, Any]]:
        """Warm each task/client pair, then randomize pair order independently in every repeat."""
        if repeats < 1 or warmups < 0:
            raise ValueError("Expected positive repeats and nonnegative warmups")
        pairs = [(task, client) for task in tasks for client in clients]
        for task, client in pairs:
            if warmups:
                self.run_task(task, client, config, repeats=warmups, warmup=True, baseline=baseline)
        generator = random.Random(seed)
        results = []
        for repeat in range(1, repeats + 1):
            order = pairs.copy()
            generator.shuffle(order)
            for task, client in order:
                results.extend(self.run_task(task, client, dict(config, order_seed=seed),
                                             repeats=1, repeat_start=repeat, baseline=baseline))
        return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--task", nargs="+", required=True)
    parser.add_argument("--client", choices=["opencode", "aider", "fake", "platypus"], nargs="+", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--endpoint", default="http://127.0.0.1:8080")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--baseline", action="store_true", help="Require complete metadata and checked artifacts")
    parser.add_argument("--config", type=Path, help="JSON reproducibility metadata and model settings")
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    config = json.loads(arguments.config.read_text()) if arguments.config else {}
    config["model"] = arguments.model
    print(json.dumps(Runner(arguments.endpoint, output=arguments.output).run_matrix(
        arguments.task, arguments.client, config, arguments.repeats, arguments.warmups, arguments.seed, arguments.baseline), indent=2))
