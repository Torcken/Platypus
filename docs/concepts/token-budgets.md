# Token budgets before generation

A model accepts a bounded sequence of tokens. A token is a unit chosen by its tokenizer; it is not
necessarily a word, character, or byte. English prose, source code and non-English text can have
different token densities. This is why dividing every string by four is a rough estimate rather
than a reliable safety check.

The context window must hold both the input and the generated reply. With a 16,384-token window
and 2,048 tokens reserved for the reply, the next prompt must fit within 14,336 tokens. Checking
only the prompt against 16,384 would permit a request that cannot produce its full allowed reply.
For smaller profiles, Platypus reserves a quarter of the context instead, so a fixed 2,048-token
reservation cannot consume the whole window.

```text
context window = rendered prompt + reply reservation + unused space
16,384         = 10,000          + 2,048             + 4,336
```

`agent::ContextManager` keeps the conversation and owns this budget. Before each generation,
`prepare()` requests a prompt size from `llm::LlmBackend`. It returns a `ContextUsage` on success
or a `Result` error when tokenization fails, cancellation is requested, or the prompt is too large.
The loop then passes the reserved output limit to inference. Neither sizing nor rejection removes
conversation turns. Existing tool observations and their error flags remain available in the
session log.

For a native profile, the backend first renders the prompt exactly as it will send it to
`/completion`, including role delimiters, declarations and tool-response frames. It sends that
string to the same server's `/tokenize` endpoint with special-token parsing enabled. The server
also includes its model-configured special tokens. Using the deployed tokenizer avoids assuming
that two model families split text the same way. This request has a cost, which is included in
benchmark wall time even though it is not a completion turn.

With generic chat transport, the server applies its own template. Platypus cannot count that
hidden formatting exactly. It therefore budgets one token per serialized byte plus fixed wrapper
allowances and labels the result **estimated** in the terminal. Native counts are labelled
**tokenized**. Neither number is a claim about cache hits or prompt-processing time: those come
from inference response timings in the benchmark recordings.

The UI receives usage through a callback on the generation worker and copies its description
under a mutex. It never reads the context manager concurrently. On a full context, the task stops
before another model generation or tool execution, and the user can start a new session. Future
compaction policies require their own benchmark evidence; silently rewriting cached history is
not a fallback for a failed budget check.
