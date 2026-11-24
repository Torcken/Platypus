"""Behavioral regressions for measurement, editing, redaction, and paired statistics."""
import json
from pathlib import Path
import tempfile
import unittest
from bench.edit_formats import apply_unified_diff, patch_text, replace_text
from bench.metrics import compute_metrics
from bench.proxy import ProxyServer
from bench.report import paired_bootstrap, ReportGenerator

class RegressionTests(unittest.TestCase):
    def test_diff_changes_file_and_bad_diff_is_atomic(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "file"
            path.write_text("before\n")
            diff = "--- a/file\n+++ b/file\n@@ -1 +1 @@\n-before\n+after\n"
            self.assertTrue(apply_unified_diff(str(path), diff))
            self.assertEqual(path.read_text(), "after\n")
            self.assertFalse(apply_unified_diff(str(path), diff))
            self.assertEqual(path.read_text(), "after\n")

    def test_diff_insertion_deletion_and_missing_newline(self):
        self.assertEqual(patch_text("", "@@ -0,0 +1 @@\n+x\n"), "x\n")
        self.assertEqual(patch_text("x\n", "@@ -1 +0,0 @@\n-x\n"), "")
        self.assertEqual(patch_text("x", "@@ -1 +1 @@\n-x\n\\ No newline at end of file\n+y\n"), "y\n")

    def test_fuzzy_edit_requires_unique_candidate(self):
        result, level = replace_text("    answer = 1\n", "answer = 1", "answer = 2", True)
        self.assertEqual(level, "exact")
        self.assertIn("answer = 2", result)
        result, level = replace_text("answer   =   1\n", "answer = 1", "answer = 2", True)
        self.assertEqual(level, "whitespace")
        with self.assertRaises(ValueError):
            replace_text("same\nsame\n", "same", "other", True)
        with self.assertRaises(ValueError):
            replace_text("aaa", "aa", "other", True)

    def test_sse_reassembly_keeps_timings_and_function_arguments(self):
        chunks = [
            {"choices": [{"delta": {"content": "hi", "tool_calls": [{"index": 0, "id": "call-1", "function": {"name": "read", "arguments": '{"pa'}}]}}]},
            {"choices": [{"delta": {"content": " there", "tool_calls": [{"index": 0, "function": {"arguments": 'th":"x"}'}}]}}], "timings": {"prompt_n": 4, "cache_n": 2}},
        ]
        raw = "".join("data: " + json.dumps(chunk) + "\r\n\r\n" for chunk in chunks).encode()
        response = ProxyServer._assemble(raw, True)
        self.assertEqual(response["choices"][0]["message"]["content"], "hi there")
        self.assertEqual(json.loads(response["choices"][0]["message"]["tool_calls"][0]["function"]["arguments"]), {"path": "x"})
        self.assertEqual(response["timings"]["cache_n"], 2)

    def test_recordings_redact_nested_secrets(self):
        with tempfile.TemporaryDirectory() as temporary:
            proxy = ProxyServer("http://127.0.0.1:8080", temporary, [r"secret-[a-z]+"])
            proxy._save_record("/completion", {"Authorization": "credential", "text": "secret-value"}, {"cookie": "private"})
            text = proxy.recording.read_text()
            self.assertNotIn("credential", text)
            self.assertNotIn("secret-value", text)
            self.assertNotIn("private", text)

    def test_metrics_validate_types_and_count_fourth_repetition(self):
        tool = {"type": "function", "function": {"name": "read", "parameters": {
            "required": ["path"], "properties": {"path": {"type": "string"}}}}}
        record = {"endpoint": "/v1/chat/completions", "request": {"tools": [tool]},
                  "response": {"choices": [{"message": {"tool_calls": [{"function": {"name": "read", "arguments": '{"path":"x"}'}}]}}]},
                  "timings": {"prompt_n": 10, "cache_n": 5, "prompt_ms": 4}}
        metrics = compute_metrics([record] * 5, {"edit"}, "error")
        self.assertEqual(metrics["loops"], 2)
        self.assertEqual(metrics["invalid_tool_calls"], 0)
        self.assertEqual(metrics["prompt_tokens_total"], 75)
        record["response"]["choices"][0]["message"]["tool_calls"][0]["function"]["arguments"] = '{"path":3}'
        self.assertEqual(compute_metrics([record], set(), "error")["invalid_tool_calls"], 1)
        self.assertIsNone(compute_metrics([], set(), "error")["prompt_tokens_total"])

    def test_bootstrap_pairs_tasks_and_excludes_changed_fixtures(self):
        left = [{"task_id": "a", "success": False}, {"task_id": "b", "success": False}]
        right = [{"task_id": "a", "success": True}, {"task_id": "b", "success": True}]
        self.assertEqual(paired_bootstrap(left, right, samples=100), (1., 1., 1., 2))
        self.assertEqual(paired_bootstrap(left, right, samples=100), paired_bootstrap(left, right, samples=100))
        with self.assertRaises(ValueError):
            paired_bootstrap([{"task_id": "a", "task_commit": "old"}], [{"task_id": "a", "task_commit": "new"}])

    def test_unknown_metrics_render_na(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = ReportGenerator().generate_markdown_report([{"task_id": "x", "status": "failure", "wall_time_s": 1}], str(Path(temporary) / "report.md"))
            self.assertIn("n/a", report)
