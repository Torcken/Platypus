"""Run C++ HTTP integration tests against a disposable loopback server."""
import http.server
import json
import os
import subprocess
import sys
import threading

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        body = b'{"status":"ok"}'
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if self.path == "/tokenize":
            body = json.dumps({"tokens": [1, 2, 3]})
            content_type = "application/json"
        elif self.path == "/completion":
            if "Sampling raw" in request.get("prompt", ""):
                body = json.dumps({"content": json.dumps({key: request[key] for key in ("top_k", "top_p", "seed")})})
            else:
                body = json.dumps({"content": "Fixture raw completion."})
            content_type = "application/json"
        elif request.get("stream") and any(
            message.get("content") == "Sampling stream" for message in request.get("messages", [])
        ):
            sampling = {key: request[key] for key in ("top_k", "top_p", "seed")}
            chunk = {"choices": [{"delta": {"content": json.dumps(sampling)}, "finish_reason": "stop"}]}
            body = "data: " + json.dumps(chunk) + "\n\ndata: [DONE]\n\n"
            content_type = "text/event-stream"
        elif request.get("stream") and any(
            message.get("content") == "Incomplete tools" for message in request.get("messages", [])
        ):
            chunks = [
                {"choices": [{"delta": {"tool_calls": [{"index": 0, "id": "partial", "function": {"name": "read_file", "arguments": "{}"}}]}, "finish_reason": None}]},
                {"choices": [{"delta": {}, "finish_reason": "stop"}]},
            ]
            body = "".join("data: " + json.dumps(chunk) + "\n\n" for chunk in chunks)
            body += "data: [DONE]\n\n"
            content_type = "text/event-stream"
        elif request.get("stream") and any(
            message.get("content") == "Use tools" for message in request.get("messages", [])
        ):
            chunks = [
                {"choices": [{"delta": {"content": "Explanation with {\"tool_calls\": []}."}, "finish_reason": None}]},
                {"choices": [{"delta": {"tool_calls": [{"index": 0, "id": "call_a", "type": "function", "function": {"name": "read_", "arguments": "{\"path\":"}}]}, "finish_reason": None}]},
                {"choices": [{"delta": {"tool_calls": [{"index": 1, "id": "call_b", "type": "function", "function": {"name": "list_files", "arguments": "{\"path\":\".\"}"}}, {"index": 0, "function": {"name": "file", "arguments": "\"README.md\"}"}}]}, "finish_reason": None}]},
                {"choices": [{"delta": {}, "finish_reason": "tool_calls"}]},
            ]
            body = "".join("data:" + json.dumps(chunk) + "\n\n" for chunk in chunks)
            body += "data: [DONE]\n\n"
            content_type = "text/event-stream"
        elif request.get("stream"):
            chunks = [
                {"choices": [{"delta": {"content": "streamed "}, "finish_reason": None}]},
                {"choices": [{"delta": {"content": "response"}, "finish_reason": "stop"}]},
            ]
            body = "".join("data: " + json.dumps(chunk) + "\n\n" for chunk in chunks)
            body += "data: [DONE]\n\n"
            content_type = "text/event-stream"
        else:
            if any(
                message.get("content") == "Sampling check" for message in request.get("messages", [])
            ):
                content = json.dumps({key: request[key] for key in ("top_k", "top_p", "seed")})
            else:
                content = "Fixture response from HTTP server."
            body = json.dumps({"choices": [{"message": {"content": content}}]})
            content_type = "application/json"
        data = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        for index in range(0, len(data), 7):
            self.wfile.write(data[index:index+7])
            self.wfile.flush()

with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    env = dict(os.environ, PLATYPUS_TEST_SERVER=f"http://127.0.0.1:{server.server_port}")
    try:
        result = subprocess.run(sys.argv[1:], env=env, timeout=20)
    finally:
        server.shutdown()
        thread.join()
    sys.exit(result.returncode)
