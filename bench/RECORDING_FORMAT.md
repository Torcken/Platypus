# Recording Format

This document defines the JSONL format used by the recording proxy to capture `llama-server` interactions.

Exchanges are stored as individual JSON lines in `bench/recordings/` or the runner's sibling
`recordings/` directory. Aggregated run metrics are separate `bench/results/*.jsonl` files.

## Schema

Each line in the JSONL file must contain the following fields:

| Field | Type | Description |
| --- | --- | --- |
| `timestamp` | string | ISO 8601 UTC timestamp when the exchange was recorded. |
| `endpoint` | string | The API endpoint called (e.g., `/v1/chat/completions`). |
| `request` | object | The full JSON request body sent to `llama-server`. |
| `response` | object | The full JSON response body received from `llama-server`. |
| `timings` | object | The `timings` block from the `llama-server` response. |

## `timings` Block Fields

The `timings` block is provided by `llama.cpp` and must be preserved exactly as received:

- `prompt_n`: Number of prompt tokens processed (sum with `cache_n` for total prompt tokens).
- `cache_n`: Number of tokens retrieved from the KV cache.
- `prompt_ms`: Time taken to process the prompt (in milliseconds).
- `predicted_n`: Number of tokens generated.
- `predicted_ms`: Time taken to generate the tokens (in milliseconds).

## Optional native-client metadata

The Platypus benchmark adapter enables `PLATYPUS_BENCHMARK_TRACE=1`. Generation requests then
carry an `X-Platypus-Benchmark` JSON header, capped at 48 KiB. The proxy removes this header before
forwarding and stores it as `client_metadata`; the model request and prompt remain unchanged.
Normal CLI sessions do not send it. Version 1 includes the tool schemas already present in the
prompt, syntax family, reasoning delimiters, and observations (`id`, `name`, `is_error`). It omits
observation contents; IDs are stable history positions for deduplication across repeated requests.

Scoring extracts calls from actual model responses with strict JSON, Qwen XML or Gemma syntax
parsing and the same minimal schema checks as wire function calls. It does not reuse the agent's
tolerant parser. Invalid metadata makes tool metrics unavailable and records `tool_metrics_errors`.
Tool results become visible in the following request; an error immediately before cancellation
without another generation cannot be observed by this protocol. As with third-party tool
messages, error status is supplied by the client; task success comes only from the verifier.

## Example Record

```json
{
  "timestamp": "2026-11-20T10:00:00.000Z",
  "endpoint": "/v1/chat/completions",
  "request": {
    "model": "gemma-4-12b",
    "messages": [
      { "role": "user", "content": "Hello!" }
    ],
    "temperature": 0.7
  },
  "response": {
    "id": "chatcmpl-123",
    "choices": [
      {
        "message": { "role": "assistant", "content": "Hello! How can I help you today?" },
        "finish_reason": "stop"
      }
    ],
    "usage": {
      "prompt_tokens": 5,
      "completion_tokens": 10,
      "total_tokens": 15
    },
    "timings": {
      "prompt_n": 5,
      "cache_n": 2,
      "prompt_ms": 100,
      "predicted_n": 10,
      "predicted_ms": 500
    }
  }
}
```
