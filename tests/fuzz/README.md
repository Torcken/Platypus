# Tool-call parser fuzzing

Normal dev/ASan/UBSan CTest runs include 2,000 deterministic mutations through the same fuzz entry
point. They are regression smoke checks, not a coverage-guided fuzz campaign. Parsed calls are
never executed. Invariants: bounded call count, object arguments, nonempty names, valid JSON
serialization and no exception escaping the parser.

For coverage-guided fuzzing, use a separate Clang build with its libFuzzer, ASan and UBSan runtimes:
The build/flag protocol follows the [LLVM 18.1.8 libFuzzer documentation](https://releases.llvm.org/18.1.8/docs/LibFuzzer.html).

```sh
cmake -S . -B build/fuzz -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DPLATYPUS_BUILD_FUZZERS=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build/fuzz --target fuzz_tool_calls -j2
mkdir -p build/fuzz/corpus/tool_calls
cp tests/fuzz/corpus/tool_calls/* build/fuzz/corpus/tool_calls/
./build/fuzz/fuzz_tool_calls build/fuzz/corpus/tool_calls -runs=10000 -max_len=65537 \
  -timeout=5 -artifact_prefix=build/fuzz/
```

Copy seeds into the build directory because libFuzzer grows its corpus. Keep crash artifacts,
minimize each reproducer, and add a focused regression before fixing the parser. The parser limits
responses to 64 KiB, batches to 64 calls and JSON depth to 64. The agent's tolerant JSON repairs
remain distinct from the benchmark's strict scoring parser.

Clang/libFuzzer is not installed in the current development environment. Local checks cover the
entry point with GCC and both sanitizers; an actual libFuzzer campaign remains unverified. An
edit-format fuzz target will follow the EditFormat interface.
