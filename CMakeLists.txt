cmake_minimum_required(VERSION 3.20)
project(kvstore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for tooling (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warning flags - production code should compile cleanly with strict warnings
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

# sanitizer options
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

# Define the library
add_library(kvstore)

target_sources(kvstore
    PRIVATE
        src/core/store.cpp
        src/core/wal.cpp
        src/net/server/server.cpp
        src/net/text_protocol.cpp
        src/net/client/client.cpp
        src/core/snapshot.cpp
        src/core/disk_store.cpp
        src/util/signal_handler.cpp
        src/util/logger.cpp
        src/util/config.cpp
        src/net/binary_protocol.cpp
        src/net/protocol_handler.cpp
)

target_include_directories(kvstore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

add_executable(kvstore-server
    bin/server_main.cpp
)

target_link_libraries(kvstore-server
    PRIVATE
        kvstore
)

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)


# Enable testing
enable_testing()
add_subdirectory(tests)