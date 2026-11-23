import json
import time
import os
import argparse
from typing import List, Dict, Any
from bench.adapters.opencode import OpenCodeClient

class Runner:
    """
    The primary execution engine for the benchmark.
    Orchestrates the lifecycle of tasks, clients, and result recording.
    """

    def __init__(self, proxy_url: str = "http://localhost:8000"):
        self.proxy_url = proxy_url
        self.clients = {
            "opencode": OpenCodeClient(proxy_url=proxy_url)
        }
        # Placeholder for future clients
        # self.clients["other_client"] = OtherClient(proxy_url=proxy_url)

    def run_task(self, task_id: str, client_name: str, model_config: Dict[str, Any], repeats: int = 1) -> List[Dict[str, Any]]:
        """
        Orchestrates the lifecycle of a single benchmark run.
        
        Args:
            task_id: The ID of the task to run (e.g., "task_001_cpp_refactor").
            client_name: The name of the client adapter to use.
            model_config: Configuration for the model (e.g., temperature, max_tokens).
            repeats: Number of times to run the task for bootstrap statistics.

        Returns:
            A list of result records for the given task.
        """
        if client_name not in self.clients:
            raise ValueError(f"Unknown client: {client_name}")

        # Load task definition
        task_path = f"bench/tasks/{task_id}.json"
        if not os.path.exists(task_path):
            raise FileNotFoundError(f"Task file not found: {task_path}")
        
        with open(task_path, 'r') as f:
            task = json.load(f)

        repo_path = task['repo_path']
        results = []

        print(f"--- Starting Task: {task_id} (Repeats: {repeats}) ---")
        
        for i in range(repeats):
            print(f"  Run {i+1}/{repeats}...")
            start_time = time.time()
            
            # In a real implementation, we would pass model_config to the client
            # For now, we call the client's run_task method.
            result = self.clients[client_name].run_task(task_id, repo_path)
            
            end_time = time.time()
            
            # Create a copy of the result to avoid modifying the same object if the client reuses it
            result_copy = result.copy()
            # Update result with wall_time
            result_copy['wall_time_s'] = end_time - start_time
            # Add a unique identifier for the repeat
            result_copy['repeat'] = i + 1
            
            results.append(result_copy)
            print(f"  Completed in {result_copy['wall_time_s']:.2f}s")

        print(f"--- Finished Task: {task_id} ---")
        return results

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Platypus Benchmark Runner")
    parser.add_argument("--task", required=True, help="The ID of the task to run (e.g., task_001_cpp_refactor)")
    parser.add_argument("--client", required=True, help="The name of the client adapter (e.g., opencode)")
    parser.add_argument("--model", required=True, help="The model configuration string")
    parser.add_argument("--repeats", type=int, default=1, help="Number of times to run the task for bootstrap statistics")
    
    args = parser.parse_args()

    runner = Runner()
    results = runner.run_task(
        task_id=args.task, 
        client_name=args.client, 
        model_config={"model": args.model}, 
        repeats=args.repeats
    )
    
    print(json.dumps(results, indent=2))
