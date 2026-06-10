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
# test_header — magic-header parser unit tests.
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
# test_types — type-string parser unit tests.
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
# test_metablock — OSF5 JSON metablock parser unit tests.
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
# test_metablock_xml — OSF4 XML metablock parser unit tests.
# ---------------------------------------------------------------------------

add_executable(test_metablock_xml
    unit/test_metablock_xml.cpp
)

target_link_libraries(test_metablock_xml PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_metablock_xml)

gtest_discover_tests(test_metablock_xml)

# ---------------------------------------------------------------------------
# test_block — block-model primitive unit tests.
# ---------------------------------------------------------------------------

add_executable(test_block
    unit/test_block.cpp
)

target_link_libraries(test_block PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_block)

gtest_discover_tests(test_block)

# ---------------------------------------------------------------------------
# test_stats — reader-stats helper unit tests.
# ---------------------------------------------------------------------------

add_executable(test_stats
    unit/test_stats.cpp
)

target_link_libraries(test_stats PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_stats)

gtest_discover_tests(test_stats)

# ---------------------------------------------------------------------------
# test_reader — BlockReader unit tests against synthetic byte sequences.
# ---------------------------------------------------------------------------

add_executable(test_reader
    unit/test_reader.cpp
)

target_link_libraries(test_reader PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_reader)

gtest_discover_tests(test_reader)

# ---------------------------------------------------------------------------
# test_data_channel — typed channel model unit tests.
# ---------------------------------------------------------------------------

add_executable(test_data_channel
    unit/test_data_channel.cpp
)

target_link_libraries(test_data_channel PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_data_channel)

gtest_discover_tests(test_data_channel)

# ---------------------------------------------------------------------------
# test_manager — DataManager builder state-machine unit tests.
# ---------------------------------------------------------------------------

add_executable(test_manager
    unit/test_manager.cpp
)

target_link_libraries(test_manager PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_manager)

gtest_discover_tests(test_manager)

# ---------------------------------------------------------------------------
# test_block_encode — block-encoder primitive unit tests.
# ---------------------------------------------------------------------------

add_executable(test_block_encode
    unit/test_block_encode.cpp
)

target_link_libraries(test_block_encode PRIVATE
    osf::osf
    GTest::gtest_main
)

