# Grammar-constrained decoding

`llama.cpp` can restrict generated text to a grammar. Platypus can build a GBNF grammar from the
tool declarations already sent to the model. The current generator supports the three profile
families (`json_tagged`, `qwen_xml`, and `gemma`) and the flat string, integer, number, and boolean
properties used by the built-in tools. Required keys must appear; optional keys may be omitted;
property order is unrestricted.

The generated grammar currently accepts exactly one tool call. It is an offline primitive; the
backend does not send it yet. Attaching it to every turn would prevent a normal text answer from
being generated. A future request mode must let the model produce either a final answer or a valid
call without making the two forms ambiguous.

The parser remains authoritative after generation. It checks call structure and the tool registry
checks argument schemas before execution. Grammar constraints do not grant permission to run a
tool, and tool output remains untrusted input. Unsupported schemas or servers that reject grammar
requests must use the existing tolerant parser path.

The generator's syntax and schema regressions are offline checks only. Before enabling it, validate
the emitted grammar with the pinned llama.cpp grammar validator, then compare grammar-on and
grammar-off runs on the same tasks, model, server, and hardware. Record invalid calls, task success,
prompt usage, and failures; keep the feature off if the paired results do not meet Phase 2's
acceptance criteria. Special-token matching and delimiter-heavy strings require real-server
validation.

See [ADR-019](../decisions/ADR-019-tool-call-grammar.md) and the upstream
[pinned GBNF guide](https://github.com/ggml-org/llama.cpp/blob/2d8a32502909ca065c41c4cf5f2fb1c427c047fc/grammars/README.md).
