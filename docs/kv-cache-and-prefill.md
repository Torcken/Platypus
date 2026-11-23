# KV Cache and Prefill

This document describes the concepts of Key-Value (KV) caching and prefilling as they relate to the Platypus benchmark and `llama-server` interactions.

## KV Cache

The KV cache is a mechanism used by transformer models to store the keys and values of previously processed tokens. This avoids redundant computation when generating subsequent tokens in a sequence.

- **Cache Hit**: When the model retrieves tokens from the KV cache instead of re-computing them.
- **Cache Miss**: When the model must compute the KV pairs for tokens because they are not in the cache.
- **`cache_n`**: The number of tokens retrieved from the KV cache in a single request.

## Prefill

Prefilling is the process of processing the initial prompt tokens before generating any output tokens.

- **Prefill Phase**: The model processes the entire input sequence (prompt) to build the initial KV cache.
- **Generation Phase**: The model generates tokens one by one (or in batches), using the KV cache to inform the next token's computation.
- **`prompt_ms`**: The time taken to complete the prefill phase.

## Benchmarking Implications

The Platypus benchmark aims to capture and analyze these metrics to evaluate model efficiency and performance.

- **Cache Utilization**: We monitor the `cache_n` field to understand how effectively the system is reusing the KV cache across multiple turns in a conversation.
- **Prefill Efficiency**: We monitor `prompt_ms` to measure the latency of the prefill phase, which is critical for understanding the overhead of long prompts.
- **Scaling**: As prompts grow longer, the prefill time and the importance of efficient KV cache management (e.g., via `llama.cpp`'s cache mechanisms) become more significant.

## Implementation Notes

- The `timings` block provided by `llama-server` is our primary source for these metrics.
- When calculating the `cache_hit_ratio`, we use the ratio of `cache_n` to `prompt_n`.
