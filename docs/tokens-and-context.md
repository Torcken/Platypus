# Tokens and Context

This document describes how tokens and context windows are handled in the Platypus benchmark system.

## Token Counting

We use the `llama.cpp` tokenizer to count tokens. For the purpose of the benchmark, we distinguish between:

1.  **Prompt Tokens**: The tokens in the user input and the preceding conversation history.
2.  **Generated Tokens**: The tokens produced by the model in response to the prompt.

## Context Management

The benchmark system must ensure that the context window of the model is not exceeded.

- **Context Limit**: The maximum context window of the model as reported by the `llama-server` (`/health` or `/tokenize` endpoints).
- **Context Window Sliding**: When a conversation exceeds the context limit, the oldest messages in the history are removed until the prompt fits within the window.
- **Cache Utilization**: We aim to maximize the reuse of the KV cache. The `cache_n` field in the `timings` block is used to monitor cache hits.

## Metrics related to Tokens and Context

- `prompt_tokens_total`: Total number of tokens in the prompt across all turns in a session.
- `prompt_tokens_processed`: Number of prompt tokens that were actually processed by the model (may be less than `prompt_tokens_total` if parts of the prompt were retrieved from the KV cache).
- `cache_hit_ratio`: The ratio of `cache_n` to `prompt_n` (or `prompt_tokens_processed`).
- `prefill_ms`: Time taken to process the prompt tokens (`prompt_ms`).

## Implementation Notes

- Use the `/tokenize` endpoint of `llama-server` to determine the token count for any given string.
- The recording proxy must calculate and record the `timings` block accurately for every request.
