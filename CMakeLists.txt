# CMakeLists.txt for the cpp_toolbelt project
# This file defines the 'toolbelt' library and its dependencies using CMake.

cmake_minimum_required(VERSION 3.15)
project(cpp_toolbelt LANGUAGES CXX)

# Set C++ standard and compiler flags
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Prefer standard C++

# Add common compile options as found in toolbelt/BUILD
add_compile_options(-Wall)

# --- Apple Silicon (ARM64) specific settings ---
# CMAKE_OSX_ARCHITECTURES is expected to be set by the parent project or environment.
# We will propagate this value to sub-dependencies.
if(APPLE)
    message(STATUS "Building for Apple platform. Propagating CMAKE_OSX_ARCHITECTURES: ${CMAKE_OSX_ARCHITECTURES}")
endif()

# Include FetchContent module to manage Abseil and co dependencies
include(FetchContent)

# --- External Dependency: Abseil ---
# cpp_toolbelt depends on Abseil, so we need to fetch and make it available.
FetchContent_Declare(
    abseil
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG 20230802.0 # Matches the version in cpp_toolbelt's WORKSPACE
    # Pass architecture settings to Abseil's CMake build
    CMAKE_ARGS
        CMAKE_OSX_ARCHITECTURES="${CMAKE_OSX_ARCHITECTURES}"
)
FetchContent_MakeAvailable(abseil)
# Abseil provides targets like absl::base, absl::strings, etc.

# --- External Dependency: co ---
# cpp_toolbelt depends on co, so we need to fetch and make it available.
FetchContent_Declare(
    co
    GIT_REPOSITORY https://github.com/dallison/co.git
    GIT_TAG cf1252b2f5952d7cba83b67dd69288971c0a2b57
    # Pass architecture settings to co's CMake build
    CMAKE_ARGS
        CMAKE_OSX_ARCHITECTURES="${CMAKE_OSX_ARCHITECTURES}"
)
FetchContent_MakeAvailable(co)
# co provides the 'co' target.

# --- toolbelt Library Target ---
# Corresponds to //toolbelt:toolbelt in Bazel
# Source files are omitted here for manual addition by the user.
add_library(toolbelt STATIC
    toolbelt/bitset.h
    toolbelt/clock.h
    toolbelt/color.cc
    toolbelt/color.h
    toolbelt/fd.cc
    toolbelt/fd.h
    toolbelt/hexdump.cc
    toolbelt/hexdump.h
    toolbelt/logging.cc
    toolbelt/logging.h
    toolbelt/mutex.h
    toolbelt/payload_buffer.cc
    toolbelt/payload_buffer.h
    toolbelt/pipe.cc
    toolbelt/pipe.h
    toolbelt/sockets.cc
    toolbelt/sockets.h
    toolbelt/table.cc
    toolbelt/table.h
    toolbelt/triggerfd.cc
    toolbelt/triggerfd.h
)

# Headers are relative to the project root, so we add the current source directory.
target_include_directories(toolbelt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against Abseil and co dependencies
target_link_libraries(toolbelt PUBLIC
    absl::base
    absl::strings
    absl::optional
    absl::log # Added as it's used by toolbelt/logging
    absl::time # Added as it's used by toolbelt/time
    absl::synchronization # Added as it's used by toolbelt/utilities/thread_pool
    co # Link against the co library
)

# Note: If there are tests in cpp_toolbelt, they would need a separate target
# and link against gtest_main.

