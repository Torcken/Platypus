# Recording Format

This document defines the JSONL format used by the recording proxy to capture `llama-server` interactions.

All records are stored as individual JSON lines in a file named `bench/results/<date>-<label>.jsonl`.

## Schema

Each line in the JSONL file must contain the following fields:

| Field | Type | Description |
| --- | --- | --- |
| `timestamp` | string | ISO 8601 timestamp of the request start. |
| `endpoint` | string | The API endpoint called (e.g., `/v1/chat/completions`). |
| `request` | object | The full JSON request body sent to `llama-server`. |
| `response` | object | The full JSON response body received from `llama-server`. |
| `timings` | object | The `timings` block from the `llama-server` response. |

## `timings` Block Fields

The `timings` block is provided by `llama.cpp` and must be preserved exactly as received:

- `prompt_n`: Number of tokens in the prompt.
- `cache_n`: Number of tokens retrieved from the KV cache.
- `prompt_ms`: Time taken to process the prompt (in milliseconds).
- `predicted_n`: Number of tokens generated.
- `predicted_ms`: Time taken to generate the tokens (in milliseconds).

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
