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
