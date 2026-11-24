"""Aider adapter for an explicitly selected local endpoint.

UNVERIFIED: aider is not installed here. Check flags against aider --help before baseline runs.
"""
from pathlib import Path
from typing import Any

class AiderClient:
    """Launch aider without loading user credentials or committing edits."""
    edit_tools: set[str] = set()
    error_pattern = r"(?i)(error|failed)"

    def __init__(self, proxy_url: str):
        self.proxy_url = proxy_url

    def command(self, instruction: str, repo: Path, config: dict[str, Any],
                home: Path, env: dict[str, str]) -> list[str]:
        """Return a noninteractive command targeting the recording proxy."""
        env["OPENAI_API_BASE"] = self.proxy_url + "/v1"
        env["OPENAI_API_KEY"] = "local-no-secret"
        return ["aider", "--model", "openai/" + config.get("model", "local"),
                "--yes-always", "--no-auto-commits", "--no-check-update", "--message", instruction]
