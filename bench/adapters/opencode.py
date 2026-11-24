"""OpenCode subprocess adapter; commands checked with the installed CLI help.

Provider fields follow https://opencode.ai/docs/providers/ (2026-11-24).
The real-model compatibility check remains a human phase-exit requirement.
"""
from __future__ import annotations
import json
from pathlib import Path
from typing import Any

class OpenCodeClient:
    """Configure a single local OpenAI-compatible provider in a disposable HOME."""
    edit_tools = {"edit", "write", "apply_patch"}
    error_pattern = r"(?i)(error|failed|not found)"

    def __init__(self, proxy_url: str = "http://127.0.0.1:8000"):
        self.proxy_url = proxy_url

    def command(self, instruction: str, repo: Path, config: dict[str, Any],
                home: Path, env: dict[str, str]) -> list[str]:
        """Return argv and put provider configuration in the isolated user configuration."""
        model = config.get("model", "local")
        settings = {"model": "platypus/" + model, "providers": {"platypus": {
            "name": "Local benchmark", "package": "@opencode/ai/providers/openai-compatible",
            "settings": {"baseURL": self.proxy_url + "/v1"},
            "models": {model: {"name": model}},
        }}}
        location = home / ".config/opencode/opencode.json"
        location.parent.mkdir(parents=True, exist_ok=True)
        location.write_text(json.dumps(settings), encoding="utf-8")
        return [config.get("executable", "opencode"), "run", "--pure", "--auto", "--format", "json",
                "--model", "platypus/" + model, "--dir", str(repo), instruction]
