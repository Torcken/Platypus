"""End-to-end runner checks with real subprocesses, HTTP and maintained verifiers."""
import json
from pathlib import Path
import tempfile
import unittest
from bench.run import Runner, run_process
from bench.tests.fake_server import fake_server
from bench.report import ReportGenerator

class TestRunner(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "repos/example").mkdir(parents=True)
        (self.root / "repos/example/original.txt").write_text("original")
        self.task = self.root / "tasks/example"
        self.task.mkdir(parents=True)
        (self.task / "task.toml").write_text(
            'repository = "repos/example"\ninstruction = "Write answer.txt"\ntimeout = 5\n')
        (self.task / "verify.sh").write_text('test "$(cat answer.txt)" = verified\n')
        self.runner = Runner(root=self.root)

    def tearDown(self):
        self.temporary.cleanup()

    def test_run_task_success(self):
        with fake_server() as endpoint:
            self.runner.proxy_url = endpoint
            results = self.runner.run_task("example", "fake", {"model": "fixture"}, repeats=2)
        self.assertEqual(len(results), 2)
        self.assertEqual([record["repeat"] for record in results], [1, 2])
        self.assertTrue(all(record["success"] for record in results))
        self.assertFalse((self.root / "repos/example/answer.txt").exists())
        self.assertEqual(results[0]["turns"], 1)
        self.assertEqual(results[0]["prompt_tokens_total"], 15)
        self.assertEqual(results[0]["prompt_tokens_processed"], 10)
        self.assertAlmostEqual(results[0]["cache_hit_ratio"], 1 / 3)
        self.assertEqual(results[0]["prefill_ms"], 20)
        self.assertIsNone(results[0]["invalid_tool_calls"])
        recorded = [json.loads(line) for line in self.runner.output.read_text().splitlines()]
        self.assertEqual(len(recorded), 2)
        report = ReportGenerator().generate_markdown_report(results, str(self.root / "report.md"))
        self.assertIn("example", report)

    def test_matrix_excludes_warmups_and_records_repeat_numbers(self):
        with fake_server() as endpoint:
            self.runner.proxy_url = endpoint
            results = self.runner.run_matrix(["example"], ["fake"], {"model": "fixture"},
                                             repeats=2, warmups=1, seed=7)
        self.assertEqual([item["repeat"] for item in results], [1, 2])
        self.assertTrue(all(not item["warmup"] for item in results))
        self.assertEqual(len(self.runner.output.read_text().splitlines()), 2)
        self.assertEqual(results[0]["task_commit"], results[1]["task_commit"])

    def test_fixture_runs_cannot_be_published_as_baselines(self):
        with self.assertRaisesRegex(ValueError, "Baseline metadata incomplete"):
            self.runner.run_task("example", "fake", {}, repeats=1, baseline=True)
        self.assertFalse(self.runner.output.exists())

    def test_run_task_unknown_client(self):
        with self.assertRaisesRegex(ValueError, "Unknown client: invalid_client"):
            self.runner.run_task("example", "invalid_client", {})

    def test_run_task_file_not_found(self):
        with self.assertRaises(FileNotFoundError):
            self.runner.run_task("non_existent_task", "fake", {})

    def test_verifier_failure_is_never_reported_as_success(self):
        (self.task / "verify.sh").write_text("exit 7\n")
        with fake_server() as endpoint:
            self.runner.proxy_url = endpoint
            results = self.runner.run_task("example", "fake", {}, repeats=2)
        self.assertTrue(all(not record["success"] for record in results))
        self.assertTrue(all(record["verify_exit_code"] == 7 for record in results))

    def test_timeout_kills_process_and_is_recorded(self):
        result = run_process(["/bin/sh", "-c", "sleep 30"], self.root,
                             {"PATH": "/usr/bin:/bin"}, 0.05)
        self.assertTrue(result["timed_out"])
        self.assertNotEqual(result["exit_code"], 0)

if __name__ == "__main__":
    unittest.main()
