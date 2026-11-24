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
            body = json.dumps({"content": "Fixture raw completion."})
            content_type = "application/json"
        elif request.get("stream"):
            chunks = [
                {"choices": [{"delta": {"content": "streamed "}, "finish_reason": None}]},
                {"choices": [{"delta": {"content": "response"}, "finish_reason": "stop"}]},
            ]
            body = "".join("data: " + json.dumps(chunk) + "\n\n" for chunk in chunks)
            body += "data: [DONE]\n\n"
            content_type = "text/event-stream"
        else:
            body = json.dumps({"choices": [{"message": {"content": "Fixture response from HTTP server."}}]})
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