# The encoder lives in src/ (non-public header).
target_include_directories(test_block_encode PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

osf_set_warnings(test_block_encode)

gtest_discover_tests(test_block_encode)

# ---------------------------------------------------------------------------
# test_durable_file — POSIX/Win32 RAII file wrapper unit tests.
# ---------------------------------------------------------------------------

add_executable(test_durable_file
    unit/test_durable_file.cpp
)

target_link_libraries(test_durable_file PRIVATE
    osf::osf
    GTest::gtest_main
)

# DurableFile lives in src/ (non-public header).
target_include_directories(test_durable_file PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

osf_set_warnings(test_durable_file)

gtest_discover_tests(test_durable_file)

# ---------------------------------------------------------------------------
# test_streaming_writer — StreamingWriter lifecycle + write tests.
# ---------------------------------------------------------------------------

add_executable(test_streaming_writer
    unit/test_streaming_writer.cpp
)

target_link_libraries(test_streaming_writer PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_streaming_writer)

gtest_discover_tests(test_streaming_writer)

# ---------------------------------------------------------------------------
# test_block_writer — BlockWriter unit tests.
# ---------------------------------------------------------------------------

add_executable(test_block_writer
    unit/test_block_writer.cpp
)

target_link_libraries(test_block_writer PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_block_writer)

gtest_discover_tests(test_block_writer)

# ---------------------------------------------------------------------------
# test_stale_value_guard — StaleValueGuard unit tests.
# ---------------------------------------------------------------------------

add_executable(test_stale_value_guard
    unit/test_stale_value_guard.cpp
)

target_link_libraries(test_stale_value_guard PRIVATE
    osf::osf
    GTest::gtest_main
)

osf_set_warnings(test_stale_value_guard)

gtest_discover_tests(test_stale_value_guard)

# ---------------------------------------------------------------------------
# test_throwing — opt-in throwing convenience layer unit tests.
# Includes integration/roundtrip_helper.hpp for the write round-trips.
# ---------------------------------------------------------------------------

add_executable(test_throwing
    unit/test_throwing.cpp
)

target_link_libraries(test_throwing PRIVATE
    osf::osf
    GTest::gtest_main
)

target_include_directories(test_throwing PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/integration")

osf_set_warnings(test_throwing)

gtest_discover_tests(test_throwing)

# ---------------------------------------------------------------------------
# test_writer_common — shared writer infrastructure unit tests.
# ---------------------------------------------------------------------------

add_executable(test_writer_common
    unit/test_writer_common.cpp
)

target_link_libraries(test_writer_common PRIVATE
    osf::osf
    GTest::gtest_main
)

target_include_directories(test_writer_common PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

osf_set_warnings(test_writer_common)

gtest_discover_tests(test_writer_common)

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
# reference files in examples/generated/.
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

# ---------------------------------------------------------------------------
# test_metablock_xml_examples — OSF4 XML metablock parser tests against
# reference files in examples/generated/ plus the motorbike/steam_loco
# field samples.
# ---------------------------------------------------------------------------

add_executable(test_metablock_xml_examples
    integration/test_metablock_xml_examples.cpp
)

target_link_libraries(test_metablock_xml_examples PRIVATE
    osf::osf
    GTest::gtest_main
)

target_compile_definitions(test_metablock_xml_examples PRIVATE
    OSF_EXAMPLES_DIR="${OSF_EXAMPLES_DIR_PATH}")

osf_set_warnings(test_metablock_xml_examples)

gtest_discover_tests(test_metablock_xml_examples)

# ---------------------------------------------------------------------------
# test_reader_examples — BlockReader integration tests against the
# generated reference files and the motorbike / steam_loco field
# samples.
# ---------------------------------------------------------------------------

add_executable(test_reader_examples
    integration/test_reader_examples.cpp
)

target_link_libraries(test_reader_examples PRIVATE
    osf::osf
    GTest::gtest_main
)

target_compile_definitions(test_reader_examples PRIVATE
    OSF_EXAMPLES_DIR="${OSF_EXAMPLES_DIR_PATH}")

osf_set_warnings(test_reader_examples)

gtest_discover_tests(test_reader_examples)

# ---------------------------------------------------------------------------
# test_manager_examples — DataManager integration tests against the
# generated reference files and the motorbike / steam_loco field
# samples.
# ---------------------------------------------------------------------------

add_executable(test_manager_examples
    integration/test_manager_examples.cpp
)

target_link_libraries(test_manager_examples PRIVATE
    osf::osf
    GTest::gtest_main
)

target_compile_definitions(test_manager_examples PRIVATE
    OSF_EXAMPLES_DIR="${OSF_EXAMPLES_DIR_PATH}")

osf_set_warnings(test_manager_examples)

gtest_discover_tests(test_manager_examples)

# ---------------------------------------------------------------------------
# test_streaming_writer_examples — cross-implementation roundtrip
# (StreamingWriter Cat-F + Cat-G).
# ---------------------------------------------------------------------------

add_executable(test_streaming_writer_examples
    integration/test_streaming_writer_examples.cpp
)

target_link_libraries(test_streaming_writer_examples PRIVATE
    osf::osf
    GTest::gtest_main
)

target_compile_definitions(test_streaming_writer_examples PRIVATE
    OSF_EXAMPLES_DIR="${OSF_EXAMPLES_DIR_PATH}")

# roundtrip_helper.hpp lives in the integration/ source dir.
target_include_directories(test_streaming_writer_examples PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/integration")

osf_set_warnings(test_streaming_writer_examples)

gtest_discover_tests(test_streaming_writer_examples)

# ---------------------------------------------------------------------------
# test_block_writer_examples — BlockWriter cross-implementation roundtrip
# (BlockWriter Cat-A + Cat-B).
# ---------------------------------------------------------------------------

add_executable(test_block_writer_examples
    integration/test_block_writer_examples.cpp
)

target_link_libraries(test_block_writer_examples PRIVATE
    osf::osf
    GTest::gtest_main
)

target_compile_definitions(test_block_writer_examples PRIVATE
    OSF_EXAMPLES_DIR="${OSF_EXAMPLES_DIR_PATH}")

# roundtrip_helper.hpp lives in the integration/ source dir.
target_include_directories(test_block_writer_examples PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/integration")

osf_set_warnings(test_block_writer_examples)

gtest_discover_tests(test_block_writer_examples)

# ---------------------------------------------------------------------------
# test_compression — DecompressingIStream + detect_compression unit tests.
# Links zlib directly to build compressed fixtures.
# ---------------------------------------------------------------------------

add_executable(test_compression
    unit/test_compression.cpp
)

target_link_libraries(test_compression PRIVATE
    osf::osf
    GTest::gtest_main
    ${OSF_ZLIB_TARGET}
)

if(OSF_ZLIB_INCLUDE_DIRS)
    target_include_directories(test_compression PRIVATE ${OSF_ZLIB_INCLUDE_DIRS})
endif()

osf_set_warnings(test_compression)

gtest_discover_tests(test_compression)

# ---------------------------------------------------------------------------
# test_compression_examples — transparent OSFZ decompression against real
# files. Links zlib directly to re-compress reference files.
# ---------------------------------------------------------------------------

add_executable(test_compression_examples
    integration/test_compression_examples.cpp
)

target_link_libraries(test_compression_examples PRIVATE
    osf::osf
    GTest::gtest_main
    ${OSF_ZLIB_TARGET}
)

target_compile_definitions(test_compression_examples PRIVATE
    OSF_EXAMPLES_DIR="${OSF_EXAMPLES_DIR_PATH}")

# roundtrip_helper.hpp lives in the integration/ source dir.
target_include_directories(test_compression_examples PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/integration")

if(OSF_ZLIB_INCLUDE_DIRS)
    target_include_directories(test_compression_examples PRIVATE ${OSF_ZLIB_INCLUDE_DIRS})
endif()

osf_set_warnings(test_compression_examples)

gtest_discover_tests(test_compression_examples)

# ---------------------------------------------------------------------------
# test_c_api — C ABI smoke test. A standalone C99 program that links the
# osf-c shared library; built only when OSF_BUILD_C_API=ON.
# Proves the header is C-compatible and the DLL/.so links + works.
# ---------------------------------------------------------------------------

if(OSF_BUILD_C_API)
    add_executable(test_c_api c_api/test_c_api.c)
    set_target_properties(test_c_api PROPERTIES LINKER_LANGUAGE C)
    target_link_libraries(test_c_api PRIVATE osf_c)
    target_compile_definitions(test_c_api PRIVATE
        OSF_EXAMPLES_DIR="${OSF_EXAMPLES_DIR_PATH}")
    osf_set_warnings(test_c_api)
    add_test(NAME c_api COMMAND test_c_api)
    # Make the osf-c shared library discoverable at run time on every OS
    # without copying or RPATH juggling.
    set_tests_properties(c_api PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:osf_c>;LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:osf_c>;DYLD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:osf_c>")
endif()
