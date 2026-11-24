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

# Native profile sessions exercise rendering through the actual HTTP transport and CLI.
for syntax, prefix, suffix, role, call, response_marker in (
    ("qwen_xml", "<|im_start|>{role}\n", "<|im_end|>\n", "assistant",
     "<tool_call><function=write><parameter=path>native.txt</parameter><parameter=content>verified</parameter></function></tool_call>", "<tool_response>"),
    ("gemma", "<|turn>{role}\n", "<turn|>\n", "model",
     '<|tool_call>call:write{path:<|"|>native.txt<|"|>,content:<|"|>verified<|"|>}<tool_call|>', "<|tool_response>response:write"),
):
    import json
    with tempfile.TemporaryDirectory(prefix="platypus-native-") as temporary:
        root = Path(temporary)
        profile = root / "profile.toml"
        profile.write_text('name="fixture"\ntool_syntax=' + json.dumps(syntax) +
                           '\n[template]\nturn_prefix=' + json.dumps(prefix) +
                           '\nturn_suffix=' + json.dumps(suffix) +
                           '\nassistant_role=' + json.dumps(role) + '\n')
        captured = []
        with fake_server(responses=[call, "Done"], captured=captured) as endpoint:
            result = subprocess.run([binary, "run", "--headless", "--yes", "--project", temporary,
                                     "--endpoint", endpoint, "--profile", str(profile), "Write a file"],
                                    capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        assert (root / "native.txt").read_text() == "verified"
        completions = [item["body"] for item in captured if item["endpoint"] == "/completion"]
        assert len(completions) == 2
        assert completions[1]["prompt"].startswith(completions[0]["prompt"] + call)
        assert response_marker in completions[1]["prompt"]
print("Native Qwen/Gemma CLI history integration passed")
