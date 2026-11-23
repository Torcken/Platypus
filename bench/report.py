import json
import os
import math
import statistics
import datetime
from typing import List, Dict, Any

class ReportGenerator:
    """
    Processes benchmark results and generates Markdown reports.
    """

    def __init__(self, results_dir: str = "bench/results"):
        self.results_dir = results_dir

    def calculate_wilson_interval(self, successes: int, total: int, confidence: float = 0.95) -> tuple:
        """
        Computes the Wilson score interval for a given number of successes in a total number of trials.
        """
        if total == 0:
            return (0.0, 0.0)
        
        p = successes / total
        z = 1.96  # For 95% confidence
        
        denominator = 1 + z**2 / total
        centre = p + z**2 / (2 * total)
        spread = z * math.sqrt((p * (1 - p) + z**2 / (4 * total)) / total)
        
        lower = (centre - spread) / denominator
        upper = (centre + spread) / denominator
        
        return max(0.0, min(1.0, lower)), max(0.0, min(1.0, upper))

    def generate_markdown_report(self, results: List[Dict[str, Any]], output_path: str = "bench/report.md") -> str:
        """
        Generates a Markdown report summarizing the benchmark results.
        """
        # Group results by task_id
        tasks = {}
        for res in results:
            tid = res.get("task_id", "unknown")
            if tid not in tasks:
                tasks[tid] = []
            tasks[tid].append(res)

        report_lines = [
            "# Benchmark Report",
            f"Generated on: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
            "",
            "## Summary Table",
            "| Task ID | Success Rate (95% CI) | Avg Wall Time (s) | Turns | Prompt Tokens | Cache Hit |",
            "| --- | --- | --- | --- | --- | --- |"
        ]

        for tid, task_results in tasks.items():
            successes = sum(1 for r in task_results if r.get("status") == "success")
            total = len(task_results)
            
            # Calculate statistics
            wall_times = [r["wall_time_s"] for r in task_results]
            avg_wall_time = statistics.mean(wall_times) if wall_times else 0.0
            
            # Aggregate prompt tokens and cache hits
            total_prompt_tokens = sum(r.get("prompt_tokens_total", 0) for r in task_results)
            avg_cache_hit = statistics.mean([r.get("cache_hit_ratio", 0) for r in task_results]) if task_results else 0.0
            
            # Calculate Wilson Interval
            lower, upper = self.calculate_wilson_interval(successes, total)
            
            success_rate_str = f"{successes/total:.2%} ({lower:.2%}-{upper:.2%})"
            
            report_lines.append(f"| {tid} | {success_rate_str} | {avg_wall_time:.2f} | {sum(r.get('turns', 0) for r in task_results)/total:.1f} | {total_prompt_tokens} | {avg_cache_hit:.2%} |")

        report_lines.append("")
        report_lines.append("## Detailed Results")
        
        for tid, task_results in tasks.items():
            report_lines.append(f"### {tid}")
            report_lines.append("| Repeat | Status | Wall Time (s) | Turns | Prompt Tokens | Cache Hit |")
            report_lines.append("| --- | --- | --- | --- | --- | --- |")
            for r in task_results:
                report_lines.append(f"| {r.get('repeat')} | {r.get('status')} | {r.get('wall_time_s', 0):.2f} | {r.get('turns', 0)} | {r.get('prompt_tokens_total', 0)} | {r.get('cache_hit_ratio', 0):.2%} |")
            report_lines.append("")

        full_report = "\n".join(report_lines)
        
        with open(output_path, 'w') as f:
            f.write(full_report)
            
        return full_report

if __name__ == "__main__":
    # Example usage
    results = [
        {"task_id": "task_001", "status": "success", "wall_time_s": 10.0, "turns": 3, "prompt_tokens_total": 100, "cache_hit_ratio": 0.5, "repeat": 1},
        {"task_id": "task_001", "status": "success", "wall_time_s": 11.0, "turns": 3, "prompt_tokens_total": 100, "cache_hit_ratio": 0.4, "repeat": 2},
    ]
    generator = ReportGenerator()
    print(generator.generate_markdown_report(results))
