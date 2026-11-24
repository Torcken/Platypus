"""Disposable streaming llama-server fixture for CPU-only benchmark tests."""
from contextlib import contextmanager
import http.server
import json
import threading
from typing import Iterator

@contextmanager
def fake_server(edit: dict[str, str] | None = None, responses: list[str] | None = None,
                captured: list[dict] | None = None,
                tokenization: dict | None = None) -> Iterator[str]:
    """Yield a loopback endpoint returning a scripted file edit and known token timings."""
    edit = edit or {"path": "answer.txt", "content": "verified\n"}
    counter = 0
    lock = threading.Lock()
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass
        def do_POST(self):
            request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            if captured is not None:
                captured.append({"endpoint": self.path, "body": request,
                                 "headers": dict(self.headers.items())})
            if self.path == "/tokenize":
                body = json.dumps(tokenization if tokenization is not None else
                                  {"tokens": list(range(10))}).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            nonlocal counter
            with lock:
                content = responses[min(counter, len(responses) - 1)] if responses else json.dumps(edit)
                counter += 1
            timings = {"prompt_n": 10, "cache_n": 5, "prompt_ms": 20,
                       "predicted_n": 8, "predicted_ms": 10}
            if request.get("stream"):
                chunks = [{"choices": [{"delta": {"content": content[:7]}}]},
                          {"choices": [{"delta": {"content": content[7:]}}], "timings": timings}]
                body = "".join("data: " + json.dumps(chunk) + "\r\n\r\n" for chunk in chunks)
                body += "data: [DONE]\r\n\r\n"
                kind = "text/event-stream"
            else:
                body = json.dumps({"content": content, "timings": timings} if self.path == "/completion" else
                                  {"choices": [{"message": {"content": content}}], "timings": timings})
                kind = "application/json"
            body = body.encode()
            self.send_response(200)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            for index in range(0, len(body), 5):
                self.wfile.write(body[index:index + 5])
                self.wfile.flush()
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield f"http://127.0.0.1:{server.server_port}"
        finally:
            server.shutdown()
            thread.join()
