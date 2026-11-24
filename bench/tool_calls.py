"""Strict, schema-aware tool extraction from recorded model responses.

Unlike the agent's tolerant parser, scoring never repairs malformed model output. Native syntax
families share the same argument validator and action canonicalization as JSON function calls.
"""
from __future__ import annotations

import json
import math
import re
from typing import Any


def _object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate argument or object key")
        result[key] = value
    return result


def _constant(value: str) -> None:
    raise ValueError("Non-finite JSON constant: " + value)


_DECODER = json.JSONDecoder(object_pairs_hook=_object, parse_constant=_constant)
_MARKERS = ('"tool_call"', '"tool_calls"', '"plan"', '<tool_call', '<function=',
            '<|tool_call>')


def metadata_error(metadata: Any) -> str | None:
    """Validate the optional versioned trace before metrics consume it."""
    if not isinstance(metadata, dict):
        return "Benchmark metadata must be an object"
    if metadata.get("error"):
        return str(metadata["error"])
    if type(metadata.get("version")) is not int or metadata["version"] != 1:
        return "Unsupported benchmark metadata version"
    if metadata.get("syntax") not in ("json_tagged", "qwen_xml", "gemma"):
        return "Invalid benchmark syntax family"
    tools = metadata.get("tools")
    if not isinstance(tools, list):
        return "Benchmark tools must be a list"
    for tool in tools:
        if not isinstance(tool, dict):
            return "Invalid benchmark tool declaration"
        function = tool.get("function", tool)
        if (not isinstance(function, dict) or not isinstance(function.get("name"), str)
                or not isinstance(function.get("parameters"), dict)):
            return "Invalid benchmark tool schema"
    observations = metadata.get("observations", [])
    if not isinstance(observations, list):
        return "Invalid benchmark observations"
    for result in observations:
        if (not isinstance(result, dict) or type(result.get("id")) is not int
                or not isinstance(result.get("name"), str)
                or type(result.get("is_error")) is not bool):
            return "Invalid benchmark observation"
    reasoning = metadata.get("reasoning", {})
    if (not isinstance(reasoning, dict)
            or any(not isinstance(reasoning.get(key, ""), str) for key in ("open", "close"))
            or type(reasoning.get("implicit", False)) is not bool):
        return "Invalid benchmark reasoning settings"
    return None


def visible_text(text: str, reasoning: dict[str, Any]) -> str:
    """Remove only a leading explicit or profile-opened reasoning channel."""
    opening, closing = reasoning.get("open"), reasoning.get("close")
    if not opening or not closing:
        return text
    stripped = text.lstrip()
    explicit = stripped.startswith(opening)
    if not explicit and not reasoning.get("implicit"):
        return text
    start = len(text) - len(stripped) + len(opening) if explicit else 0
    end = text.find(closing, start)
    if end < 0:
        raise ValueError("Incomplete reasoning channel")
    return text[end + len(closing):]


def _gemma_arguments(text: str) -> dict[str, Any]:
    converted = ""
    index = 0
    while index < len(text):
        if text.startswith('<|"|>', index):
            end = text.find('<|"|>', index + 5)
            if end < 0:
                raise ValueError("Unclosed native string")
            converted += json.dumps(text[index + 5:end])
            index = end + 5
        elif text[index].isalpha() or text[index] == "_":
            word = re.match(r"[A-Za-z_][A-Za-z_0-9]*", text[index:])
            if word is None:
                raise ValueError("Invalid native identifier")
            index += len(word[0])
            converted += json.dumps(word[0]) if text[index:].lstrip().startswith(":") else word[0]
        else:
            converted += text[index]
            index += 1
    return _DECODER.decode(converted)


def extract_calls(text: str, syntax: str, tools: dict[str, Any]) -> list[dict[str, Any]]:
    """Extract complete calls, raising ValueError for malformed attempted calls."""
    calls = []
    if syntax == "qwen_xml" and "<function=" in text:
        for match in re.finditer(r"<function=([A-Za-z_][A-Za-z_0-9]*)>(.*?)</function>",
                                 text, re.DOTALL):
            args = {}
            properties = tools.get(match[1], {}).get("parameters", {}).get("properties", {})
            for parameter in re.finditer(r"<parameter=([A-Za-z_][A-Za-z_0-9]*)>(.*?)</parameter>",
                                         match[2], re.DOTALL):
                value = parameter[2].removeprefix("\n").removesuffix("\n")
                if parameter[1] in args:
                    raise ValueError("Duplicate XML argument")
                if properties.get(parameter[1], {}).get("type") != "string":
                    try:
                        value = _DECODER.decode(value)
                    except ValueError:
                        pass  # The common validator rejects a string for a declared number.
                args[parameter[1]] = value
            residue = re.sub(r"<parameter=[A-Za-z_][A-Za-z_0-9]*>.*?</parameter>", "",
                             match[2], flags=re.DOTALL)
            if residue.strip():
                raise ValueError("Malformed XML parameters")
            calls.append({"name": match[1], "arguments": args})
        if len(calls) != text.count("<function="):
            raise ValueError("Unclosed XML function")
    elif syntax == "gemma" and "<|tool_call>" in text:
        for match in re.finditer(r"<\|tool_call>call:([A-Za-z_][A-Za-z_0-9]*)\s*(\{.*?)<tool_call\|>",
                                 text, re.DOTALL):
            calls.append({"name": match[1], "arguments": _gemma_arguments(match[2])})
        if len(calls) != text.count("<|tool_call>"):
            raise ValueError("Malformed native call")
    else:
        start = text.find("{")
        if start >= 0:
            try:
                value, _ = _DECODER.raw_decode(text[start:])
            except ValueError:
                if any(marker in text for marker in _MARKERS):
                    raise
                return []
            if isinstance(value, dict):
                if "tool_call" in value:
                    calls = [value["tool_call"]]
                elif "tool_calls" in value or "plan" in value:
                    calls = value.get("tool_calls", value.get("plan"))
                elif "name" in value:
                    calls = [value]
    if not isinstance(calls, list) or (not calls and any(marker in text for marker in _MARKERS)):
        raise ValueError("Tool markers without a parsed call")
    return calls


def valid_arguments(name: Any, args: Any, tools: dict[str, Any]) -> bool:
    """Check declared names, required keys and top-level JSON types without external packages."""
    if not isinstance(name, str) or name not in tools or not isinstance(args, dict):
        return False
    schema = tools[name].get("parameters", {})
    if any(key not in args for key in schema.get("required", [])):
        return False
    properties = schema.get("properties", {})
    if schema.get("additionalProperties") is False and any(key not in properties for key in args):
        return False
    checks = {"string": lambda item: isinstance(item, str),
              "integer": lambda item: type(item) is int,
              "number": lambda item: type(item) in (int, float) and math.isfinite(item),
              "boolean": lambda item: isinstance(item, bool),
              "array": lambda item: isinstance(item, list),
              "object": lambda item: isinstance(item, dict)}
    return all(kind not in checks or checks[kind](value)
               for key, value in args.items()
               for kind in [properties.get(key, {}).get("type")])
