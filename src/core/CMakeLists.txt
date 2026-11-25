cmake_minimum_required(VERSION 3.20)
project(platypus_core)

add_library(core STATIC
    codec.cpp
    ../tools/builtins.cpp
    ../tools/edit_format.cpp
    ../tools/tool.cpp
    ../agent/context_manager.cpp
    ../agent/repository_map.cpp
    plan_store.cpp
    plan_commands.cpp
    review_store.cpp
    project_checks.cpp
    execution_engine.cpp
    ../session/store.cpp
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(core PRIVATE ${tree_sitter_runtime_SOURCE_DIR}/lib/include)
target_link_libraries(core PUBLIC llm util SQLite::SQLite3 tree_sitter_grammars
    tomlplusplus::tomlplusplus)
