"""Apply and evaluate unified diff, exact replacement, and unambiguous fuzzy edits."""
from __future__ import annotations
import argparse
import difflib
import json
from pathlib import Path
import re
import tempfile
import urllib.request
from typing import Any


def patch_text(original: str, patch: str) -> str:
    """Apply validated unified hunks in memory; reject stale context and partial patches."""
    source = original.splitlines(keepends=True)
    raw = patch.splitlines(keepends=True)
    lines: list[str] = []
    for line in raw:
        if line.startswith("\\ No newline"):
            if not lines:
                raise ValueError("Unexpected newline marker")
            lines[-1] = lines[-1].removesuffix("\n")
        else:
            lines.append(line)
    result: list[str] = []
    cursor = 0
    index = 0
    hunks = 0
    while index < len(lines):
        line = lines[index]
        if not line.startswith("@@"):
            if line.startswith(("---", "+++", "diff ", "index ")) or not line.strip():
                index += 1
                continue
            raise ValueError("Unexpected text outside diff hunk")
        header = re.match(r"@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@", line)
        if not header:
            raise ValueError("Invalid hunk header")
        old_start = int(header[1]); old_count = int(header[2] or 1)
        new_count = int(header[4] or 1)
        offset = old_start - 1 if old_count else old_start
        if offset < cursor or offset > len(source):
            raise ValueError("Hunk outside file or overlaps previous hunk")
        result.extend(source[cursor:offset]); cursor = offset
        removed = added = 0
        index += 1
        while index < len(lines) and (removed < old_count or added < new_count):
            operation = lines[index][:1]; text = lines[index][1:]
            if operation not in (" ", "+", "-"):
                raise ValueError("Invalid hunk line")
            if operation in (" ", "-"):
                if cursor >= len(source) or source[cursor] != text:
                    raise ValueError("Diff context does not match")
                cursor += 1; removed += 1
            if operation in (" ", "+"):
                result.append(text); added += 1
            index += 1
        if removed != old_count or added != new_count:
            raise ValueError("Hunk line count mismatch")
        hunks += 1
    if not hunks:
        raise ValueError("No diff hunks")
    result.extend(source[cursor:])
    return "".join(result)


def apply_unified_diff(file_path: str, diff_text: str) -> bool:
    """Write only after every hunk applies; report failure without changing the input."""
    path = Path(file_path)
    try:
        after = patch_text(path.read_text(), diff_text)
        path.write_text(after)
        return True
    except (OSError, ValueError):
        return False


def replace_text(content: str, old: str, new: str, fuzzy: bool = False) -> tuple[str, str]:
    """Return an edit and match level; ambiguous candidates always fail."""
    if not old:
        raise ValueError("Empty search")
    matches = [match.start() for match in re.finditer("(?=" + re.escape(old) + ")", content)]
    if len(matches) == 1:
        begin = matches[0]
        return content[:begin] + new + content[begin + len(old):], "exact"
    if len(matches) > 1 or not fuzzy:
        raise ValueError("Ambiguous or missing search text")
    lines = content.splitlines(keepends=True)
    count = len(old.splitlines())
    normalize = lambda value: " ".join(value.split())
    expected = normalize(old)
    candidates = [(index, "".join(lines[index:index + count]))
                  for index in range(max(0, len(lines) - count + 1))]
    for level in ("whitespace", "fuzzy"):
        choices = [(index, text) for index, text in candidates
                   if (normalize(text) == expected if level == "whitespace" else
                       difflib.SequenceMatcher(None, text, old).ratio() >= 0.9)]
        if len(choices) > 1:
            raise ValueError("Ambiguous fuzzy edit")
        if len(choices) == 1:
            index, _text = choices[0]
            replacement = new
            if lines[index + count - 1].endswith("\n") and not replacement.endswith("\n"):
                replacement += "\n"
            return "".join(lines[:index]) + replacement + "".join(lines[index + count:]), level
    raise ValueError("No fuzzy candidate above threshold")


def apply_search_replace(file_path: str, edits: list[dict[str, str]]) -> bool:
    """Apply unique exact replacements atomically; a missing search fails the whole edit."""
    path = Path(file_path)
    try:
        content = path.read_text()
        for edit in edits:
            content, _level = replace_text(content, edit["old_string"], edit["new_string"])
        path.write_text(content)
        return True
    except (OSError, ValueError, KeyError):
        return False


def normalized(content: str) -> str:
    """Ignore trailing whitespace on each line, while preserving leading indentation."""
    return "\n".join(line.rstrip() for line in content.splitlines())


def verify_file_content(file_path: str, expected_content: str) -> bool:
    """Check edited contents with the roadmap's trailing-whitespace normalization."""
    return normalized(Path(file_path).read_text()) == normalized(expected_content)


def experiment(endpoint: str, tasks: Path, templates: Path, repeats: int,
               output: Path, model: str) -> list[dict[str, Any]]:
    """Compare the three formats on the same files; append one measured result per attempt."""
    records = []
    output.parent.mkdir(parents=True, exist_ok=True)
    for task in sorted(path for path in tasks.iterdir() if path.is_dir()):
        before_files = list((task / "before").iterdir())
        if len(before_files) != 1:
            raise ValueError("Each edit task needs exactly one file")
        before = before_files[0].read_text()
        after = (task / "after" / before_files[0].name).read_text()
        for format_name in ("unified", "exact", "fuzzy"):
            for repeat in range(1, repeats + 1):
                template = (templates / (format_name + ".txt")).read_text()
                instruction = (task / "instruction.txt").read_text()
                prompt = template + "\nTask:\n" + instruction + "\nFile:\n" + before
                request = urllib.request.Request(endpoint.rstrip("/") + "/v1/chat/completions",
                    data=json.dumps({"model": model, "messages": [{"role": "user", "content": prompt}],
                                     "temperature": 0.3}).encode(), headers={"Content-Type": "application/json"})
                success = False; level = None; error = None
                try:
                    with urllib.request.urlopen(request, timeout=120) as response:
                        text = json.load(response)["choices"][0]["message"]["content"]
                    text = re.sub(r"<think>.*?</think>", "", text, flags=re.S).strip()
                    text = re.sub(r"^```[^\n]*\n|\n```$", "", text)
                    if format_name == "unified":
                        result = patch_text(before, text); level = "exact"
                    else:
                        edit = json.loads(text)
                        result, level = replace_text(before, edit["old_string"], edit["new_string"], format_name == "fuzzy")
                    success = normalized(result) == normalized(after)
                except (OSError, ValueError, KeyError, IndexError) as exception:
                    error = str(exception)
                record = {"task_id": task.name, "format": format_name, "repeat": repeat,
                          "model": model, "success": success, "match_level": level, "error": error}
                with output.open("a") as result_file:
                    result_file.write(json.dumps(record) + "\n")
                records.append(record)
    return records

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", default="http://127.0.0.1:8080")
    parser.add_argument("--model", required=True)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--tasks", type=Path, default=Path("bench/edit_tasks"))
    parser.add_argument("--templates", type=Path, default=Path("bench/edit_formats"))
    parser.add_argument("--output", type=Path, default=Path("bench/results/edit-formats.jsonl"))
    args = parser.parse_args()
    print(json.dumps(experiment(args.endpoint, args.tasks, args.templates, args.repeats, args.output, args.model), indent=2))
