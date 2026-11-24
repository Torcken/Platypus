"""Fetch a trusted task repository at an explicit commit; never called by the runner."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tomllib

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("task", help="Maintained task ID under bench/tasks")
    arguments = parser.parse_args()
    if Path(arguments.task).name != arguments.task:
        parser.error("Task must be a single directory name")
    root = Path(__file__).resolve().parent
    task = tomllib.loads((root / "tasks" / arguments.task / "task.toml").read_text())
    url, commit = task["repository"], task.get("commit")
    if not url.startswith("https://") or not commit:
        parser.error("Fetching requires an HTTPS repository and a pinned commit")
    destination = root / ".cache/repos" / hashlib.sha256(url.encode()).hexdigest()[:16]
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists():
        subprocess.run(["git", "clone", "--no-checkout", "--", url, str(destination)], check=True)
    subprocess.run(["git", "-C", str(destination), "fetch", "origin", commit], check=True)
    subprocess.run(["git", "-C", str(destination), "checkout", "--detach", commit], check=True)
    print(destination)
