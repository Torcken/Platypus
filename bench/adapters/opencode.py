import subprocess
import json
import os
from typing import List, Dict, Any

class OpenCodeClient:
    """
    A client adapter for the 'opencode' tool.
    It interacts with the tool to perform coding tasks and records
    the interaction with the proxy.
    """

    def __init__(self, proxy_url: str = "http://localhost:8000"):
        self.proxy_url = proxy_url

    def run_task(self, task_id: str, repo_path: str) -> Dict[str, Any]:
        """
        Runs a single task using the opencode tool.
        """
        # Load task definition
        task_path = f"bench/tasks/{task_id}.json"
        with open(task_path, 'r') as f:
            task = json.load(f)

        description = task['description']
        prompt_prefix = task['client_config']['prompt_prefix']
        
        # This is a placeholder for the actual opencode CLI command
        # In a real implementation, we would use subprocess to call 'opencode'
        # and point it to our proxy.
        
        print(f"Running task {task_id} using opencode...")
        
        # Example of how the command might be constructed:
        # command = [
        #     "opencode",
        #     "--model", "llama-cpp",
        #     "--base-url", self.proxy_url,
        #     "--repo", repo_path,
        #     "--prompt", f"{prompt_prefix}{description}"
        # ]
        # result = subprocess.run(command, capture_output=True, text=True)

        # For now, we return a dummy success
        return {
            "task_id": task_id,
            "status": "success",
            "wall_time_s": 10.5,
            "turns": 3,
            "prompt_tokens_total": 150,
            "prompt_tokens_processed": 120,
            "cache_hit_ratio": 0.2,
            "prefill_ms": 200,
            "invalid_tool_calls": 0,
            "failed_edits": 0,
            "loops": 0
        }

if __name__ == "__main__":
    client = OpenCodeClient()
    # Example run
    # print(client.run_task("task_001_cpp_refactor", "bench/repos/cpp_refactor"))
