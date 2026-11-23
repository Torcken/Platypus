# Measuring Agents

This document outlines the methodology for measuring and evaluating the performance of coding agents within the Platypus benchmark.

## Core Metrics

We focus on several key performance indicators (KPIs) to evaluate an agent's capability and efficiency:

- **Success Rate**: The percentage of tasks successfully completed by the agent according to the test definition (verified by `verify.sh`).
- **Wall Time (Seconds)**: The total elapsed time from the start of the task until it is completed or fails.
- **Number of Turns**: The total number of interactions between the user (or automation script) and the agent.
- **Prompt Tokens (Total)**: The sum of all tokens sent in the prompts across all turns of a session.
- **Prompt Tokens (Processed)**: The number of tokens actually processed by the model (accounting for cache hits).
- **Cache Hit Ratio**: The proportion of prompt tokens that were retrieved from the KV cache.
- **Prefill Latency (ms)**: The time taken for the model to process the initial prompt.
- **Invalid Tool Calls**: The count of times the agent attempted to call a tool with incorrect parameters or non-existent tools.
- **Failed Edits**: The count of times the agent attempted a code modification that was rejected by the system or failed to apply.
- **Loops**: The number of times the agent repeated the same action or stayed in a cycle without making progress.

## Evaluation Methodology

1.  **Deterministic Verification**: Every task includes a `verify.sh` script that programmatically checks if the agent's output meets the requirements (e.g., a test passes, a specific file exists with correct content).
2.  **Statistical Significance**: We use the **Wilson interval** to calculate the confidence interval for success rates and **bootstrap intervals** for paired comparisons between different agent configurations or models.
3.  **Trace Analysis**: Each benchmark run generates a detailed JSONL trace (see `bench/RECORDING_FORMAT.md`). This allows for deep dives into *why* an agent failed (e.g., identifying a loop or a specific repeated error).

## Analysis Goals

- **Model Comparison**: Compare different models (e.g., Gemma 4 vs. others) on the same coding tasks.
- **Prompt Engineering Impact**: Evaluate how different system prompts or few-shot examples affect success rate and token efficiency.
- **Cost/Efficiency Analysis**: Correlate success rates with token usage and wall time to find the most "efficient" agent configuration.
