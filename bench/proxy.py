import json
import time
import logging
import urllib.request
import urllib.error
import urllib.parse
from datetime import datetime, timezone
from typing import Any, Dict, Optional
import os

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

class ProxyServer:
    def __init__(self, target_url: str, output_dir: str):
        self.target_url = target_url
        self.output_dir = output_dir
        # Ensure output directory exists
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)

    def _save_record(self, endpoint: str, request_data: Dict[str, Any], response_data: Dict[str, Any]):
        """Saves a request-response pair to a JSONL file."""
        date_str = datetime.now().strftime("%Y-%m-%d")
        filename = f"{date_str}-proxy.jsonl"
        file_path = os.path.join(self.output_dir, filename)
        
        record = {
            "timestamp": datetime.now(timezone.utc).isoformat() + "Z",
            "endpoint": endpoint,
            "request": request_data,
            "response": response_data,
            "timings": response_data.get("timings", {})
        }
        
        with open(file_path, "a") as f:
            f.write(json.dumps(record) + "\n")

    def handle_request(self, method: str, endpoint: str, body: Optional[Dict[str, Any]]) -> Dict[str, Any]:
        """
        Handles a request to the proxy, forwards it to the target, and saves the record.
        Uses standard library urllib.request.
        """
        logger.info(f"Received {method} request for {endpoint}")

        url = f"{self.target_url}{endpoint}"
        data = json.dumps(body).encode("utf-8") if body else None
        
        headers = {
            "Content-Type": "application/json",
            "User-Agent": "PlatypusProxy/1.0"
        }
        
        req = urllib.request.Request(url, data=data, method=method, headers=headers)

        try:
            with urllib.request.urlopen(req) as response:
                content = response.read().decode("utf-8")
                response_json = json.loads(content)
                self._save_record(endpoint, body, response_json)
                return response_json
        except urllib.error.HTTPError as e:
            # Handle non-200 responses
            error_body = e.read().decode("utf-8")
            try:
                response_json = json.loads(error_body)
            except json.JSONDecodeError:
                response_json = {"error": error_body}
            
            self._save_record(endpoint, body, response_json)
            return response_json
        except Exception as e:
            logger.error(f"Request failed: {e}")
            return {"error": str(e)}

    def start(self, host: str, port: int):
        """
        Placeholder for server start logic.
        In a real production app, we'd use a proper WSGI/ASGI server.
        """
        print(f"Proxy starting on {host}:{port} targeting {self.target_url}")
        pass

if __name__ == "__main__":
    # Example usage:
    # proxy = ProxyServer(target_url="http://localhost:8080", output_dir="bench/results")
    # result = proxy.handle_request("POST", "/v1/chat/completions", {"prompt": "hi"})
    # print(result)
    pass
