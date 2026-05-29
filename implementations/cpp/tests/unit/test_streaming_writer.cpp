// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/streaming_writer.hpp"
#include "osf/binary_sample.hpp"
#include "osf/error.hpp"
#include "osf/metablock.hpp"
#include "osf/reader.hpp"
#include "osf/manager.hpp"
#include "osf/data_channel.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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

// Decompose a written .osf file into magic line, metablock JSON,
// and remaining block-stream bytes.
struct WrittenFile {
    std::string magic_line;          // includes trailing \n
    std::string metablock_json;      // exactly metablock_len bytes
    std::vector<std::uint8_t> block_stream;
};

WrittenFile read_written_file(std::filesystem::path const& path) {
    WrittenFile wf;
    std::ifstream in{path, std::ios::binary};
    std::string line;
    std::getline(in, line);   // reads up to (but not including) \n
    auto const space = line.find(' ');
    auto const len_str = line.substr(space + 1);
    auto const metablock_len =
        static_cast<std::size_t>(std::stoul(len_str));
    wf.magic_line = line + "\n";
    std::vector<char> mb(metablock_len);
    if (metablock_len > 0) {
        in.read(mb.data(), static_cast<std::streamsize>(metablock_len));
    }
    wf.metablock_json.assign(mb.data(), metablock_len);
    wf.block_stream.assign(std::istreambuf_iterator<char>{in},
                           std::istreambuf_iterator<char>{});
    return wf;
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

// ── Category C — Byte-exact tests ────────────────────────────────────

TEST(StreamingWriterByteExact, magic_header_is_OSF5_with_metablock_length) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.add_channel(make_double_channel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());
    ASSERT_TRUE(w.close().has_value());

    auto const wf = read_written_file(g.path);
    EXPECT_EQ(wf.magic_line.substr(0, 5), "OSF5 ");
    EXPECT_EQ(wf.magic_line.back(), '\n');
    EXPECT_EQ(wf.metablock_json.size(),
              static_cast<std::size_t>(std::stoul(
                  wf.magic_line.substr(5,
                                       wf.magic_line.size() - 6))));
}

TEST(StreamingWriterByteExact, metablock_json_starts_with_envelope) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    w.set_creator("test:1");
    ASSERT_TRUE(w.add_channel(make_double_channel("Sensor/T")).has_value());
    ASSERT_TRUE(w.start().has_value());
    ASSERT_TRUE(w.close().has_value());

    auto const wf = read_written_file(g.path);
    auto parsed = osf::parse_metablock_json(wf.metablock_json);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    ASSERT_EQ(parsed->channels.size(), 1u);
    EXPECT_EQ(parsed->channels[0].name, "Sensor/T");
    EXPECT_EQ(parsed->file_info.creator, "test:1");
}

TEST(StreamingWriterByteExact, first_bcStartData_block_layout_for_double) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.add_channel(make_double_channel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());

    double const samples[] = {1.5};
    ASSERT_TRUE(w.start_equidistant_segment(
        /*channel=*/0, /*start_ts=*/0LL, /*rate=*/100.0,
        samples, /*count=*/1).has_value());
    ASSERT_TRUE(w.close().has_value());

    auto const wf = read_written_file(g.path);
    // Single-sample bcStartData layout (bit-7 clear):
    //   [u16 ci=0][u16 len=25][0x06 ctrl][i64 ts][f64 rate][f64 sample]
    //   len = 1 + 8 + 8 + 8 = 25
    //   frame size = 2 + 2 + 25 = 29 bytes
    ASSERT_EQ(wf.block_stream.size(), 29u);
    EXPECT_EQ(wf.block_stream[0], 0x00);
    EXPECT_EQ(wf.block_stream[1], 0x00);
    EXPECT_EQ(wf.block_stream[2], 0x19);     // 25 low byte
    EXPECT_EQ(wf.block_stream[3], 0x00);
    EXPECT_EQ(wf.block_stream[4], 0x06);     // bcStartData, bit-7 clear
}

// ── Category D — Channel-state / chunking tests ──────────────────────

