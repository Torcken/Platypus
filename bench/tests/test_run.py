import unittest
import json
import os
from unittest.mock import MagicMock, patch
from bench.run import Runner

class TestRunner(unittest.TestCase):
    def setUp(self):
        self.proxy_url = "http://localhost:8000"
        # Mock the client to avoid actual network calls and file system side effects
        self.mock_client = MagicMock()
        # Mock the OpenCodeClient initialization
        self.patcher_client = patch('bench.run.OpenCodeClient', return_value=self.mock_client)
        self.patcher_client.start()

        self.runner = Runner(proxy_url=self.proxy_url)
        # Ensure our mock is actually in the clients dict
        self.runner.clients["opencode"] = self.mock_client

    def tearDown(self):
        self.patcher_client.stop()

    @patch('os.path.exists')
    @patch('builtins.open', new_callable=unittest.mock.mock_open, read_data='{"repo_path": "fake/path"}')
    def test_run_task_success(self, mock_open, mock_exists):
        mock_exists.return_value = True
        
        # Configure mock client response
        self.mock_client.run_task.return_value = {"id": "1", "result": "success"}
        
        results = self.runner.run_task("task_001_cpp_refactor", "opencode", {"model": "llama-cpp"}, repeats=2)
        
        # Assertions
        self.assertEqual(len(results), 2)
        self.assertEqual(results[0]["repeat"], 1)
        self.assertEqual(results[1]["repeat"], 2)
        self.assertIn("wall_time_s", results[0])
        self.mock_client.run_task.assert_called()

    def test_run_task_unknown_client(self):
        with self.assertRaises(ValueError) as cm:
            self.runner.run_task("task_001_cpp_refactor", "invalid_client", {})
        self.assertEqual(str(cm.exception), "Unknown client: invalid_client")

    @patch('os.path.exists')
    def test_run_task_file_not_found(self, mock_exists):
        mock_exists.return_value = False
        with self.assertRaises(FileNotFoundError):
            self.runner.run_task("non_existent_task", "opencode", {})

if __name__ == "__main__":
    unittest.main()
