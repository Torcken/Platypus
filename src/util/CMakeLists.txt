cmake_minimum_required(VERSION 3.20)
project(platypus_util)

add_library(util STATIC
    util.cpp
    ../safety/snapshot_store.cpp
    ../platform/process.cpp
    ../platform/clipboard.cpp
    ../platform/hardware.cpp
    ../platform/model_download.cpp
    ../platform/model_catalog.cpp
)

target_include_directories(util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(util PUBLIC platypus_options nlohmann_json::nlohmann_json reproc++ Threads::Threads
    PRIVATE CURL::libcurl OpenSSL::Crypto)
if(Vulkan_FOUND AND UNIX)
    target_link_libraries(util PRIVATE ${CMAKE_DL_LIBS})
    target_compile_definitions(util PRIVATE PLATYPUS_HAVE_VULKAN=1)
endif()
