"""A deterministic subprocess client for full benchmark-pipeline tests."""
from pathlib import Path
import sys
from typing import Any

class FakeClient:
    """Ask the fixture server for an edit and apply its returned file content."""
    edit_tools = {"write"}
    error_pattern = r"(?i)(error|failed)"

    def __init__(self, proxy_url: str):
        self.proxy_url = proxy_url

    def command(self, instruction: str, repo: Path, config: dict[str, Any],
                home: Path, env: dict[str, str]) -> list[str]:
        """Run this module without relying on PYTHONPATH from the parent environment."""
        return [sys.executable, str(Path(__file__).resolve()), self.proxy_url, instruction]

if __name__ == "__main__":
    import json
    import urllib.request
    payload = json.dumps({"messages": [{"role": "user", "content": sys.argv[2]}],
                          "stream": True}).encode()
    request = urllib.request.Request(sys.argv[1] + "/v1/chat/completions", data=payload,
                                     headers={"Content-Type": "application/json"})
    content = ""
    with urllib.request.urlopen(request, timeout=10) as response:
        for raw in response:
            line = raw.decode().strip()
            if line.startswith("data: ") and line != "data: [DONE]":
                chunk = json.loads(line[6:])
                for choice in chunk.get("choices", []):
                    content += choice.get("delta", {}).get("content", "")
    edit = json.loads(content)
    path = Path(edit["path"])
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("Invalid fake edit path")
    path.write_text(edit["content"], encoding="utf-8")
