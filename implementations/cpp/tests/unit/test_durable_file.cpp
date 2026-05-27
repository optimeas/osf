// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "durable_file.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using osf::detail::DurableFile;

// Generate a unique temp path per test invocation so parallel test
// runs do not collide.
std::filesystem::path make_temp_path() {
    static std::atomic<std::uint64_t> counter{0};
    auto const n = counter.fetch_add(1) + 1;
    auto const filename = "osf_durable_file_test_" + std::to_string(n) + ".bin";
    return std::filesystem::temp_directory_path() / filename;
}

// RAII cleanup so failing tests do not leave temp files behind.
struct TempFileGuard {
    std::filesystem::path path;
    ~TempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

}  // namespace

TEST(DurableFile, create_succeeds_on_writable_temp_dir) {
    TempFileGuard g{make_temp_path()};
    auto r = DurableFile::create(g.path);
    ASSERT_TRUE(r.has_value()) << "create failed: " << r.error().message;
    EXPECT_TRUE(r->is_open());
    EXPECT_TRUE(std::filesystem::exists(g.path));
}

TEST(DurableFile, create_fails_on_nonexistent_parent_dir) {
    auto const bad =
        std::filesystem::temp_directory_path() /
        "osf_does_not_exist_12345" / "file.bin";
    auto r = DurableFile::create(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::IoError);
}

TEST(DurableFile, write_appends_bytes_in_order) {
    TempFileGuard g{make_temp_path()};
    auto r = DurableFile::create(g.path);
    ASSERT_TRUE(r.has_value());

    std::uint8_t const part1[] = {0xDE, 0xAD};
    std::uint8_t const part2[] = {0xBE, 0xEF};
    ASSERT_TRUE(r->write(part1, sizeof(part1)).has_value());
    ASSERT_TRUE(r->write(part2, sizeof(part2)).has_value());
    ASSERT_TRUE(r->force().has_value());
    ASSERT_TRUE(r->close().has_value());

    // Read back and verify order.
    std::ifstream in{g.path, std::ios::binary};
    std::vector<std::uint8_t> buf{
        std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    ASSERT_EQ(buf.size(), 4u);
    EXPECT_EQ(buf[0], 0xDE);
    EXPECT_EQ(buf[1], 0xAD);
    EXPECT_EQ(buf[2], 0xBE);
    EXPECT_EQ(buf[3], 0xEF);
}

TEST(DurableFile, force_commits_buffered_writes) {
    // The strongest claim we can make portably under exclusive-lock
    // semantics: after force()+close(), the bytes are readable from an
    // independent stream. We do not try to simulate power loss, and we
    // cannot disentangle force()'s contribution from close()'s, but the
    // existence of the bytes after force() (regardless of what close()
    // adds) is the necessary durability invariant.
    TempFileGuard g{make_temp_path()};
    auto r = DurableFile::create(g.path);
    ASSERT_TRUE(r.has_value());
    std::uint8_t const data[] = {0x42};
    ASSERT_TRUE(r->write(data, 1).has_value());
    ASSERT_TRUE(r->force().has_value());
    ASSERT_TRUE(r->close().has_value());

    std::ifstream in{g.path, std::ios::binary};
    ASSERT_TRUE(in.good());
    int const byte = in.get();
    EXPECT_EQ(byte, 0x42);
}

TEST(DurableFile, close_is_idempotent) {
    TempFileGuard g{make_temp_path()};
    auto r = DurableFile::create(g.path);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->close().has_value());
    EXPECT_FALSE(r->is_open());
    // Second close is a no-op success.
    EXPECT_TRUE(r->close().has_value());
}

TEST(DurableFile, write_after_close_returns_io_error) {
    TempFileGuard g{make_temp_path()};
    auto r = DurableFile::create(g.path);
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(r->close().has_value());

    std::uint8_t const data[] = {0x00};
    auto wr = r->write(data, 1);
    ASSERT_FALSE(wr.has_value());
    EXPECT_EQ(wr.error().code, osf::Error::Code::IoError);
}

TEST(DurableFile, move_ctor_transfers_handle) {
    TempFileGuard g{make_temp_path()};
    auto r = DurableFile::create(g.path);
    ASSERT_TRUE(r.has_value());
    DurableFile moved{std::move(*r)};
    EXPECT_FALSE(r->is_open());
    EXPECT_TRUE(moved.is_open());

    std::uint8_t const data[] = {0x77};
    EXPECT_TRUE(moved.write(data, 1).has_value());
    EXPECT_TRUE(moved.force().has_value());
}

TEST(DurableFile, destructor_closes_open_file) {
    auto const path = make_temp_path();
    TempFileGuard g{path};
    {
        auto r = DurableFile::create(path);
        ASSERT_TRUE(r.has_value());
        std::uint8_t const data[] = {0xAA};
        ASSERT_TRUE(r->write(data, 1).has_value());
        ASSERT_TRUE(r->force().has_value());
        // Let r go out of scope without explicit close().
    }
    // After dtor, the file is closed; we can read it from a fresh stream.
    std::ifstream in{path, std::ios::binary};
    ASSERT_TRUE(in.good());
    int const byte = in.get();
    EXPECT_EQ(byte, 0xAA);
}
