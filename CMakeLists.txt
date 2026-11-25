cmake_minimum_required(VERSION 3.24)
project(platypus VERSION 0.1.0
    DESCRIPTION "A C++20 terminal coding agent for locally served open-weight models"
    HOMEPAGE_URL "https://github.com/Torcken/Platypus"
    LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()
include(GNUInstallDirs)
option(PLATYPUS_BUILD_FUZZERS "Build Clang/libFuzzer parser targets" OFF)
if(PLATYPUS_BUILD_FUZZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "PLATYPUS_BUILD_FUZZERS requires Clang with the libFuzzer runtime. Use a separate Clang build directory.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CheckCXXSourceCompiles)
find_package(Threads REQUIRED)
set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
check_cxx_source_compiles("#include <thread>
#include <stop_token>
int main() { std::jthread t([](std::stop_token) {}); t.request_stop(); }" PLATYPUS_HAS_JTHREAD)
if(NOT PLATYPUS_HAS_JTHREAD)
    message(FATAL_ERROR "Platypus requires C++20 std::jthread and std::stop_token. Upgrade your compiler and standard library.")
endif()
include(cmake/CPM.cmake)
CPMAddPackage("gh:nlohmann/json@3.11.3")
CPMAddPackage(NAME Catch2 GITHUB_REPOSITORY catchorg/Catch2 GIT_TAG v3.8.1
    OPTIONS "CATCH_INSTALL_DOCS OFF" "CATCH_INSTALL_EXTRAS OFF")
CPMAddPackage(NAME tomlplusplus GITHUB_REPOSITORY marzer/tomlplusplus GIT_TAG v3.4.0)
CPMAddPackage(NAME reproc GITHUB_REPOSITORY DaanDeMeyer/reproc GIT_TAG v14.2.5
    OPTIONS "REPROC++ ON" "REPROC_TEST OFF" "REPROC_EXAMPLES OFF")
CPMAddPackage(NAME ftxui GITHUB_REPOSITORY ArthurSonzogni/FTXUI GIT_TAG v6.1.9
    OPTIONS "FTXUI_BUILD_DOCS OFF" "FTXUI_BUILD_EXAMPLES OFF" "FTXUI_BUILD_TESTS OFF")
set(_PLATYPUS_PREVIOUS_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
CPMAddPackage(NAME tree_sitter_runtime GITHUB_REPOSITORY tree-sitter/tree-sitter
    GIT_TAG v0.25.10 SOURCE_SUBDIR lib)
if(_PLATYPUS_PREVIOUS_BUILD_SHARED_LIBS STREQUAL "")
    unset(BUILD_SHARED_LIBS)
else()
    set(BUILD_SHARED_LIBS "${_PLATYPUS_PREVIOUS_BUILD_SHARED_LIBS}")
endif()
unset(_PLATYPUS_PREVIOUS_BUILD_SHARED_LIBS)
CPMAddPackage(NAME tree_sitter_c GITHUB_REPOSITORY tree-sitter/tree-sitter-c
    GIT_TAG v0.24.2 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME tree_sitter_cpp GITHUB_REPOSITORY tree-sitter/tree-sitter-cpp
    GIT_TAG v0.23.3 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME tree_sitter_python GITHUB_REPOSITORY tree-sitter/tree-sitter-python
    GIT_TAG v0.25.0 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME tree_sitter_javascript GITHUB_REPOSITORY tree-sitter/tree-sitter-javascript
    GIT_TAG v0.25.0 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME tree_sitter_typescript GITHUB_REPOSITORY tree-sitter/tree-sitter-typescript
    GIT_TAG v0.23.2 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME tree_sitter_rust GITHUB_REPOSITORY tree-sitter/tree-sitter-rust
    GIT_TAG v0.24.2 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME tree_sitter_go GITHUB_REPOSITORY tree-sitter/tree-sitter-go
    GIT_TAG v0.23.3 DOWNLOAD_ONLY YES)
add_library(tree_sitter_grammars STATIC
    ${tree_sitter_c_SOURCE_DIR}/src/parser.c
    ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
    ${tree_sitter_python_SOURCE_DIR}/src/parser.c
    ${tree_sitter_javascript_SOURCE_DIR}/src/parser.c
    ${tree_sitter_typescript_SOURCE_DIR}/typescript/src/parser.c
    ${tree_sitter_typescript_SOURCE_DIR}/tsx/src/parser.c
    ${tree_sitter_rust_SOURCE_DIR}/src/parser.c
    ${tree_sitter_go_SOURCE_DIR}/src/parser.c
)
if(EXISTS "${tree_sitter_typescript_SOURCE_DIR}/typescript/src/scanner.c")
    target_sources(tree_sitter_grammars PRIVATE
        ${tree_sitter_typescript_SOURCE_DIR}/typescript/src/scanner.c)
endif()
if(EXISTS "${tree_sitter_typescript_SOURCE_DIR}/tsx/src/scanner.c")
    target_sources(tree_sitter_grammars PRIVATE
        ${tree_sitter_typescript_SOURCE_DIR}/tsx/src/scanner.c)
endif()
foreach(grammar IN ITEMS c cpp python javascript typescript rust go)
    if(EXISTS "${tree_sitter_${grammar}_SOURCE_DIR}/src/scanner.c")
        target_sources(tree_sitter_grammars PRIVATE
            "${tree_sitter_${grammar}_SOURCE_DIR}/src/scanner.c")
    endif()
endforeach()
target_include_directories(tree_sitter_grammars PRIVATE
    ${tree_sitter_c_SOURCE_DIR}/src
    ${tree_sitter_cpp_SOURCE_DIR}/src
    ${tree_sitter_python_SOURCE_DIR}/src
    ${tree_sitter_javascript_SOURCE_DIR}/src
    ${tree_sitter_typescript_SOURCE_DIR}/typescript/src
    ${tree_sitter_typescript_SOURCE_DIR}/tsx/src
    ${tree_sitter_rust_SOURCE_DIR}/src
    ${tree_sitter_go_SOURCE_DIR}/src
)
target_link_libraries(tree_sitter_grammars PUBLIC tree-sitter)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Vulkan QUIET)
option(PLATYPUS_ASAN "Enable AddressSanitizer" OFF)
option(PLATYPUS_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
add_library(platypus_options INTERFACE)
target_include_directories(platypus_options INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_compile_options(platypus_options INTERFACE -Wall -Wextra -Wpedantic)
if(PLATYPUS_ASAN)
    target_compile_options(platypus_options INTERFACE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(platypus_options INTERFACE -fsanitize=address)
    target_compile_options(tree_sitter_grammars PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_compile_options(tree-sitter PRIVATE -fsanitize=address -fno-omit-frame-pointer)
endif()
if(PLATYPUS_UBSAN)
    target_compile_options(platypus_options INTERFACE -fsanitize=undefined -fno-sanitize-recover=all)
    target_link_options(platypus_options INTERFACE -fsanitize=undefined)
    target_compile_options(tree_sitter_grammars PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
    target_compile_options(tree-sitter PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
endif()
add_subdirectory(src/util)
add_subdirectory(src/llm)
add_subdirectory(src/core)
add_executable(platypus src/main.cpp src/tui/terminal.cpp)
target_link_libraries(platypus PRIVATE core ftxui::component ftxui::dom ftxui::screen)
target_compile_definitions(platypus PRIVATE
    PLATYPUS_VERSION="${PROJECT_VERSION}"
    PLATYPUS_SOURCE_CATALOG_PATH="${PROJECT_SOURCE_DIR}/profiles/catalog.json"
    PLATYPUS_SOURCE_UPDATE_SCRIPT_PATH="${PROJECT_SOURCE_DIR}/scripts/update.py"
    PLATYPUS_INSTALL_CATALOG_PATH="${CMAKE_INSTALL_FULL_DATADIR}/platypus/profiles/catalog.json")
if(APPLE)
    set_target_properties(platypus PROPERTIES
        INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
else()
    set_target_properties(platypus PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
install(TARGETS platypus RUNTIME DESTINATION bin)
install(DIRECTORY profiles/ DESTINATION ${CMAKE_INSTALL_DATADIR}/platypus/profiles)
install(PROGRAMS scripts/update.py DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/platypus)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
include(CTest)
if(BUILD_TESTING)
    foreach(test_name context_manager_test execution_engine_test llm_layer_test test_tools)
        add_executable(${test_name} src/tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE core Catch2::Catch2)
        target_compile_options(${test_name} PRIVATE -UNDEBUG)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-work/${test_name}")
        set_property(TARGET ${test_name} PROPERTY CXX_EXTENSIONS OFF)
        if(test_name STREQUAL "llm_layer_test")
            add_test(NAME ${test_name} COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tests/fixtures/with_server.py $<TARGET_FILE:${test_name}>)
        else()
            add_test(NAME ${test_name} COMMAND ${test_name})
        endif()
        set_tests_properties(${test_name} PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/test-work/${test_name}")
    endforeach()
    add_executable(e2e_tests src/tests/e2e/e2e_tests.cpp)
    target_link_libraries(e2e_tests PRIVATE core)
    target_compile_options(e2e_tests PRIVATE -UNDEBUG)
    add_test(NAME e2e_tests COMMAND e2e_tests)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-work/e2e_tests")
    set_tests_properties(e2e_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/test-work/e2e_tests")
    add_executable(history_truncation_test src/tests/e2e/test_history_truncation.cpp)
    target_link_libraries(history_truncation_test PRIVATE core)
    target_compile_options(history_truncation_test PRIVATE -UNDEBUG)
    add_test(NAME history_truncation_test COMMAND history_truncation_test)
    add_executable(regression_tests tests/unit/regression_tests.cpp tests/fuzz/tool_calls.cpp
        tests/fuzz/edit_format.cpp)
    target_link_libraries(regression_tests PRIVATE core Catch2::Catch2WithMain)
    add_test(NAME regression_tests COMMAND regression_tests)
    add_test(NAME cli_integration COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/integration/test_cli.py $<TARGET_FILE:platypus>)
    add_test(NAME server_backend_selection COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/integration/test_backend_selection.py)
    add_test(NAME terminal_smoke COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/integration/test_terminal.py $<TARGET_FILE:platypus>)
    set_tests_properties(context_manager_test execution_engine_test llm_layer_test test_tools
        e2e_tests history_truncation_test regression_tests cli_integration server_backend_selection
        terminal_smoke PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endif()
if(PLATYPUS_BUILD_FUZZERS)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer,address,undefined")
    check_cxx_source_compiles("#include <cstddef>
extern \"C\" int LLVMFuzzerTestOneInput(const unsigned char*, std::size_t) { return 0; }"
        PLATYPUS_HAS_LIBFUZZER)
    unset(CMAKE_REQUIRED_FLAGS)
    if(NOT PLATYPUS_HAS_LIBFUZZER)
        message(FATAL_ERROR "Clang requires an installed libFuzzer/AddressSanitizer/UBSan runtime to build fuzz targets.")
    endif()
    add_executable(fuzz_tool_calls tests/fuzz/tool_calls.cpp src/core/codec.cpp)
    target_include_directories(fuzz_tool_calls PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(fuzz_tool_calls PRIVATE nlohmann_json::nlohmann_json)
    target_compile_options(fuzz_tool_calls PRIVATE -Wall -Wextra -Wpedantic
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(fuzz_tool_calls PRIVATE -fsanitize=fuzzer,address,undefined)
    add_executable(fuzz_edit_format tests/fuzz/edit_format.cpp tests/fuzz/edit_format_main.cpp
        src/tools/edit_format.cpp)
    target_include_directories(fuzz_edit_format PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(fuzz_edit_format PRIVATE nlohmann_json::nlohmann_json)
    target_compile_options(fuzz_edit_format PRIVATE -Wall -Wextra -Wpedantic
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(fuzz_edit_format PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
