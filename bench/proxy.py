"""Streaming recording proxy for the pinned llama-server API, using the standard library."""
from __future__ import annotations

import argparse
import copy
from datetime import datetime, timezone
import http.client
import http.server
import json
from pathlib import Path
import re
import threading
from typing import Any
import urllib.error
import urllib.parse
import urllib.request

if __package__ in (None, ""):
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bench.tool_calls import metadata_error


class ProxyServer:
    """Forward HTTP traffic and record one redacted JSONL object per completed exchange."""

    def __init__(self, target_url: str, output_dir: str, secret_patterns: list[str] | None = None):
        self.target_url = target_url.rstrip("/")
        parsed = urllib.parse.urlsplit(self.target_url)
        if parsed.scheme not in ("http", "https") or not parsed.hostname:
            raise ValueError("Expected an http(s) target URL")
        self.output_dir = str(output_dir)
        Path(output_dir).mkdir(parents=True, exist_ok=True)
        self.recording = Path(output_dir) / "requests.jsonl"
        self.secret_patterns = [re.compile(pattern) for pattern in (secret_patterns or [])]
        self.lock = threading.Lock()
        self.server: http.server.ThreadingHTTPServer | None = None
        self.thread: threading.Thread | None = None

    def _redact(self, value: Any) -> Any:
        if isinstance(value, dict):
            return {key: "[REDACTED]" if key.lower() in {
                "authorization", "cookie", "set-cookie", "api_key", "password", "token"
            } else self._redact(item) for key, item in value.items()}
        if isinstance(value, list):
            return [self._redact(item) for item in value]
        if isinstance(value, str):
            for pattern in self.secret_patterns:
                value = pattern.sub("[REDACTED]", value)
        return value

    def _save_record(self, endpoint: str, request_data: Any, response_data: Any,
                     client_metadata: dict[str, Any] | None = None) -> None:
        timings = response_data.get("timings", {}) if isinstance(response_data, dict) else {}
        record = {
            "timestamp": datetime.now(timezone.utc).isoformat(), "endpoint": endpoint,
            "request": request_data, "response": response_data, "timings": timings,
        }
        if client_metadata is not None:
            record["client_metadata"] = client_metadata
        record = self._redact(record)
        with self.lock, self.recording.open("a", encoding="utf-8") as output:
            output.write(json.dumps(record) + "\n")

    def handle_request(self, method: str, endpoint: str, body: Any) -> dict[str, Any]:
        """Forward a nonstreaming JSON request; retained for direct callers and unit tests."""
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.target_url + endpoint, data=data, method=method,
                                         headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=300) as response:
                result = json.loads(response.read())
        except urllib.error.HTTPError as error:
            try:
                result = json.loads(error.read())
            except (ValueError, UnicodeError):
                result = {"error": str(error)}
            finally:
                error.close()
        except (OSError, ValueError) as error:
            result = {"error": str(error)}
        self._save_record(endpoint, body, result)
        return result

    @staticmethod
    def _assemble(raw: bytes, streaming: bool) -> Any:
        if not streaming:
            try:
                return json.loads(raw)
            except (ValueError, UnicodeError):
                return {"raw": raw.decode("utf-8", errors="replace")}
        chunks = []
        content = ""
        timings = {}
        calls: dict[int, dict[str, Any]] = {}
        for line in raw.decode("utf-8", errors="replace").splitlines():
            if not line.startswith("data:") or line[5:].strip() == "[DONE]":
                continue
            try:
                chunk = json.loads(line[5:].strip())
            except ValueError:
                continue
            chunks.append(chunk)
            timings.update(chunk.get("timings", {}))
            content += chunk.get("content", "")
            for choice in chunk.get("choices", []):
                delta = choice.get("delta", {})
                content += delta.get("content") or ""
                for call in delta.get("tool_calls", []):
                    item = calls.setdefault(call.get("index", 0), {
                        "type": "function", "function": {"name": "", "arguments": ""}})
                    if "id" in call:
                        item["id"] = call["id"]
                    for key in ("name", "arguments"):
                        item["function"][key] += call.get("function", {}).get(key, "")
        message: dict[str, Any] = {"content": content}
        if calls:
            message["tool_calls"] = [calls[index] for index in sorted(calls)]
        return {"choices": [{"message": message}], "content": content,
                "timings": timings, "chunks": chunks}

    def start(self, host: str = "127.0.0.1", port: int = 0) -> str:
        """Start a background loopback proxy and return its bound URL."""
        owner = self
        upstream = urllib.parse.urlsplit(self.target_url)

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.0"

            def log_message(self, *_args: Any) -> None:
                pass

            def forward(self) -> None:
                size = int(self.headers.get("Content-Length", "0"))
                body = self.rfile.read(size) if size else None
                metadata = None
                if trace := self.headers.get("X-Platypus-Benchmark"):
                    try:
                        metadata = json.loads(trace)
                        if error := metadata_error(metadata):
                            raise ValueError(error)
                    except ValueError as error:
                        metadata = {"error": str(error)}
                headers = {key: value for key, value in self.headers.items()
                           if key.lower() not in {"host", "connection", "transfer-encoding",
                                                  "x-platypus-benchmark"}}
                connection_type = (http.client.HTTPSConnection if upstream.scheme == "https"
                                   else http.client.HTTPConnection)
                connection = connection_type(upstream.hostname, upstream.port, timeout=300)
                raw = bytearray()
                try:
                    connection.request(self.command, upstream.path.rstrip("/") + self.path,
                                       body=body, headers=headers)
                    response = connection.getresponse()
                    self.send_response(response.status)
                    for key, value in response.getheaders():
                        if key.lower() not in {"connection", "transfer-encoding", "content-length"}:
                            self.send_header(key, value)
                    self.send_header("Connection", "close")
                    self.end_headers()
                    while chunk := response.read1(4096):
                        raw.extend(chunk)
                        self.wfile.write(chunk)
                        self.wfile.flush()
                    streaming = "text/event-stream" in response.getheader("Content-Type", "")
                    try:
                        request_data = json.loads(body) if body else None
                    except (ValueError, UnicodeError):
                        request_data = {"raw": body.decode(errors="replace")}
                    owner._save_record(self.path, request_data,
                                       owner._assemble(bytes(raw), streaming), metadata)
                except (OSError, http.client.HTTPException) as error:
                    owner._save_record(self.path, None, {"error": str(error)}, metadata)
                    self.close_connection = True
                finally:
                    connection.close()

            do_GET = do_POST = do_PUT = do_DELETE = do_PATCH = do_OPTIONS = forward

        self.server = http.server.ThreadingHTTPServer((host, port), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        return f"http://{host}:{self.server.server_port}"

    def stop(self) -> None:
        """Stop accepting requests and join the listener thread."""
        if self.server:
            self.server.shutdown()
            self.server.server_close()
        if self.thread:
            self.thread.join()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True)
    parser.add_argument("--output-dir", default="bench/recordings")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    proxy = ProxyServer(args.target, args.output_dir)
    print(proxy.start(port=args.port), flush=True)
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        proxy.stop()
