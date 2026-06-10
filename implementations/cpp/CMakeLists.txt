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
option(OSF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"     OFF)

# When the C ABI shared library is built, the static osf_core (and the
# vendored pugixml + FetchContent zlibstatic objects folded into it) must
# be position-independent so they can link into the shared osf-c on
# ELF/Mach-O. Set this before any target is created so every dependency
# inherits it. No-op on MSVC.
if(OSF_BUILD_C_API)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

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

# Vendored nlohmann/json (single-header, MIT). Used by the OSF5
# metablock parser. Attached SYSTEM to suppress upstream warnings
# under our /W4 /permissive- and -Wall -Wextra -Werror profiles.
target_include_directories(osf_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann-json"
)

# Vendored pugixml v1.15 (MIT). Used by the OSF4 metablock parser.
# Unlike the other two vendored libraries pugixml is not header-only,
# so its translation unit compiles into osf_core directly (below).
# The include path is still attached via osf::headers SYSTEM so the
# upstream's own warnings stay silent under our /W4 / -Wall profiles
# and consumers can include <pugixml.hpp> via the interface target.
target_include_directories(osf_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugixml"
)

# ---------------------------------------------------------------------------
# zlib — transparent OSFZ (gzip / zlib) decompression on read.
#
#   OSF_USE_SYSTEM_ZLIB=ON  -> find_package(ZLIB) (system / vcpkg).
#   OSF_USE_SYSTEM_ZLIB=OFF -> FetchContent zlib 1.3.1 (pinned tarball +
#                              SHA256).
#
# zlib is a PRIVATE dependency of osf_core (used only in
# src/compression.cpp; never exposed in a public header — DecompressingIStream
# hides z_stream behind a PIMPL). OSF_ZLIB_TARGET / OSF_ZLIB_INCLUDE_DIRS are
# also consumed by the compression test targets in tests/CMakeLists.txt.
# ---------------------------------------------------------------------------

set(OSF_ZLIB_INCLUDE_DIRS "")
if(OSF_USE_SYSTEM_ZLIB)
    find_package(ZLIB REQUIRED)
    set(OSF_ZLIB_TARGET ZLIB::ZLIB)
else()
    include(FetchContent)
    FetchContent_Declare(
        zlib
        URL      https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
    # zlib 1.3.1 uses directory-scoped include_directories(), so the
    # zlibstatic target carries no interface include dirs — surface them
    # explicitly for osf_core and the compression tests.
    set(OSF_ZLIB_TARGET zlibstatic)
    set(OSF_ZLIB_INCLUDE_DIRS "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
endif()

# osf::osf — core library. Static by default, shared if BUILD_SHARED_LIBS=ON.
# The internal target name is osf_core (consistent with osf_headers); the
# OUTPUT_NAME property keeps the produced file as libosf.a / osf.lib.
add_library(osf_core
    src/block.cpp
    src/block_encode.cpp
    src/compression.cpp
    src/block_writer.cpp
    src/data_channel.cpp
    src/durable_file.cpp
    src/error.cpp
    src/header.cpp
    src/manager.cpp
    src/metablock.cpp
    src/metablock_xml.cpp
    src/reader.cpp
    src/stale_value_guard.cpp
    src/stats.cpp
    src/streaming_writer.cpp
    src/writer_common.cpp
    src/types.cpp
    third_party/pugixml/pugixml.cpp
)
add_library(osf::osf ALIAS osf_core)

set_target_properties(osf_core PROPERTIES
    OUTPUT_NAME osf
)

target_link_libraries(osf_core PUBLIC osf::headers)
target_link_libraries(osf_core PRIVATE ${OSF_ZLIB_TARGET})
if(OSF_ZLIB_INCLUDE_DIRS)
    target_include_directories(osf_core PRIVATE ${OSF_ZLIB_INCLUDE_DIRS})
endif()

osf_set_warnings(osf_core)

# The vendored pugixml.cpp must not be subject to our own warning
# profile — it is third-party code and we treat it as binary-identical
# to upstream. Disable warnings on that translation unit only.
if(MSVC)
    set_source_files_properties(third_party/pugixml/pugixml.cpp
        PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(third_party/pugixml/pugixml.cpp
        PROPERTIES COMPILE_OPTIONS "-w")
endif()

# ---------------------------------------------------------------------------
# osf-c — C ABI shared library (DECISIONS §23). Built only when
# OSF_BUILD_C_API=ON. Wraps osf_core (static, folded in) behind a pure-C99
# extern "C" surface; the public header is include/osf/c_api.h.
# ---------------------------------------------------------------------------

if(OSF_BUILD_C_API)
    # The C ABI test (tests/c_api/test_c_api.c) is compiled as C; the
    # project is otherwise CXX-only. Enable C here so multi-config (MSVC,
    # auto-enables C) and single-config (Make/Ninja, does not) generators
    # both work. No-op for the default build (option OFF).
    enable_language(C)

    add_library(osf_c SHARED src/c_api.cpp)
    add_library(osf::c ALIAS osf_c)
    set_target_properties(osf_c PROPERTIES
        OUTPUT_NAME osf-c
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_compile_definitions(osf_c PRIVATE OSF_C_BUILDING)
    target_link_libraries(osf_c PRIVATE osf_core)
    target_include_directories(osf_c PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    osf_set_warnings(osf_c)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(OSF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
