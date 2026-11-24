"""Statistical reporting for recorded benchmark runs; unknown values remain n/a."""
from __future__ import annotations
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import random
import statistics
from typing import Any


def label(record: dict[str, Any]) -> str:
    """Identify the full client/model configuration without merging incompatible settings."""
    config = record.get("config", {})
    return config.get("label") or " / ".join(str(record.get(key, config.get(key, "unspecified")))
                                             for key in ("client", "model")) + " / " + json.dumps(config, sort_keys=True)


def paired_bootstrap(left: list[dict[str, Any]], right: list[dict[str, Any]],
                     samples: int = 10000, seed: int = 42) -> tuple[float, float, float, int]:
    """Bootstrap paired per-task success differences, using only comparable task fixtures."""
    grouped = []
    for records in (left, right):
        tasks: dict[tuple[str, str, str], list[int]] = defaultdict(list)
        for record in records:
            key = (record["task_id"], record.get("task_fixture_sha256", ""), record.get("task_commit", ""))
            tasks[key].append(int(record.get("success", record.get("status") == "success")))
        grouped.append(tasks)
    shared = sorted(grouped[0].keys() & grouped[1].keys())
    if not shared:
        raise ValueError("No comparable paired tasks")
    differences = [statistics.mean(grouped[1][key]) - statistics.mean(grouped[0][key]) for key in shared]
    rng = random.Random(seed)
    bootstrap = sorted(statistics.mean(rng.choices(differences, k=len(differences))) for _ in range(samples))
    return statistics.mean(differences), bootstrap[int(samples * .025)], bootstrap[min(samples - 1, int(samples * .975))], len(shared)


class ReportGenerator:
    """Produce per-hardware/configuration tables and optional paired comparisons."""
    def __init__(self, results_dir: str = "bench/results"):
        self.results_dir = results_dir

    def calculate_wilson_interval(self, successes: int, total: int,
                                  confidence: float = .95) -> tuple[float, float]:
        """Compute a 95% Wilson interval for binary task outcomes."""
        if confidence != .95 or not 0 <= successes <= total:
            raise ValueError("Expected valid counts and 95% confidence")
        if not total:
            return 0., 0.
        probability = successes / total
        z = 1.96
        denominator = 1 + z * z / total
        center = probability + z * z / (2 * total)
        spread = z * math.sqrt(probability * (1 - probability) / total + z * z / (4 * total * total))
        return max(0., (center - spread) / denominator), min(1., (center + spread) / denominator)

    def generate_markdown_report(self, results: list[dict[str, Any]],
                                 output_path: str = "bench/report.md",
                                 compare: tuple[str, str] | None = None) -> str:
        """Write all required metrics, run counts, and reproducible paired intervals."""
        groups: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
        for record in results:
            groups[(record.get("hardware_tier", "unspecified"), label(record))].append(record)
        lines = ["# Benchmark Report", "", "Intervals describe these recorded runs; missing measurements are n/a.", ""]
        metrics = ["wall_time_s", "turns", "prompt_tokens_total", "prompt_tokens_processed", "cache_hit_ratio",
                   "prefill_ms", "prefill_ms_median", "invalid_tool_calls", "failed_edits", "loops"]
        for tier in sorted({key[0] for key in groups}):
            lines += ["## Hardware tier: " + tier, "",
                      "| Configuration | Runs | Success (95% Wilson) | " + " | ".join(metrics) + " |",
                      "|---|---:|---|" + "---:|" * len(metrics)]
            for (hardware, name), runs in sorted(groups.items()):
                if hardware != tier:
                    continue
                successes = sum(record.get("success", record.get("status") == "success") for record in runs)
                lower, upper = self.calculate_wilson_interval(successes, len(runs))
                values = []
                for metric in metrics:
                    observed = [record.get(metric) for record in runs]
                    values.append(f"{statistics.mean(observed):.3f}" if all(value is not None for value in observed) else "n/a")
                lines.append(f"| {name.replace('|', '/')} | {len(runs)} | {successes / len(runs):.1%} ({lower:.1%}–{upper:.1%}) | " + " | ".join(values) + " |")
            lines.append("")
        lines += ["## Tasks", "", "| Task | Runs | Successes |", "|---|---:|---:|"]
        for task in sorted({record["task_id"] for record in results}):
            runs = [record for record in results if record["task_id"] == task]
            successes = sum(record.get("success", record.get("status") == "success") for record in runs)
            lines.append(f"| {task} | {len(runs)} | {successes} |")
        if compare:
            left = [record for record in results if label(record) == compare[0]]
            right = [record for record in results if label(record) == compare[1]]
            difference, lower, upper, tasks = paired_bootstrap(left, right)
            lines += ["", "## Paired comparison", "",
                      f"{compare[1]} minus {compare[0]}: {difference:.1%} success difference; 95% paired bootstrap [{lower:.1%}, {upper:.1%}], n={tasks} tasks (seed 42)."]
            if lower <= 0 <= upper or tasks < 2:
                lines.append("Warning: the task sample does not establish a nonzero improvement.")
        text = "\n".join(lines) + "\n"
        Path(output_path).parent.mkdir(parents=True, exist_ok=True)
        Path(output_path).write_text(text, encoding="utf-8")
        return text


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", default="bench/report.md")
    parser.add_argument("--compare", nargs=2)
    arguments = parser.parse_args()
    records = [json.loads(line) for line in arguments.input.read_text().splitlines() if line.strip()]
    print(ReportGenerator().generate_markdown_report(records, arguments.output, tuple(arguments.compare) if arguments.compare else None))
