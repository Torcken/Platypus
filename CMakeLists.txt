cmake_minimum_required(VERSION 3.20)

project(platypus VERSION 0.1.0 LANGUAGES CXX)

# C++20 support is mandatory
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Toolchain check for std::jthread (C++20)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|MSVC|AppleClang" OR 
   (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.0") OR
   (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.0") OR
   (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND MSVC_VERSION VERSION_LESS "19.24"))
    message(FATAL_ERROR "Compiler does not appear to support std::jthread (C++20). Please upgrade your compiler.")
endif()

# Include directories
include_directories(src)

# Add subdirectories
add_subdirectory(src/util)
add_subdirectory(src/llm)
add_subdirectory(src/core)

add_executable(context_manager_test src/tests/context_manager_test.cpp)
target_link_libraries(context_manager_test PRIVATE core llm util)

add_executable(e2e_tests src/tests/e2e/e2e_tests.cpp)
target_link_libraries(e2e_tests PRIVATE core llm util)


# Add profiles (for TOML parsing, we'll need a library later)
# For now, just a placeholder
file(GLOB_RECURSE PROFILES_FILES "profiles/*.toml")
message(STATUS "Found model profiles: ${PROFILES_FILES}")
