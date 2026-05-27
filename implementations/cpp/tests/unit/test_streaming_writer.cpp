// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/streaming_writer.hpp"
#include "osf/binary_sample.hpp"
#include "osf/error.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace {

std::filesystem::path make_temp_path() {
    static std::atomic<std::uint64_t> counter{0};
    auto const n = counter.fetch_add(1) + 1;
    auto const filename = "osf_streaming_writer_test_" +
                          std::to_string(n) + ".osf";
    return std::filesystem::temp_directory_path() / filename;
}

struct TempFileGuard {
    std::filesystem::path path;
    ~TempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

osf::ChannelDef make_double_channel(std::string name) {
    osf::ChannelDef d;
    d.name = std::move(name);
    d.data_type = osf::DataType::Double;
    d.channel_type = osf::ChannelType::Scalar;
    d.size_of_length_value = 2;
    return d;
}

}  // namespace

// ── Category A — Lifecycle / State machine ───────────────────────────

TEST(StreamingWriterLifecycle, start_fails_without_channels) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    auto r = w.start();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterLifecycle, add_channel_after_start_returns_error) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.add_channel(make_double_channel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());
    auto r = w.add_channel(make_double_channel("b"));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterLifecycle, write_before_start_returns_error) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.add_channel(make_double_channel("a")).has_value());
    auto r = w.write_timestamped_sample<double>(0, /*ts=*/0, /*v=*/1.0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterLifecycle, double_start_returns_error) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.add_channel(make_double_channel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());
    auto r = w.start();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterLifecycle, close_in_configure_state_is_noop_success) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    auto r = w.close();
    EXPECT_TRUE(r.has_value());
    // No file was created — close() in Configure state should not open it.
    EXPECT_FALSE(std::filesystem::exists(g.path));
}

TEST(StreamingWriterLifecycle, destructor_closes_unfinished_writer_safely) {
    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        ASSERT_TRUE(w.add_channel(make_double_channel("a")).has_value());
        ASSERT_TRUE(w.start().has_value());
        // Let w go out of scope without explicit close().
    }
    // File should exist with valid magic header + metablock; the
    // reader will see zero data blocks but a complete framing.
    EXPECT_TRUE(std::filesystem::exists(g.path));
}

// ── Category B (initial subset) — Pre-write error tier ───────────────
//
// Full Cat-B coverage (oversized variable, datatype mismatch on the
// write_* path, append-without-start) arrives in Tasks 4–6 when the
// write methods land.

TEST(StreamingWriterPreWrite, add_channel_rejects_invalid_sov) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    osf::ChannelDef d;
    d.name = "a";
    d.data_type = osf::DataType::Double;
    d.channel_type = osf::ChannelType::Scalar;
    d.size_of_length_value = 3;   // invalid
    auto r = w.add_channel(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterPreWrite, add_channel_rejects_unsupported_data_type) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    osf::ChannelDef d;
    d.name = "a";
    d.data_type = osf::DataType::Unsupported;
    d.channel_type = osf::ChannelType::Scalar;
    d.size_of_length_value = 2;
    auto r = w.add_channel(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterPreWrite, add_channel_returns_sequential_indices) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    auto r0 = w.add_channel(make_double_channel("a"));
    auto r1 = w.add_channel(make_double_channel("b"));
    auto r2 = w.add_channel(make_double_channel("c"));
    ASSERT_TRUE(r0.has_value()); EXPECT_EQ(*r0, 0u);
    ASSERT_TRUE(r1.has_value()); EXPECT_EQ(*r1, 1u);
    ASSERT_TRUE(r2.has_value()); EXPECT_EQ(*r2, 2u);
}
