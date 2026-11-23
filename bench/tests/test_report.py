import unittest
import os
from bench.report import ReportGenerator

class TestReport(unittest.TestCase):
    def setUp(self):
        self.generator = ReportGenerator()

    def test_calculate_wilson_interval(self):
        # 50% success rate
        # p = 0.5, n = 10, z = 1.96
        # denominator = 1 + (1.96^2 / 10) = 1.38416
        # centre = 0.5 + (1.96^2 / 20) = 0.5 + 0.19208 = 0.69208
        # spread = 1.96 * sqrt((0.5 * 0.5 + 1.96^2 / 40) / 10) = 1.96 * sqrt((0.25 + 0.09604) / 10) = 1.96 * sqrt(0.034604) = 1.96 * 0.18602 = 0.3646
        # lower = (0.69208 - 0.3646) / 1.38416 = 0.2365
        # upper = (0.69208 + 0.3646) / 1.38416 = 0.7988
        
        lower, upper = self.generator.calculate_wilson_interval(5, 10)
        self.assertAlmostEqual(lower, 0.2366, places=3)
        self.assertAlmostEqual(upper, 0.7634, places=3)
        
        # 100% success rate
        # p = 1.0, n = 10, z = 1.96
        # denominator = 1 + (1.96^2 / 10) = 1.38416
        # centre = 1.0 + (1.96^2 / 20) = 1.0 + 0.19208 = 1.19208
        # spread = 1.96 * sqrt((1.0 * 0 + 1.96^2 / 40) / 10) = 1.96 * sqrt((0.09604) / 10) = 1.96 * sqrt(0.009604) = 1.96 * 0.098 = 0.19208
        # lower = (1.19208 - 0.19208) / 1.38416 = 1.0 / 1.38416 = 0.722459...
        # upper = (1.19208 + 0.19208) / 1.38416 = 1.38416 / 1.38416 = 1.0
        
        lower, upper = self.generator.calculate_wilson_interval(10, 10)
        self.assertAlmostEqual(lower, 0.7225, places=3)
        self.assertAlmostEqual(upper, 1.0, places=3)

        # 0% success rate
        # p = 0.0, n = 10, z = 1.96
        # denominator = 1 + (1.96^2 / 10) = 1.38416
        # centre = 0.0 + (1.96^2 / 20) = 0.19208
        # spread = 1.96 * sqrt((0 * 1 + 1.96^2 / 40) / 10) = 1.96 * sqrt(0.009604) = 0.19208
        # lower = (0.19208 - 0.19208) / 1.38416 = 0.0
        # upper = (0.19208 + 0.19208) / 1.38416 = 0.38416 / 1.38416 = 0.2775
        
        lower, upper = self.generator.calculate_wilson_interval(0, 10)
        self.assertAlmostEqual(lower, 0.0, places=3)
        self.assertAlmostEqual(upper, 0.2775, places=3)

        # Edge case: 0 total
        lower, upper = self.generator.calculate_wilson_interval(0, 0)
        self.assertEqual(lower, 0.0)
        self.assertEqual(upper, 0.0)

    def test_generate_markdown_report_file_creation(self):
        results = [
            {"task_id": "task_001", "status": "success", "wall_time_s": 10.0, "turns": 3, "prompt_tokens_total": 100, "cache_hit_ratio": 0.5, "repeat": 1},
            {"task_id": "task_001", "status": "success", "wall_time_s": 11.0, "turns": 3, "prompt_tokens_total": 100, "cache_hit_ratio": 0.4, "repeat": 2},
        ]
        output_path = "bench/tests/test_report.md"
        try:
            self.generator.generate_markdown_report(results, output_path=output_path)
            self.assertTrue(os.path.exists(output_path))
            with open(output_path, 'r') as f:
                content = f.read()
                self.assertIn("# Benchmark Report", content)
                self.assertIn("task_001", content)
        finally:
            if os.path.exists(output_path):
                os.remove(output_path)

if __name__ == "__main__":
    unittest.main()
