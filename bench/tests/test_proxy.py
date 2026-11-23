import unittest
import json
import os
from unittest.mock import MagicMock, patch
from bench.proxy import ProxyServer
from urllib.error import HTTPError

class TestProxy(unittest.TestCase):
    def setUp(self):
        self.target_url = "http://localhost:8080"
        self.output_dir = "bench/results"
        # Ensure output dir exists for tests
        if not os.path.exists(self.output_dir):
            os.makedirs(self.output_dir)
        self.proxy = ProxyServer(self.target_url, self.output_dir)

    def tearDown(self):
        # Cleanup created files
        if os.path.exists(self.output_dir):
            for filename in os.listdir(self.output_dir):
                file_path = os.path.join(self.output_dir, filename)
                if os.path.isfile(file_path):
                    try:
                        os.remove(file_path)
                    except OSError:
                        pass

    @patch('urllib.request.urlopen')
    def test_handle_request_json(self, mock_urlopen):
        # Setup mock response
        mock_response = MagicMock()
        mock_response.read.return_value = json.dumps({"id": "1", "choices": [{"text": "hello"}]}).encode("utf-8")
        mock_response.__enter__.return_value = mock_response
        mock_urlopen.return_value = mock_response

        # Simulate a request
        body = {"prompt": "hi"}
        response = self.proxy.handle_request("POST", "/v1/chat/completions", body)
        
        # Assertions
        self.assertEqual(response["id"], "1")
        self.assertEqual(response["choices"][0]["text"], "hello")
        
        # Check if record was saved
        files = os.listdir(self.output_dir)
        self.assertTrue(len(files) > 0, "No record file was created")
        with open(os.path.join(self.output_dir, files[0]), 'r') as f:
            data = json.loads(f.read())
            self.assertEqual(data["endpoint"], "/v1/chat/completions")
            self.assertEqual(data["request"], body)

    @patch('urllib.request.urlopen')
    def test_handle_request_http_error(self, mock_urlopen):
        # Create a mock HTTPError instance that behaves exactly like it
        # We want the object to have a .read() method that returns our data.
        mock_error_instance = MagicMock()
        mock_error_instance.read.return_value = json.dumps({"error": "not found"}).encode("utf-8")
        
        # We need to ensure that isinstance(mock_error_instance, HTTPError) is true if checked
        # Or more importantly, that it is raised as an HTTPError.
        # Since handle_request uses `except urllib.error.HTTPError as e:`, 
        # we should make the side_effect an actual HTTPError instance if possible, 
        # or a mock that is treated as one.
        
        # Let's try creating a real HTTPError first.
        try:
            real_error = HTTPError(
                url="http://localhost:8080/v1/chat/completions",
                code=404,
                msg="Not Found",
                hdrs={},
                fp=None
            )
            # Patch the read method of the real error instance
            real_error.read = MagicMock(return_value=json.dumps({"error": "not found"}).encode("utf-8"))
            mock_urlopen.side_effect = real_error
        except Exception:
            # Fallback: if we can't create a real one easily, we use a mock that mimics the behavior
            mock_error_instance.spec = HTTPError
            mock_urlopen.side_effect = mock_error_instance

        # Simulate a request
        body = {"prompt": "hi"}
        response = self.proxy.handle_request("POST", "/v1/chat/completions", body)
        
        # Assertions
        self.assertEqual(response["error"], "not found")
        
        # Check if record was saved even on error
        files = os.listdir(self.output_dir)
        self.assertTrue(len(files) > 0)
        with open(os.path.join(self.output_dir, files[0]), 'r') as f:
            data = json.loads(f.read())
            self.assertEqual(data["endpoint"], "/v1/chat/completions")

if __name__ == "__main__":
    unittest.main()