TEST(StreamingWriterChunking,
     long_double_append_at_sov2_produces_multiple_continued_blocks) {
    // sov=2 limits each bcContinuedData payload to 65535 bytes.
    // Body = 1 (ctrl) + 4 (N) + N * 8 (double) = 5 + 8N.
    // 5 + 8N <= 65535  →  N <= 8191. So 100k samples → 13+ blocks.
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.add_channel(make_double_channel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());

    std::vector<double> samples(100'000);
    std::iota(samples.begin(), samples.end(), 0.0);
    ASSERT_TRUE(w.start_equidistant_segment(
        0, 0LL, 1000.0, samples.data(), 1).has_value());
    ASSERT_TRUE(w.append_equidistant_samples(
        0, samples.data() + 1, samples.size() - 1).has_value());
    ASSERT_TRUE(w.close().has_value());

    // Reader sees a logical single segment because the chunking is
    // invisible — all bcContinuedData blocks belong to the same
    // bcStartData group.
    auto mgr = osf::DataManager::load_from_file(g.path);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    ASSERT_EQ(mgr->channels().size(), 1u);
    auto const& ch = mgr->channels().front();
    auto const* eq = std::get_if<osf::EquidistantChannel>(&ch);
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->segments.size(), 1u);
    auto const flat = osf::as_doubles_flat(*eq);
    ASSERT_TRUE(flat.has_value());
    EXPECT_EQ(flat->size(), 100'000u);
    EXPECT_DOUBLE_EQ((*flat)[0], 0.0);
    EXPECT_DOUBLE_EQ((*flat)[99'999], 99'999.0);
    // Block count assertion: the writer emitted more than one block.
    EXPECT_GT(mgr->stats.blocks_read, 1u);
}

TEST(StreamingWriterChunking,
     two_segments_on_same_channel_emit_two_bcStartData) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.add_channel(make_double_channel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());

    double const s1[] = {1.0, 2.0, 3.0};
    double const s2[] = {10.0, 20.0};
    ASSERT_TRUE(w.start_equidistant_segment(0, 0LL, 100.0, s1, 3).has_value());
    ASSERT_TRUE(w.start_equidistant_segment(
        0, 1'000'000'000LL, 200.0, s2, 2).has_value());
    ASSERT_TRUE(w.close().has_value());

    auto mgr = osf::DataManager::load_from_file(g.path);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    auto const* eq = std::get_if<osf::EquidistantChannel>(
        &mgr->channels().front());
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->segments.size(), 2u);
    EXPECT_EQ(eq->segments[0].start_timestamp_ns, 0);
    EXPECT_DOUBLE_EQ(eq->segments[0].sample_rate_hz, 100.0);
    EXPECT_EQ(eq->segments[1].start_timestamp_ns, 1'000'000'000LL);
    EXPECT_DOUBLE_EQ(eq->segments[1].sample_rate_hz, 200.0);
}

// ── Category B (remaining) — Pre-write errors for equidistant ────────

TEST(StreamingWriterPreWrite, append_without_start_returns_invalid_block) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.add_channel(make_double_channel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());

    double const s[] = {1.0};
    auto r = w.append_equidistant_samples(0, s, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidBlock);

    // Writer remains in Streaming — a subsequent correct call works.
    ASSERT_TRUE(w.start_equidistant_segment(0, 0LL, 100.0, s, 1).has_value());
    ASSERT_TRUE(w.append_equidistant_samples(0, s, 1).has_value());
}

TEST(StreamingWriterPreWrite,
     equidistant_rate_zero_or_nan_returns_invalid_argument) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.add_channel(make_double_channel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());

    double const s[] = {1.0};
    auto r0 = w.start_equidistant_segment(0, 0LL, 0.0, s, 1);
    ASSERT_FALSE(r0.has_value());
    EXPECT_EQ(r0.error().code, osf::Error::Code::InvalidArgument);

    auto r1 = w.start_equidistant_segment(
        0, 0LL, std::numeric_limits<double>::quiet_NaN(), s, 1);
    ASSERT_FALSE(r1.has_value());
    EXPECT_EQ(r1.error().code, osf::Error::Code::InvalidArgument);

    // Negative rate also rejected.
    auto r2 = w.start_equidistant_segment(0, 0LL, -100.0, s, 1);
    ASSERT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterPreWrite,
     equidistant_float_overload_on_double_channel_returns_datatype_mismatch) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.add_channel(make_double_channel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());

    float const s[] = {1.0f};
    auto r = w.start_equidistant_segment(0, 0LL, 100.0, s, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::DataTypeMismatch);
}

// ── Category E — Roundtrip tests ─────────────────────────────────────

TEST(StreamingWriterRoundtrip,
     equidistant_double_single_segment_round_trips) {
    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        ASSERT_TRUE(w.add_channel(make_double_channel("eq")).has_value());
        ASSERT_TRUE(w.start().has_value());
        std::vector<double> s = {1.5, 2.5, 3.5, 4.5, 5.5};
        ASSERT_TRUE(w.start_equidistant_segment(
            0, 1'000'000'000LL, 1000.0, s.data(), s.size()).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::load_from_file(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* eq = std::get_if<osf::EquidistantChannel>(
        mgr->channel("eq"));
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->segments.size(), 1u);
    EXPECT_EQ(eq->segments[0].start_timestamp_ns, 1'000'000'000LL);
    EXPECT_DOUBLE_EQ(eq->segments[0].sample_rate_hz, 1000.0);
    auto const flat = osf::as_doubles_flat(*eq);
    ASSERT_TRUE(flat.has_value());
    std::vector<double> const expected = {1.5, 2.5, 3.5, 4.5, 5.5};
    EXPECT_EQ(*flat, expected);
}

TEST(StreamingWriterRoundtrip,
     equidistant_float_multi_segment_round_trips) {
    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "fl";
        d.data_type = osf::DataType::Float;
        d.channel_type = osf::ChannelType::Equidistant;
        d.size_of_length_value = 2;
        ASSERT_TRUE(w.add_channel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        float const s1[] = {1.0f, 2.0f, 3.0f};
        float const s2[] = {10.0f, 20.0f};
        ASSERT_TRUE(w.start_equidistant_segment(
            0, 0LL, 100.0, s1, 3).has_value());
        ASSERT_TRUE(w.start_equidistant_segment(
            0, 1'000'000'000LL, 200.0, s2, 2).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::load_from_file(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* eq = std::get_if<osf::EquidistantChannel>(
        mgr->channel("fl"));
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->segments.size(), 2u);
    auto const flat = osf::as_floats_flat(*eq);
    ASSERT_TRUE(flat.has_value());
    std::vector<float> const expected = {1.0f, 2.0f, 3.0f, 10.0f, 20.0f};
    EXPECT_EQ(*flat, expected);
}
