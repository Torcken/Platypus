"""Verify deterministic automatic llama-server backend selection."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SELECTOR = ROOT / "scripts" / "server-backend-selection.sh"


def select(os_name, cuda_toolkit, nvidia_device, vulkan_tools, render_device):
    script = 'source "$1"; shift; platypus_select_server_backend "$@"'
    result = subprocess.run(
        ["bash", "-c", script, "backend-selection", str(SELECTOR), os_name,
         cuda_toolkit, nvidia_device, vulkan_tools, render_device],
        check=True, capture_output=True, text=True, timeout=5)
    return result.stdout.strip()


assert select("Linux", "no", "no", "no", "no") == "cpu"
assert select("Linux", "yes", "no", "no", "no") == "cpu"
assert select("Linux", "yes", "yes", "yes", "yes") == "cuda"
assert select("Linux", "no", "no", "yes", "yes") == "vulkan"
assert select("Linux", "no", "no", "yes", "no") == "cpu"
assert select("Darwin", "no", "no", "no", "no") == "metal"
assert select("FreeBSD", "yes", "yes", "yes", "yes") == "cpu"
