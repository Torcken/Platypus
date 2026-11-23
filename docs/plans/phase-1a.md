# Phase 1a: Core Layer and Execution Engine

This plan outlines the development of the core C++ components for the Platypus agent, focusing on infrastructure, LLM integration, and the execution loop.

## Goals
- Establish a robust C++20 foundation with standard library-first utilities.
- Implement a flexible LLM backend system supporting multiple providers.
- Create a reliable "think-act-observe" execution engine.
- Provide a base set of tools for agent interaction with the environment.

## Tasks
- [x] Create directory structure for src (util, llm, core) and profiles
- [x] Create CMakeLists.txt with C++20 and std::jthread check
- [x] Create CMakePresets.json
- [x] Implement Core Utilities (Result<T>, logger, string utils)
- [x] Implement Concurrency Model (Task, ConcurrencyManager, std::stop_token)
- [x] Implement LLM Layer: LlmBackend interface and FakeBackend
- [x] Implement LLM Layer: LlamaServerBackend
- [x] Implement Chat Template (ChatFormatter)
- [x] Implement Model Profiles (ModelProfileManager)
- [x] Implement Tool System Foundation (ToolRegistry, Tool struct)
- [x] Implement Execution Engine (think-act-observe loop, history handling)
- [x] Implement Common Tool Placeholders (filesystem, shell)
- [x] Create Main Entry Point (src/main.cpp)

## Verification
- Unit tests for `Result`, `Logger`, and `StringUtils`.
- Unit tests for `FakeBackend` and `LlamaServerBackend` connection.
- Unit test for `ExecutionEngine` loop with `FakeBackend`.
- Successful compilation and execution of `src/main.cpp` with `fake_backend`.
