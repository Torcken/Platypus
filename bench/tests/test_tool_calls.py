"""Check native/JSON scoring against malformed calls, types and repeated observations."""
import json
import unittest

from bench.metrics import compute_metrics
from bench.tool_calls import extract_calls, metadata_error, valid_arguments, visible_text


class ToolMetricTests(unittest.TestCase):
    """Apply one scoring definition across wire protocols and syntax families."""

    def setUp(self):
        self.tool = {"name": "edit", "parameters": {"type": "object",
                     "properties": {"path": {"type": "string"}, "line": {"type": "integer"}},
                     "required": ["path"], "additionalProperties": False}}
        self.tools = {"edit": self.tool}

    def record(self, text, syntax="json_tagged", observations=None):
        return {"endpoint": "/completion", "request": {"prompt": "native prompt"},
                "response": {"content": text},
                "client_metadata": {"version": 1, "tools": [self.tool], "syntax": syntax,
                                    "observations": observations or []}}

    def test_native_calls_keep_numeric_strings_and_types(self):
        for text, syntax in (
            ('<tool_call><function=edit><parameter=path>true</parameter>'
             '<parameter=line>2</parameter></function></tool_call>', "qwen_xml"),
            ('<|tool_call>call:edit{path:<|"|>true<|"|>,line:2}<tool_call|>', "gemma"),
            ('prefix {"tool_call":{"name":"edit","arguments":{"path":"true","line":2}}}',
             "json_tagged"),
        ):
            with self.subTest(syntax=syntax):
                calls = extract_calls(text, syntax, self.tools)
                self.assertEqual(calls, [{"name": "edit", "arguments": {"path": "true", "line": 2}}])
                metrics = compute_metrics([self.record(text, syntax)], {"edit"}, "error")
                self.assertEqual(metrics["invalid_tool_calls"], 0)

    def test_strict_metrics_do_not_repair_malformed_calls(self):
        for text, syntax in (
            ('{"tool_call":{"name":"edit","arguments":{"path":"x",}}}', "json_tagged"),
            ('{"tool_call":{"name":"edit","arguments":{"path":"x"}}', "json_tagged"),
            ('<function=edit><parameter=path>x</function>', "qwen_xml"),
            ('<function=edit><parameter=path>x</parameter>', "qwen_xml"),
            ('<|tool_call>call:edit{path:<|"|>x}<tool_call|>', "gemma"),
            ('<|tool_call>call:edit{path:<|"|>x<|"|>}', "gemma"),
            ('{"tool_calls":[null]}', "json_tagged"),
            ('{"tool_call":{"name":"edit","arguments":false}}', "json_tagged"),
        ):
            with self.subTest(text=text):
                metrics = compute_metrics([self.record(text, syntax)], {"edit"}, "error")
                self.assertEqual(metrics["invalid_tool_calls"], 1)

    def test_minimal_schema_check_rejects_unknown_names_required_keys_and_types(self):
        for name, args in (("missing", {}), ("edit", {}), ("edit", {"path": 3}),
                           ("edit", {"path": "x", "line": True}),
                           ("edit", {"path": "x", "extra": "ignored?"})):
            self.assertFalse(valid_arguments(name, args, self.tools))
        self.assertTrue(valid_arguments("edit", {"path": "x", "line": 2}, self.tools))

    def test_repeated_native_results_are_deduplicated_and_fourth_action_counts(self):
        calls = '<|tool_call>call:edit{path:<|"|>x<|"|>}<tool_call|>'
        observations = [{"id": 3, "name": "edit", "is_error": True}]
        records = [self.record(calls, "gemma", observations) for _ in range(5)]
        metrics = compute_metrics(records, {"edit"}, "error")
        self.assertEqual(metrics["failed_edits"], 1)
        self.assertEqual(metrics["loops"], 2)
        self.assertEqual(metrics["invalid_tool_calls"], 0)

    def test_reasoning_is_excluded_without_removing_literal_argument_data(self):
        call = '{"tool_call":{"name":"edit","arguments":{"path":"</think>"}}}'
        reasoning = {"open": "<think>", "close": "</think>", "implicit": False}
        text = '<think>{"tool_call":invalid}</think>' + call
        self.assertEqual(visible_text(text, reasoning), call)
        self.assertEqual(visible_text(call, reasoning), call)
        with self.assertRaises(ValueError):
            visible_text("<think>unfinished", reasoning)
        reasoning["implicit"] = True
        self.assertEqual(visible_text("reason</think>answer", reasoning), "answer")

    def test_corrupt_trace_makes_tool_metrics_unavailable(self):
        record = self.record("Done")
        record["client_metadata"] = {"error": "Invalid trace"}
        metrics = compute_metrics([record], {"edit"}, "error")
        self.assertIsNone(metrics["invalid_tool_calls"])
        self.assertEqual(metrics["tool_metrics_errors"], ["Invalid trace"])

    def test_mistyped_trace_is_explicitly_unavailable_instead_of_crashing(self):
        valid = self.record("Done")["client_metadata"]
        self.assertIsNone(metadata_error(valid))
        for broken in (None, [], {**valid, "version": True}, {**valid, "tools": [None]},
                       {**valid, "observations": [{"id": 1, "name": "edit", "is_error": "true"}]},
                       {**valid, "reasoning": {"implicit": "true"}}):
            record = self.record("Done")
            record["client_metadata"] = broken
            metrics = compute_metrics([record], {"edit"}, "error")
            self.assertIsNone(metrics["invalid_tool_calls"])
            self.assertTrue(metrics["tool_metrics_errors"])

    def test_duplicate_keys_are_invalid_in_all_encodings(self):
        for text, syntax in (
            ('{"tool_call":{"name":"edit","arguments":{"path":"x","path":"y"}}}', "json_tagged"),
            ('<function=edit><parameter=path>x</parameter><parameter=path>y</parameter></function>', "qwen_xml"),
            ('<|tool_call>call:edit{path:<|"|>x<|"|>,path:<|"|>y<|"|>}<tool_call|>', "gemma"),
        ):
            with self.assertRaises(ValueError):
                extract_calls(text, syntax, self.tools)

    def test_chat_wire_calls_and_plain_text_calls_score_equally(self):
        call = {"name": "edit", "arguments": {"path": "x"}}
        plain = self.record(json.dumps({"tool_call": call}))
        wire = self.record("unused")
        wire["response"] = {"choices": [{"message": {"tool_calls": [call]}}]}
        self.assertEqual(compute_metrics([plain], {"edit"}, "error"),
                         compute_metrics([wire], {"edit"}, "error"))
