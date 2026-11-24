"""Run the built Platypus executable headlessly through the same recording proxy."""
from pathlib import Path
from typing import Any

class PlatypusClient:
    """Use one explicit --yes invocation only inside the disposable benchmark workspace."""
    edit_tools = {"write", "edit"}
    error_pattern = r"(?i)(error|failed|not found)"
    def __init__(self, proxy_url: str):
        self.proxy_url = proxy_url
    def command(self, instruction: str, repo: Path, config: dict[str, Any],
                home: Path, env: dict[str, str]) -> list[str]:
        env["PLATYPUS_BENCHMARK_TRACE"] = "1"
        executable = Path(config.get("executable", Path(__file__).resolve().parents[2] / "build/dev/platypus")).resolve()
        command = [str(executable), "run", "--headless", "--yes", "--project", str(repo), "--endpoint", self.proxy_url]
        if config.get("profile"):
            command += ["--profile", str(Path(config["profile"]).resolve())]
        return command + [instruction]
