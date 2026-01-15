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

# Define the library
add_library(kvstore)

target_sources(kvstore
    PRIVATE
        src/kvstore.cpp
)

target_include_directories(kvstore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Enable testing
enable_testing()
add_subdirectory(tests)