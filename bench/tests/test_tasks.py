"""Maintained task verifiers must fail initially and pass an independent reference solution."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOLUTIONS = {
    "bug-python": {"calc.py": "def add(a, b):\n    return a + b\n"},
    "feature-cpp": {"main.cpp": "int square(int x) { return x*x; }\nint main() { return square(7) == 49 ? 0 : 1; }\n"},
    "refactor-python": {"calc.py": "def double(value):\n    return value * 2\n\ndef calculate(a, b):\n    return double(a) + double(b)\n"},
    "tests-python": {"test_calc.py": "import unittest\nfrom calc import double\nclass Tests(unittest.TestCase):\n    def test_positive(self): self.assertEqual(double(3), 6)\n    def test_negative(self): self.assertEqual(double(-3), -6)\n    def test_zero(self): self.assertEqual(double(0), 0)\n"},
    "multi-python": {"config.py": "DEFAULT_VALUE = 7\n", "app.py": "from config import DEFAULT_VALUE\ndef result():\n    return DEFAULT_VALUE * 2\n"},
}

class TaskTests(unittest.TestCase):
    def test_all_maintained_verifiers_reject_baseline_and_accept_solution(self):
        for name, solution in SOLUTIONS.items():
            with self.subTest(task=name), tempfile.TemporaryDirectory() as temporary:
                workspace = Path(temporary) / "repo"
                shutil.copytree(ROOT / "repos" / name, workspace)
                command = ["/bin/sh", str(ROOT / "tasks" / name / "verify.sh")]
                baseline = subprocess.run(command, cwd=workspace, capture_output=True, timeout=20)
                self.assertNotEqual(baseline.returncode, 0)
                for path, content in solution.items():
                    (workspace / path).write_text(content)
                # Avoid timestamp-based bytecode reuse when replacing same-size fixture files.
                shutil.rmtree(workspace / "__pycache__", ignore_errors=True)
                verified = subprocess.run(command, cwd=workspace, capture_output=True, timeout=20)
                self.assertEqual(verified.returncode, 0, verified.stderr.decode())
