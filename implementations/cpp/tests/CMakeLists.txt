# GoogleTest integration via FetchContent (DECISIONS.md §20).
#
# Pinned to a specific release tarball with a SHA256 hash for
# reproducible, offline-after-first-fetch builds. No git client
# required.

include(FetchContent)

# Use extraction time as the file timestamp (CMP0135 NEW behavior).
# Avoids stale Make/Ninja caches if the same URL ever serves
# changed content; with a hash pin this is defensive but cheap.
FetchContent_Declare(
    googletest
    URL      https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)

# Force GoogleTest to use the dynamic CRT (/MD, /MDd) on MSVC,
# matching the default that osf_core uses. Without this, GoogleTest
# defaults to /MT and produces LNK4098 mismatches.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------

add_executable(test_error
    unit/test_error.cpp
)

target_link_libraries(test_error PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_error)

include(GoogleTest)
gtest_discover_tests(test_error)

# ---------------------------------------------------------------------------
# test_header — magic-header parser unit tests (Phase 2b).
# ---------------------------------------------------------------------------

add_executable(test_header
    unit/test_header.cpp
)

target_link_libraries(test_header PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_header)

gtest_discover_tests(test_header)

# ---------------------------------------------------------------------------
# test_types — type-string parser unit tests (Phase 3b).
# ---------------------------------------------------------------------------

add_executable(test_types
    unit/test_types.cpp
)

target_link_libraries(test_types PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_types)

gtest_discover_tests(test_types)

# ---------------------------------------------------------------------------
# test_metablock — OSF5 JSON metablock parser unit tests (Phase 3b).
# ---------------------------------------------------------------------------

add_executable(test_metablock
    unit/test_metablock.cpp
)

target_link_libraries(test_metablock PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_metablock)

gtest_discover_tests(test_metablock)

# ---------------------------------------------------------------------------
# test_header_examples — magic-header tests against reference files.
# ---------------------------------------------------------------------------

# Resolve examples/ via TO_CMAKE_PATH to enforce forward slashes;
# raw CMAKE_SOURCE_DIR can leak backslashes into the string literal.
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/../../examples" OSF_EXAMPLES_DIR_PATH)

add_executable(test_header_examples
    integration/test_header_examples.cpp
)

target_link_libraries(test_header_examples PRIVATE
    osf::osf
    GTest::gtest_main
)

target_compile_definitions(test_header_examples PRIVATE
    OSF_EXAMPLES_DIR="${OSF_EXAMPLES_DIR_PATH}")

osf_set_warnings(test_header_examples)

gtest_discover_tests(test_header_examples)

# ---------------------------------------------------------------------------
# test_metablock_examples — OSF5 metablock parser tests against
# reference files in examples/generated/ (Phase 3b).
# ---------------------------------------------------------------------------

add_executable(test_metablock_examples
    integration/test_metablock_examples.cpp
)

target_link_libraries(test_metablock_examples PRIVATE
    osf::osf
    GTest::gtest_main
)

target_compile_definitions(test_metablock_examples PRIVATE
    OSF_EXAMPLES_DIR="${OSF_EXAMPLES_DIR_PATH}")

osf_set_warnings(test_metablock_examples)

gtest_discover_tests(test_metablock_examples)
