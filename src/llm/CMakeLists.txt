cmake_minimum_required(VERSION 3.20)
project(platypus_llm)

add_library(llm STATIC
    backend.cpp
    chat_formatter.cpp
    fake_backend.cpp
    llama_server_backend.cpp
    llm_backend_factory.cpp
    model_profile.cpp
    tool_grammar.cpp
)

target_include_directories(llm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(llm PUBLIC util CURL::libcurl tomlplusplus::tomlplusplus)
