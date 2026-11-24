# Context management

The context window must fit the rendered input and a reserved reply:

```text
prompt tokens + reply reservation <= profile context limit
```

Before each model request, `agent::ContextManager::prepare()` asks the backend to size the current
history. Native profiles count the exact rendered prompt with the server tokenizer; generic chat
transport returns a labelled estimate because the server applies a hidden template. Tokenizer
errors, cancellation, or an over-budget prompt stop the task before another tool can run. The
terminal reports the input count and reserved reply separately.

The profile supplies a conservative context limit. Platypus reserves up to 2,048 reply tokens, or
one quarter of a smaller context. Do not count only the input against the full model limit. Server
timings such as cache hits and prefill duration are separate benchmark measurements, not context
sizes.

History is append-only during a task. Budget failure does not silently remove old messages, rewrite
tool observations, or execute a partial next step. At the start of a later task, history above the
configured message limit may be summarized; the stable system instruction and the latest three
messages remain verbatim. The summary is added as transient, explicitly untrusted user context and
counts toward token sizing. The session store continues to retain the original append-only records.
Compaction does not rescue an over-budget task midway through execution. Summary quality and the
effect on success and prompt size still require paired real-model measurements.

See [Token budgets](token-budgets.md), [ADR-012](../decisions/ADR-012-context-sizing.md), and
[ADR-018](../decisions/ADR-018-runtime-prefix-diagnostic.md).
