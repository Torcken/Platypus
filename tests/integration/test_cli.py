"""Exercise the built CLI against a real loopback HTTP fixture and disposable repository."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from bench.tests.fake_server import fake_server
from bench.metrics import compute_metrics
from bench.proxy import ProxyServer

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
        tokenizations = [item["body"] for item in captured if item["endpoint"] == "/tokenize"]
        assert len(tokenizations) == len(completions)
        for tokenization, completion in zip(tokenizations, completions):
            assert tokenization["content"] == completion["prompt"]
            assert tokenization["add_special"] and tokenization["parse_special"]
        captured = []
        with fake_server(responses=[call], captured=captured,
                         tokenization={"error": "Tokenizer unavailable"}) as endpoint:
            rejected = subprocess.run(
                [binary, "run", "--headless", "--yes", "--project", temporary,
                 "--endpoint", endpoint, "--profile", str(profile), "Must not generate"],
                capture_output=True, text=True, timeout=10)
        assert rejected.returncode != 0
        assert "Prompt tokenization failed" in rejected.stderr
        assert all(item["endpoint"] == "/tokenize" for item in captured)
print("Native Qwen/Gemma CLI history integration passed")

# Score a failed edit and recovery through the same proxy used for real benchmark runs.
def encode_call(syntax, name, args):
    if syntax == "qwen_xml":
        return "<tool_call><function=" + name + ">" + "".join(
            "<parameter=" + key + ">" + value + "</parameter>" for key, value in args.items()
        ) + "</function></tool_call>"
    if syntax == "gemma":
        return '<|tool_call>call:' + name + '{' + ','.join(
            key + ':<|"|>' + value + '<|"|>' for key, value in args.items()
        ) + '}<tool_call|>'
    return json.dumps({"tool_call": {"name": name, "arguments": args}})


for syntax in ("json_tagged", "qwen_xml", "gemma"):
    with tempfile.TemporaryDirectory(prefix="platypus-metrics-") as temporary:
        root = Path(temporary)
        workspace = root / "repo"
        workspace.mkdir()
        command = [binary, "run", "--headless", "--yes", "--project", str(workspace)]
        if syntax != "json_tagged":
            prefix = "<|im_start|>{role}\n" if syntax == "qwen_xml" else "<|turn>{role}\n"
            suffix = "<|im_end|>\n" if syntax == "qwen_xml" else "<turn|>\n"
            profile = root / "profile.toml"
            profile.write_text('name="fixture"\ntool_syntax=' + json.dumps(syntax) +
                               '\n[template]\nturn_prefix=' + json.dumps(prefix) +
                               '\nturn_suffix=' + json.dumps(suffix) + '\n')
            command += ["--profile", str(profile)]
        responses = [encode_call(syntax, "write", {"path": "result.txt", "content": "before"}),
                     encode_call(syntax, "edit", {"path": "result.txt", "old_string": "missing", "new_string": "after"}),
                     encode_call(syntax, "edit", {"path": "result.txt", "old_string": "before", "new_string": "after"}), "Done"]
        captured = []
        with fake_server(responses=responses, captured=captured) as upstream:
            proxy = ProxyServer(upstream, str(root / "recordings"))
            endpoint = proxy.start()
            try:
                result = subprocess.run(command + ["--endpoint", endpoint, "Edit a file"],
                                        env={**os.environ, "PLATYPUS_BENCHMARK_TRACE": "1"},
                                        capture_output=True, text=True, timeout=15)
            finally:
                proxy.stop()
        assert result.returncode == 0, result.stderr
        assert (workspace / "result.txt").read_text() == "after"
        records = [json.loads(line) for line in proxy.recording.read_text().splitlines()]
        metrics = compute_metrics(records, {"write", "edit"}, "error")
        assert metrics["turns"] == 4, metrics
        assert metrics["invalid_tool_calls"] == 0, metrics
        assert metrics["failed_edits"] == 1, metrics
        assert metrics["loops"] == 0, metrics
        assert metrics["prompt_tokens_total"] == 60, metrics
        assert not metrics["tool_metrics_errors"], metrics
        assert all("X-Platypus-Benchmark" not in item["headers"] for item in captured)
        assert all("client_metadata" in record for record in records
                   if record["endpoint"] != "/tokenize")
        assert all("client_metadata" not in record for record in records
                   if record["endpoint"] == "/tokenize")
print("JSON/Qwen/Gemma proxy tool metrics integration passed")
