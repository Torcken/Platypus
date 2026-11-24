"""Exercise the built CLI against a real loopback HTTP fixture and disposable repository."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from bench.tests.fake_server import fake_server

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="platypus-cli-") as temporary:
    root = Path(temporary)
    calls = ['{"tool_call":{"name":"write","arguments":{"path":"result.txt","content":"verified"}}}', "Done"]
    with fake_server(responses=calls) as endpoint:
        result = subprocess.run([binary, "run", "--headless", "--yes", "--project", temporary,
                                 "--endpoint", endpoint, "Write a file"], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert (root / "result.txt").read_text() == "verified"
    assert "Approved (--yes)" in result.stderr
    restored = subprocess.run([binary, "undo", "--yes", "--project", temporary], capture_output=True, text=True, timeout=10)
    assert restored.returncode == 0, restored.stderr
    assert not (root / "result.txt").exists()
    with fake_server(responses=calls) as endpoint:
        denied = subprocess.run([binary, "run", "--headless", "--project", temporary,
                                 "--endpoint", endpoint, "Write a file"], capture_output=True, text=True, timeout=10)
    assert denied.returncode != 0
    assert not (root / "result.txt").exists()
    with fake_server(responses=["Resumed"]) as endpoint:
        resumed = subprocess.run([binary, "--resume", "1", "--headless", "--project", temporary,
                                  "--endpoint", endpoint, "Continue"], capture_output=True, text=True, timeout=10)
    assert resumed.returncode == 0, resumed.stderr
    assert "Resumed" in resumed.stdout
    assert (root / ".platypus/sessions.db").is_file()
print("CLI write/approval/undo/resume integration passed")
