cmake_minimum_required(VERSION 3.20)
project(platypus_core)

add_library(core STATIC
    codec.cpp
    ../tools/builtins.cpp
    ../tools/tool.cpp
    ../agent/context_manager.cpp
    execution_engine.cpp
    ../session/store.cpp
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core PUBLIC llm util SQLite::SQLite3)
