cmake_minimum_required(VERSION 3.20)
project(platypus_util)

add_library(util STATIC
    util.cpp
    ../safety/snapshot_store.cpp
    ../platform/process.cpp
    ../platform/clipboard.cpp
)

target_include_directories(util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(util PUBLIC platypus_options nlohmann_json::nlohmann_json reproc++ Threads::Threads)
