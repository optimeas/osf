# Top-level CMake configuration for the OSF C++ library.
#
# See DECISIONS.md §20 for the architectural rationale (standalone
# C++17 implementation, two-target layout, vendored deps).

cmake_minimum_required(VERSION 3.20)

project(osf
    VERSION 0.0.1
    DESCRIPTION "Open Streaming Format — C++ implementation"
    LANGUAGES CXX
)

# C++17 is the fixed baseline (DECISIONS.md §20). No CMAKE_CXX_STANDARD
# option exposed — migration to a newer standard is a deliberate
# library upgrade, not a build-time switch.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Build options (DECISIONS.md §20)
# ---------------------------------------------------------------------------

option(BUILD_SHARED_LIBS    "Build shared instead of static library" OFF)
option(OSF_BUILD_TESTS      "Build unit and integration tests"        ON)
option(OSF_BUILD_EXAMPLES   "Build example executables"               ON)
option(OSF_BUILD_C_API      "Build the C ABI shared library wrapper"  OFF)
option(OSF_USE_SYSTEM_ZLIB  "Prefer system zlib over FetchContent"    OFF)

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# ---------------------------------------------------------------------------
# Generated version header
# ---------------------------------------------------------------------------

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/osf/version.hpp"
    @ONLY
)

# ---------------------------------------------------------------------------
# Library targets
# ---------------------------------------------------------------------------

# osf::headers — interface target carrying the public include paths.
# Header-only consumers can link this directly. Library targets and
# tests inherit from it.
add_library(osf_headers INTERFACE)
add_library(osf::headers ALIAS osf_headers)

target_include_directories(osf_headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

# Vendored tl::expected — attached SYSTEM so its warnings are
# suppressed regardless of the consumer's warning level.
target_include_directories(osf_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tl-expected"
)

# osf::osf — core library. Static by default, shared if BUILD_SHARED_LIBS=ON.
# The internal target name is osf_core (consistent with osf_headers); the
# OUTPUT_NAME property keeps the produced file as libosf.a / osf.lib.
add_library(osf_core
    src/error.cpp
)
add_library(osf::osf ALIAS osf_core)

set_target_properties(osf_core PROPERTIES
    OUTPUT_NAME osf
)

target_link_libraries(osf_core PUBLIC osf::headers)

osf_set_warnings(osf_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(OSF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
