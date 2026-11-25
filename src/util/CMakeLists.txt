cmake_minimum_required(VERSION 3.20)
project(platypus_util)

add_library(util STATIC
    util.cpp
    ../safety/snapshot_store.cpp
    ../platform/process.cpp
    ../platform/clipboard.cpp
    ../platform/hardware.cpp
    ../platform/model_download.cpp
    ../platform/server_manager.cpp
    ../platform/model_catalog.cpp
)

if(APPLE)
    target_sources(util PRIVATE ../platform/hardware_macos.mm)
    set_source_files_properties(../platform/hardware_macos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    find_library(PLATYPUS_METAL_FRAMEWORK Metal REQUIRED)
    find_library(PLATYPUS_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_link_libraries(util PRIVATE ${PLATYPUS_METAL_FRAMEWORK} ${PLATYPUS_FOUNDATION_FRAMEWORK})
endif()

target_include_directories(util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(util PUBLIC platypus_options nlohmann_json::nlohmann_json reproc++ Threads::Threads
    PRIVATE CURL::libcurl OpenSSL::Crypto)
if(UNIX)
    target_link_libraries(util PRIVATE ${CMAKE_DL_LIBS})
endif()
if(Vulkan_FOUND AND UNIX)
    target_compile_definitions(util PRIVATE PLATYPUS_HAVE_VULKAN=1)
endif()
