"""Derive client-independent metrics from proxy recordings, never client logs."""
from __future__ import annotations
from collections import Counter
import json
import re
import statistics
from typing import Any


def compute_metrics(records: list[dict[str, Any]], edit_tools: set[str],
                    error_pattern: str) -> dict[str, Any]:
    """Calculate token, latency and tool metrics using roadmap section 0.2 definitions."""
    completions = [record for record in records if record["endpoint"].split("?")[0] in {
        "/completion", "/v1/completions", "/v1/chat/completions"}]
    timings = [record.get("timings", {}) for record in completions]
    known = bool(timings) and all("prompt_n" in item and "cache_n" in item for item in timings)
    processed = sum(item.get("prompt_n", 0) for item in timings) if known else None
    cached = sum(item.get("cache_n", 0) for item in timings) if known else None
    total = processed + cached if known else None
    prefill = [item["prompt_ms"] for item in timings if "prompt_ms" in item]
    invalid = 0
    actions: Counter[str] = Counter()
    failed_edits = 0
    tool_requests = False
    seen_results: set[str] = set()
    for record in completions:
        invalid_before = invalid
        request = record.get("request") or {}
        tools = {tool.get("function", tool).get("name"): tool.get("function", tool)
                 for tool in request.get("tools", [])}
        tool_requests = tool_requests or bool(tools)
        response = record.get("response") or {}
        messages = [choice.get("message", {}) for choice in response.get("choices", [])]
        for message in messages:
            calls = message.get("tool_calls", [])
            text = message.get("content") or ""
            if not calls and any(marker in text for marker in ("<tool_call", '"tool_call"', "<function=", "<|tool_call|>")):
                invalid += 1
            for raw in calls:
                call = raw.get("function", raw)
                args = call.get("arguments", {})
                try:
                    args = json.loads(args) if isinstance(args, str) else args
                except ValueError:
                    invalid += 1
                    continue
                name = call.get("name")
                schema = tools.get(name, {}).get("parameters", {})
                wrong = not isinstance(args, dict) or name not in tools
                if isinstance(args, dict):
                    wrong |= any(key not in args for key in schema.get("required", []))
                    checks = {"string": lambda item: isinstance(item, str),
                              "integer": lambda item: type(item) is int,
                              "number": lambda item: type(item) in (int, float),
                              "boolean": lambda item: isinstance(item, bool),
                              "array": lambda item: isinstance(item, list),
                              "object": lambda item: isinstance(item, dict)}
                    for key, value in args.items():
                        kind = schema.get("properties", {}).get(key, {}).get("type")
                        if kind in checks and not checks[kind](value):
                            wrong = True
                invalid += bool(wrong)
                actions[str(name) + json.dumps(args, sort_keys=True, separators=(",", ":"))] += 1
        if invalid > invalid_before:
            invalid = invalid_before + 1
        if edit_tools:
            previous_calls = {}
            for message in request.get("messages", []):
                for raw in message.get("tool_calls", []):
                    previous_calls[raw.get("id")] = raw.get("function", raw).get("name")
                identifier = message.get("tool_call_id")
                if (message.get("role") == "tool" and identifier not in seen_results
                        and previous_calls.get(identifier) in edit_tools
                        and re.search(error_pattern, str(message.get("content", "")))):
                    seen_results.add(identifier)
                    failed_edits += 1
    return {
        "turns": len(completions), "prompt_tokens_total": total,
        "prompt_tokens_processed": processed,
        "cache_hit_ratio": cached / total if total else None,
        "prefill_ms": sum(prefill) if len(prefill) == len(timings) and timings else None,
        "prefill_ms_median": statistics.median(prefill) if prefill else None,
        "invalid_tool_calls": invalid if tool_requests else None,
        "failed_edits": failed_edits if edit_tools and tool_requests else None,
        "loops": sum(max(0, count - 3) for count in actions.values()) if tool_requests else None,
    }
