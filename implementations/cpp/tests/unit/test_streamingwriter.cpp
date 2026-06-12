// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/streamingwriter.h"
#include "osf/binarysample.h"
#include "osf/error.h"
#include "osf/metablock.h"
#include "osf/reader.h"
#include "osf/manager.h"
#include "osf/datachannel.h"

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

std::filesystem::path makeTempPath() {
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

// Scalar (timestamped / lifecycle) double channel.
osf::ChannelDef makeDoubleScalarChannel(std::string name) {
    osf::ChannelDef d;
    d.name = std::move(name);
    d.dataType = osf::DataType::Double;
    d.channelType = osf::ChannelType::Scalar;
    d.sizeOfLengthValue = 2;
    return d;
}

// Equidistant double channel (bcStartData / bcContinuedData blocks).
osf::ChannelDef makeDoubleEquidistantChannel(std::string name) {
    osf::ChannelDef d;
    d.name = std::move(name);
    d.dataType = osf::DataType::Double;
    d.channelType = osf::ChannelType::Equidistant;
    d.sizeOfLengthValue = 2;
    return d;
}

// Decompose a written .osf file into magic line, metablock JSON,
// and remaining block-stream bytes.
struct WrittenFile {
    std::string magicLine;          // includes trailing \n
    std::string metablockJson;      // exactly metablockLen bytes
    std::vector<std::uint8_t> blockStream;
};

WrittenFile readWrittenFile(std::filesystem::path const& path) {
    WrittenFile wf;
    std::ifstream in{path, std::ios::binary};
    std::string line;
    std::getline(in, line);   // reads up to (but not including) \n
    auto const space = line.find(' ');
    auto const lenStr = line.substr(space + 1);
    auto const metablockLen =
        static_cast<std::size_t>(std::stoul(lenStr));
    wf.magicLine = line + "\n";
    std::vector<char> mb(metablockLen);
    if (metablockLen > 0) {
        in.read(mb.data(), static_cast<std::streamsize>(metablockLen));
    }
    wf.metablockJson.assign(mb.data(), metablockLen);
    wf.blockStream.assign(std::istreambuf_iterator<char>{in},
                           std::istreambuf_iterator<char>{});
    return wf;
}

}  // namespace

// ── Category A — Lifecycle / State machine ───────────────────────────

TEST(StreamingWriterLifecycle, start_fails_without_channels) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    auto r = w.start();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterLifecycle, add_channel_after_start_returns_error) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(makeDoubleScalarChannel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());
    auto r = w.addChannel(makeDoubleScalarChannel("b"));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterLifecycle, write_before_start_returns_error) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(makeDoubleScalarChannel("a")).has_value());
    auto r = w.writeTimestampedSample<double>(0, /*ts=*/0, /*v=*/1.0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterLifecycle, double_start_returns_error) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(makeDoubleScalarChannel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());
    auto r = w.start();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterLifecycle, close_in_configure_state_is_noop_success) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    auto r = w.close();
    EXPECT_TRUE(r.has_value());
    // No file was created — close() in Configure state should not open it.
    EXPECT_FALSE(std::filesystem::exists(g.path));
}

TEST(StreamingWriterLifecycle, destructor_closes_unfinished_writer_safely) {
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        ASSERT_TRUE(w.addChannel(makeDoubleScalarChannel("a")).has_value());
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
// write* path, append-without-start) arrives in Tasks 4–6 when the
// write methods land.

TEST(StreamingWriterPreWrite, add_channel_rejects_invalid_sov) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    osf::ChannelDef d;
    d.name = "a";
    d.dataType = osf::DataType::Double;
    d.channelType = osf::ChannelType::Scalar;
    d.sizeOfLengthValue = 3;   // invalid
    auto r = w.addChannel(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterPreWrite, add_channel_rejects_unsupported_data_type) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    osf::ChannelDef d;
    d.name = "a";
    d.dataType = osf::DataType::Unsupported;
    d.channelType = osf::ChannelType::Scalar;
    d.sizeOfLengthValue = 2;
    auto r = w.addChannel(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterPreWrite, add_channel_returns_sequential_indices) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    auto r0 = w.addChannel(makeDoubleScalarChannel("a"));
    auto r1 = w.addChannel(makeDoubleScalarChannel("b"));
    auto r2 = w.addChannel(makeDoubleScalarChannel("c"));
    ASSERT_TRUE(r0.has_value()); EXPECT_EQ(*r0, 0u);
    ASSERT_TRUE(r1.has_value()); EXPECT_EQ(*r1, 1u);
    ASSERT_TRUE(r2.has_value()); EXPECT_EQ(*r2, 2u);
}

// ── Category C — Byte-exact tests ────────────────────────────────────

TEST(StreamingWriterByteExact, magic_header_is_OSF5_with_metablock_length) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(makeDoubleScalarChannel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());
    ASSERT_TRUE(w.close().has_value());

    auto const wf = readWrittenFile(g.path);
    EXPECT_EQ(wf.magicLine.substr(0, 5), "OSF5 ");
    EXPECT_EQ(wf.magicLine.back(), '\n');
    EXPECT_EQ(wf.metablockJson.size(),
              static_cast<std::size_t>(std::stoul(
                  wf.magicLine.substr(5,
                                       wf.magicLine.size() - 6))));
}

TEST(StreamingWriterByteExact, metablock_json_parses_with_expected_content) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    w.setCreator("test:1");
    ASSERT_TRUE(w.addChannel(makeDoubleScalarChannel("Sensor/T")).has_value());
    ASSERT_TRUE(w.start().has_value());
    ASSERT_TRUE(w.close().has_value());

    auto const wf = readWrittenFile(g.path);
    auto parsed = osf::parseMetablockJson(wf.metablockJson);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    ASSERT_EQ(parsed->channels.size(), 1u);
    EXPECT_EQ(parsed->channels[0].name, "Sensor/T");
    EXPECT_EQ(parsed->fileInfo.creator, "test:1");
}

TEST(StreamingWriterByteExact, first_bcStartData_block_layout_for_double) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(makeDoubleEquidistantChannel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());

    double const samples[] = {1.5};
    ASSERT_TRUE(w.startEquidistantSegment(
        /*channel=*/0, /*startTs=*/0LL, /*rate=*/100.0,
        samples, /*count=*/1).has_value());
    ASSERT_TRUE(w.close().has_value());

    auto const wf = readWrittenFile(g.path);
    // Single-sample bcStartData layout (bit-7 clear):
    //   [u16 ci=0][u16 len=25][0x06 ctrl][i64 ts][f64 rate][f64 sample]
    //   len = 1 + 8 + 8 + 8 = 25
    //   frame size = 2 + 2 + 25 = 29 bytes
    ASSERT_EQ(wf.blockStream.size(), 29u);
    EXPECT_EQ(wf.blockStream[0], 0x00);
    EXPECT_EQ(wf.blockStream[1], 0x00);
    EXPECT_EQ(wf.blockStream[2], 0x19);     // 25 low byte
    EXPECT_EQ(wf.blockStream[3], 0x00);
    EXPECT_EQ(wf.blockStream[4], 0x06);     // bcStartData, bit-7 clear
}

// ── Category D — Channel-state / chunking tests ──────────────────────

TEST(StreamingWriterChunking,
     long_double_append_at_sov2_produces_multiple_continued_blocks) {
    // sov=2 limits each bcContinuedData payload to 65535 bytes.
    // Body = 1 (ctrl) + 4 (N) + N * 8 (double) = 5 + 8N.
    // 5 + 8N <= 65535  →  N <= 8191. So 100k samples → 13+ blocks.
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(makeDoubleEquidistantChannel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());

    std::vector<double> samples(100'000);
    std::iota(samples.begin(), samples.end(), 0.0);
    ASSERT_TRUE(w.startEquidistantSegment(
        0, 0LL, 1000.0, samples.data(), 1).has_value());
    ASSERT_TRUE(w.appendEquidistantSamples(
        0, samples.data() + 1, samples.size() - 1).has_value());
    ASSERT_TRUE(w.close().has_value());

    // Reader sees a logical single segment because the chunking is
    // invisible — all bcContinuedData blocks belong to the same
    // bcStartData group.
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    ASSERT_EQ(mgr->channels().size(), 1u);
    auto const& ch = mgr->channels().front();
    auto const* eq = std::get_if<osf::EquidistantChannel>(&ch);
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->segments.size(), 1u);
    auto const flat = osf::asDoublesFlat(*eq);
    ASSERT_TRUE(flat.has_value());
    EXPECT_EQ(flat->size(), 100'000u);
    EXPECT_DOUBLE_EQ((*flat)[0], 0.0);
    EXPECT_DOUBLE_EQ((*flat)[99'999], 99'999.0);
    // Block count lower bound: 1 bcStartData (8189 samples, overhead=21)
    // + ceiling(91811 / 8191) = 12 bcContinuedData = 13 total.
    EXPECT_GE(mgr->stats.blocksRead, 13u);
}

TEST(StreamingWriterChunking,
     two_segments_on_same_channel_emit_two_bcStartData) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(makeDoubleEquidistantChannel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());

    double const s1[] = {1.0, 2.0, 3.0};
    double const s2[] = {10.0, 20.0};
    ASSERT_TRUE(w.startEquidistantSegment(0, 0LL, 100.0, s1, 3).has_value());
    ASSERT_TRUE(w.startEquidistantSegment(
        0, 1'000'000'000LL, 200.0, s2, 2).has_value());
    ASSERT_TRUE(w.close().has_value());

    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    auto const* eq = std::get_if<osf::EquidistantChannel>(
        &mgr->channels().front());
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->segments.size(), 2u);
    EXPECT_EQ(eq->segments[0].startTimestampNs, 0);
    EXPECT_DOUBLE_EQ(eq->segments[0].sampleRateHz, 100.0);
    EXPECT_EQ(eq->segments[1].startTimestampNs, 1'000'000'000LL);
    EXPECT_DOUBLE_EQ(eq->segments[1].sampleRateHz, 200.0);
}

// ── Category B (remaining) — Pre-write errors for equidistant ────────

TEST(StreamingWriterPreWrite, append_without_start_returns_invalid_block) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(makeDoubleEquidistantChannel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());

    double const s[] = {1.0};
    auto r = w.appendEquidistantSamples(0, s, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidBlock);

    // Writer remains in Streaming — a subsequent correct call works.
    ASSERT_TRUE(w.startEquidistantSegment(0, 0LL, 100.0, s, 1).has_value());
    ASSERT_TRUE(w.appendEquidistantSamples(0, s, 1).has_value());
}

TEST(StreamingWriterPreWrite,
     equidistant_rate_zero_or_nan_returns_invalid_argument) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(makeDoubleEquidistantChannel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());

    double const s[] = {1.0};
    auto r0 = w.startEquidistantSegment(0, 0LL, 0.0, s, 1);
    ASSERT_FALSE(r0.has_value());
    EXPECT_EQ(r0.error().code, osf::Error::Code::InvalidArgument);

    auto r1 = w.startEquidistantSegment(
        0, 0LL, std::numeric_limits<double>::quiet_NaN(), s, 1);
    ASSERT_FALSE(r1.has_value());
    EXPECT_EQ(r1.error().code, osf::Error::Code::InvalidArgument);

    // Negative rate also rejected.
    auto r2 = w.startEquidistantSegment(0, 0LL, -100.0, s, 1);
    ASSERT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code, osf::Error::Code::InvalidArgument);
}

TEST(StreamingWriterPreWrite,
     equidistant_float_overload_on_double_channel_returns_datatype_mismatch) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(makeDoubleEquidistantChannel("a")).has_value());
    ASSERT_TRUE(w.start().has_value());

    float const s[] = {1.0f};
    auto r = w.startEquidistantSegment(0, 0LL, 100.0, s, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::DataTypeMismatch);
}

// ── Category E — Roundtrip tests ─────────────────────────────────────

TEST(StreamingWriterRoundtrip,
     equidistant_double_single_segment_round_trips) {
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        ASSERT_TRUE(w.addChannel(makeDoubleEquidistantChannel("eq")).has_value());
        ASSERT_TRUE(w.start().has_value());
        std::vector<double> s = {1.5, 2.5, 3.5, 4.5, 5.5};
        ASSERT_TRUE(w.startEquidistantSegment(
            0, 1'000'000'000LL, 1000.0, s.data(), s.size()).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* eq = std::get_if<osf::EquidistantChannel>(
        mgr->channel("eq"));
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->segments.size(), 1u);
    EXPECT_EQ(eq->segments[0].startTimestampNs, 1'000'000'000LL);
    EXPECT_DOUBLE_EQ(eq->segments[0].sampleRateHz, 1000.0);
    auto const flat = osf::asDoublesFlat(*eq);
    ASSERT_TRUE(flat.has_value());
    std::vector<double> const expected = {1.5, 2.5, 3.5, 4.5, 5.5};
    EXPECT_EQ(*flat, expected);
}

TEST(StreamingWriterRoundtrip,
     equidistant_float_multi_segment_round_trips) {
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "fl";
        d.dataType = osf::DataType::Float;
        d.channelType = osf::ChannelType::Equidistant;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        float const s1[] = {1.0f, 2.0f, 3.0f};
        float const s2[] = {10.0f, 20.0f};
        ASSERT_TRUE(w.startEquidistantSegment(
            0, 0LL, 100.0, s1, 3).has_value());
        ASSERT_TRUE(w.startEquidistantSegment(
            0, 1'000'000'000LL, 200.0, s2, 2).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* eq = std::get_if<osf::EquidistantChannel>(
        mgr->channel("fl"));
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->segments.size(), 2u);
    auto const flat = osf::asFloatsFlat(*eq);
    ASSERT_TRUE(flat.has_value());
    std::vector<float> const expected = {1.0f, 2.0f, 3.0f, 10.0f, 20.0f};
    EXPECT_EQ(*flat, expected);
}

// ── Task 5 — Timestamped numeric API ─────────────────────────────────

// Cat-C byte-exact tests

TEST(StreamingWriterByteExact,
     timestamped_int32_single_sample_bit7_clear) {
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "ts";
        d.dataType = osf::DataType::Int32;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        ASSERT_TRUE(w.writeTimestampedSample<std::int32_t>(
            0, /*ts=*/42LL, /*v=*/-1).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto const wf = readWrittenFile(g.path);
    // Single-sample bcAbsTimeStampData (bit-7 = 0, no N-prefix):
    //   [u16 ci=0][u16 len=13][0x08 ctrl][i64 ts][i32 sample]
    //   len = 1 + 8 + 4 = 13; frame = 17 bytes.
    ASSERT_EQ(wf.blockStream.size(), 17u);
    EXPECT_EQ(wf.blockStream[2], 0x0D);   // 13 low byte
    EXPECT_EQ(wf.blockStream[3], 0x00);
    EXPECT_EQ(wf.blockStream[4], 0x08);   // bcAbsTimeStampData, bit-7 clear
}

TEST(StreamingWriterByteExact,
     timestamped_double_multi_sample_bit7_set_with_uint32_N) {
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "ts";
        d.dataType = osf::DataType::Double;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        std::int64_t const tss[] = {1, 2, 3};
        double const vals[]      = {1.0, 2.0, 3.0};
        ASSERT_TRUE(w.writeTimestampedSamples<double>(
            0, tss, vals, 3).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto const wf = readWrittenFile(g.path);
    // Multi-sample bcAbsTimeStampData (bit-7 = 1, with u32 N=3):
    //   [u16 ci][u16 len=53][0x88][u32 N=3][3 * (i64 ts + f64 sample)]
    //   len = 1 + 4 + 3 * 16 = 53; frame = 57 bytes.
    ASSERT_EQ(wf.blockStream.size(), 57u);
    EXPECT_EQ(wf.blockStream[4], 0x88);                      // bit-7 set
    EXPECT_EQ(wf.blockStream[5], 0x03);                      // N=3 LE
    EXPECT_EQ(wf.blockStream[6], 0x00);
    EXPECT_EQ(wf.blockStream[7], 0x00);
    EXPECT_EQ(wf.blockStream[8], 0x00);
}

TEST(StreamingWriterByteExact, timestamped_bool_one_byte_value) {
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "ts";
        d.dataType = osf::DataType::Bool;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        ASSERT_TRUE(w.writeTimestampedSample<bool>(
            0, /*ts=*/0LL, /*v=*/true).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto const wf = readWrittenFile(g.path);
    // Single-sample bool: 1 (ctrl) + 8 (ts) + 1 (byte) = 10 = 0x0A.
    // Frame = 14. Last byte is the bool, encoded as 0x01 = true.
    ASSERT_EQ(wf.blockStream.size(), 14u);
    EXPECT_EQ(wf.blockStream[4], 0x08);   // bcAbsTimeStampData, bit-7 clear
    EXPECT_EQ(wf.blockStream[13], 0x01);  // true
}

// Cat-E size-class roundtrips

TEST(StreamingWriterRoundtrip, timestamped_bool_roundtrips) {
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "b";
        d.dataType = osf::DataType::Bool;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        std::int64_t const ts[] = {10, 20, 30};
        bool         const vs[] = {true, false, true};
        ASSERT_TRUE(w.writeTimestampedSamples<bool>(
            0, ts, vs, 3).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* tsCh = std::get_if<osf::TimestampedChannel>(
        mgr->channel("b"));
    ASSERT_NE(tsCh, nullptr);
    auto const pairs = osf::asBoolsFlat(*tsCh);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 3u);
    EXPECT_EQ((*pairs)[0].first, 10);
    EXPECT_EQ((*pairs)[0].second, true);
    EXPECT_EQ((*pairs)[1].second, false);
    EXPECT_EQ((*pairs)[2].second, true);
}

TEST(StreamingWriterRoundtrip, timestamped_uint16_roundtrips) {
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "u16";
        d.dataType = osf::DataType::UInt16;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        std::int64_t const ts[]   = {1, 2, 3, 4};
        std::uint16_t const vs[]  = {0, 1, 65534, 65535};
        ASSERT_TRUE(w.writeTimestampedSamples<std::uint16_t>(
            0, ts, vs, 4).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* tsCh = std::get_if<osf::TimestampedChannel>(
        mgr->channel("u16"));
    ASSERT_NE(tsCh, nullptr);
    auto const pairs = osf::asUint16Flat(*tsCh);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 4u);
    EXPECT_EQ((*pairs)[0].second, 0);
    EXPECT_EQ((*pairs)[3].second, 65535);
}

TEST(StreamingWriterRoundtrip,
     timestamped_int32_long_run_with_chunking_roundtrips) {
    // 100k samples at sov=2: per-block budget = (65535-5)/12 ≈ 5460
    // → at least 19 blocks. The reader joins them into a single
    // timestamped channel.
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "i32";
        d.dataType = osf::DataType::Int32;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());

        std::vector<std::int64_t> ts(100'000);
        std::vector<std::int32_t> vs(100'000);
        for (std::size_t i = 0; i < ts.size(); ++i) {
            ts[i] = 1000 + static_cast<std::int64_t>(i) * 10;
            vs[i] = static_cast<std::int32_t>(i);
        }
        ASSERT_TRUE(w.writeTimestampedSamples<std::int32_t>(
            0, ts.data(), vs.data(), ts.size()).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* tsCh = std::get_if<osf::TimestampedChannel>(
        mgr->channel("i32"));
    ASSERT_NE(tsCh, nullptr);
    auto const pairs = osf::asInt32Flat(*tsCh);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 100'000u);
    EXPECT_EQ((*pairs)[0].first, 1000);
    EXPECT_EQ((*pairs)[0].second, 0);
    EXPECT_EQ((*pairs)[99'999].first, 1000 + 99'999 * 10);
    EXPECT_EQ((*pairs)[99'999].second, 99'999);
    // Lower bound: ceil(100000 / 5460) = 19 blocks (sov=2, int32, overhead=5, per-sample=12).
    EXPECT_GE(mgr->stats.blocksRead, 19u);
}

TEST(StreamingWriterRoundtrip, timestamped_uint64_roundtrips) {
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "u64";
        d.dataType = osf::DataType::UInt64;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        std::int64_t const ts[]  = {1, 2};
        std::uint64_t const vs[] = {0xDEADBEEFCAFEBABEull,
                                    0x0123456789ABCDEFull};
        ASSERT_TRUE(w.writeTimestampedSamples<std::uint64_t>(
            0, ts, vs, 2).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* tsCh = std::get_if<osf::TimestampedChannel>(
        mgr->channel("u64"));
    ASSERT_NE(tsCh, nullptr);
    auto const pairs = osf::asUint64Flat(*tsCh);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 2u);
    EXPECT_EQ((*pairs)[0].second, 0xDEADBEEFCAFEBABEull);
    EXPECT_EQ((*pairs)[1].second, 0x0123456789ABCDEFull);
}

TEST(StreamingWriterRoundtrip,
     timestamped_double_long_run_with_chunking_roundtrips) {
    // 100k doubles at sov=2: per-block ~4093 samples → ~25 blocks.
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "d";
        d.dataType = osf::DataType::Double;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());

        std::vector<std::int64_t> ts(100'000);
        std::vector<double> vs(100'000);
        for (std::size_t i = 0; i < ts.size(); ++i) {
            ts[i] = static_cast<std::int64_t>(i);
            vs[i] = static_cast<double>(i) * 0.5;
        }
        ASSERT_TRUE(w.writeTimestampedSamples<double>(
            0, ts.data(), vs.data(), ts.size()).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* tsCh = std::get_if<osf::TimestampedChannel>(
        mgr->channel("d"));
    ASSERT_NE(tsCh, nullptr);
    auto const pairs = osf::asDoublesFlat(*tsCh);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 100'000u);
    EXPECT_DOUBLE_EQ((*pairs)[99'999].second, 99'999.0 * 0.5);
    // Lower bound: ceil(100000 / 4095) = 25 blocks (sov=2, double, overhead=5, per-sample=16).
    EXPECT_GE(mgr->stats.blocksRead, 25u);
}

// Cat-B pre-write error paths

TEST(StreamingWriterPreWrite,
     timestamped_template_T_mismatch_against_channel_returns_data_type_mismatch) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    osf::ChannelDef d;
    d.name = "f";
    d.dataType = osf::DataType::Float;
    d.channelType = osf::ChannelType::Timestamped;
    d.sizeOfLengthValue = 2;
    ASSERT_TRUE(w.addChannel(d).has_value());
    ASSERT_TRUE(w.start().has_value());
    auto r = w.writeTimestampedSample<std::int32_t>(0, 0LL, 0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::DataTypeMismatch);

    // Writer remains in Streaming — a correct call succeeds.
    EXPECT_TRUE(w.writeTimestampedSample<float>(0, 0LL, 1.0f).has_value());
}

TEST(StreamingWriterPreWrite,
     timestamped_count_zero_returns_invalid_argument) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    osf::ChannelDef d;
    d.name = "i";
    d.dataType = osf::DataType::Int32;
    d.channelType = osf::ChannelType::Timestamped;
    d.sizeOfLengthValue = 2;
    ASSERT_TRUE(w.addChannel(d).has_value());
    ASSERT_TRUE(w.start().has_value());
    std::int64_t ts = 0;
    std::int32_t v  = 0;
    auto r = w.writeTimestampedSamples<std::int32_t>(0, &ts, &v, 0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

// ── Task 6: GPS + Variable API tests ─────────────────────────────────

TEST(StreamingWriterRoundtrip, timestamped_gps_array_roundtrips) {
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "gps";
        d.dataType = osf::DataType::GpsLocation;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        std::int64_t const ts[]      = {100, 200, 300};
        osf::GpsLocation const gps[] = {
            {47.5, 9.5,  400.0},
            {47.6, 9.6,  405.0},
            {47.7, 9.7,  410.0},
        };
        ASSERT_TRUE(w.writeTimestampedGpsSamples(
            0, ts, gps, 3).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* tsCh = std::get_if<osf::TimestampedChannel>(
        mgr->channel("gps"));
    ASSERT_NE(tsCh, nullptr);
    auto const pairs = osf::asGpsFlat(*tsCh);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 3u);
    EXPECT_EQ((*pairs)[0].first, 100);
    EXPECT_DOUBLE_EQ((*pairs)[0].second.latitude,  47.5);
    EXPECT_DOUBLE_EQ((*pairs)[0].second.longitude,  9.5);
    EXPECT_DOUBLE_EQ((*pairs)[0].second.altitude, 400.0);
    EXPECT_DOUBLE_EQ((*pairs)[2].second.latitude,  47.7);
    EXPECT_DOUBLE_EQ((*pairs)[2].second.altitude, 410.0);
}

TEST(StreamingWriterRoundtrip, timestamped_gps_single_sample_via_scalar_form) {
    // The scalar form forwards to the array form with count=1;
    // verify the on-disk payload is the canonical bit-7=0 single-sample
    // form via DataManager roundtrip.
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "gps";
        d.dataType = osf::DataType::GpsLocation;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        ASSERT_TRUE(w.writeTimestampedGpsSample(
            0, /*ts=*/42LL, {48.0, 10.0, 200.0}).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* tsCh = std::get_if<osf::TimestampedChannel>(
        mgr->channel("gps"));
    ASSERT_NE(tsCh, nullptr);
    auto const pairs = osf::asGpsFlat(*tsCh);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 1u);
    EXPECT_EQ((*pairs)[0].first, 42);
    EXPECT_DOUBLE_EQ((*pairs)[0].second.latitude, 48.0);
}

TEST(StreamingWriterRoundtrip, timestamped_string_multiple_samples_roundtrip) {
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "log";
        d.dataType = osf::DataType::String;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        ASSERT_TRUE(w.writeTimestampedString(
            0, 100LL, std::string_view{"alpha"}).has_value());
        ASSERT_TRUE(w.writeTimestampedString(
            0, 200LL, std::string_view{"beta"}).has_value());
        ASSERT_TRUE(w.writeTimestampedString(
            0, 300LL, std::string_view{"gamma"}).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* var = std::get_if<osf::VariableChannel>(
        mgr->channel("log"));
    ASSERT_NE(var, nullptr);
    auto const strings = var->asStrings();
    ASSERT_TRUE(strings.has_value());
    ASSERT_EQ((*strings)->size(), 3u);
    EXPECT_EQ((**strings)[0], "alpha");
    EXPECT_EQ((**strings)[1], "beta");
    EXPECT_EQ((**strings)[2], "gamma");
    EXPECT_EQ(var->timestampsNs[0], 100);
    EXPECT_EQ(var->timestampsNs[2], 300);
}

TEST(StreamingWriterRoundtrip,
     timestamped_binary_with_sov4_large_blob_roundtrip) {
    // 70 KB blob into a sov=4 channel — fits in a single block.
    TempFileGuard g{makeTempPath()};
    std::vector<std::uint8_t> blob(70'000, 0xAB);
    blob[0]   = 0xDE;
    blob[1]   = 0xAD;
    blob.back() = 0xEF;
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "img";
        d.dataType = osf::DataType::Binary;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 4;
        d.mimeType = "image/jpeg";
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        ASSERT_TRUE(w.writeTimestampedBinary(
            0, 1234LL, osf::BinarySample::fromVector(blob)).has_value());
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* var = std::get_if<osf::VariableChannel>(
        mgr->channel("img"));
    ASSERT_NE(var, nullptr);
    auto const bins = var->asBinaries();
    ASSERT_TRUE(bins.has_value());
    ASSERT_EQ((*bins)->size(), 1u);
    EXPECT_EQ((**bins)[0], blob);
    EXPECT_EQ(var->timestampsNs[0], 1234);
}

TEST(StreamingWriterChunking,
     string_channel_emits_one_block_per_sample) {
    // 5 strings → 5 separate bcAbsTimeStampData blocks (variable
    // is single-sample only per spec). DataManager.stats.blocksRead
    // should equal the count.
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "s";
        d.dataType = osf::DataType::String;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        for (std::int64_t i = 0; i < 5; ++i) {
            ASSERT_TRUE(w.writeTimestampedString(
                0, i, std::string_view{"x"}).has_value());
        }
        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    EXPECT_EQ(mgr->stats.blocksRead, 5u);
}

TEST(StreamingWriterPreWrite,
     oversized_string_at_sov2_returns_invalid_block_with_capacity_message) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    osf::ChannelDef d;
    d.name = "s";
    d.dataType = osf::DataType::String;
    d.channelType = osf::ChannelType::Timestamped;
    d.sizeOfLengthValue = 2;
    ASSERT_TRUE(w.addChannel(d).has_value());
    ASSERT_TRUE(w.start().has_value());

    // 65527-byte string trips the sov=2 single-block payload limit.
    // Body = 1 (ctrl) + 8 (ts) + payload-bytes; max payload = 65535.
    // Effective sample max = 65526; 65527 fails.
    std::string const tooBig(65527, 'x');
    auto r = w.writeTimestampedString(
        0, 0LL, std::string_view{tooBig});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidBlock);
    // Capacity-aware error message per Spec §3.3.
    EXPECT_NE(r.error().message.find("65526 bytes"), std::string::npos)
        << "error message must quote the effective capacity 65526 bytes; "
        << "got: " << r.error().message;

    // The boundary case (65526 bytes) succeeds.
    std::string const atLimit(65526, 'x');
    ASSERT_TRUE(w.writeTimestampedString(
        0, 1LL, std::string_view{atLimit}).has_value());
}

TEST(StreamingWriterPreWrite,
     oversized_binary_at_sov2_returns_invalid_block) {
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    osf::ChannelDef d;
    d.name = "b";
    d.dataType = osf::DataType::Binary;
    d.channelType = osf::ChannelType::Timestamped;
    d.sizeOfLengthValue = 2;
    ASSERT_TRUE(w.addChannel(d).has_value());
    ASSERT_TRUE(w.start().has_value());

    std::vector<std::uint8_t> tooBig(65527, 0xCD);
    auto r = w.writeTimestampedBinary(
        0, 0LL, osf::BinarySample::fromVector(tooBig));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidBlock);
    EXPECT_NE(r.error().message.find("65526 bytes"), std::string::npos);
}

// ── Task 9 — Additional lifecycle tests ──────────────────────────────

TEST(StreamingWriterLifecycle, double_close_from_configure_is_safe) {
    // First close() from Configure: returns success (no file was opened).
    // Second close(): writer is now in Closed state; returns
    // InvalidArgument "close: writer already closed".
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    auto r1 = w.close();
    EXPECT_TRUE(r1.has_value());
    EXPECT_FALSE(std::filesystem::exists(g.path));

    auto r2 = w.close();
    ASSERT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code, osf::Error::Code::InvalidArgument);
    EXPECT_NE(r2.error().message.find("already closed"), std::string::npos);
}

TEST(StreamingWriterLifecycle,
     move_construct_streaming_writer_preserves_usability) {
    // Configure + start a writer, write one block, then move-construct
    // a second writer from it. Continue writing through the moved-to
    // instance, close it, and verify the data round-trips cleanly.
    // The moved-from instance must be safely destructible (it is in
    // the Closed state after the move).
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w1{g.path};
        ASSERT_TRUE(w1.addChannel(makeDoubleScalarChannel("mv")).has_value());
        ASSERT_TRUE(w1.start().has_value());
        ASSERT_TRUE(w1.writeTimestampedSample<double>(
            0, /*ts=*/10LL, /*v=*/1.0).has_value());

        osf::StreamingWriter w2{std::move(w1)};
        // w1 is now in Closed state — safe to let it go out of scope.
        // w2 owns the file; continue writing.
        ASSERT_TRUE(w2.writeTimestampedSample<double>(
            0, /*ts=*/20LL, /*v=*/2.0).has_value());
        ASSERT_TRUE(w2.close().has_value());
        // w1 destructor runs at end of block — must not crash.
    }

    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    auto const* tsCh = std::get_if<osf::TimestampedChannel>(
        mgr->channel("mv"));
    ASSERT_NE(tsCh, nullptr);
    auto const pairs = osf::asDoublesFlat(*tsCh);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 2u);
    EXPECT_EQ((*pairs)[0].first,  10);
    EXPECT_DOUBLE_EQ((*pairs)[0].second, 1.0);
    EXPECT_EQ((*pairs)[1].first,  20);
    EXPECT_DOUBLE_EQ((*pairs)[1].second, 2.0);
}

TEST(StreamingWriterLifecycle, self_move_assignment_is_safe) {
    // Self-move-assignment must not corrupt the writer. Use a reference
    // alias to avoid the -Wself-move compiler warning under /W4.
    TempFileGuard g{makeTempPath()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(makeDoubleScalarChannel("sm")).has_value());
    ASSERT_TRUE(w.start().has_value());
    ASSERT_TRUE(w.writeTimestampedSample<double>(
        0, /*ts=*/5LL, /*v=*/42.0).has_value());

    // Self-move via reference alias (suppresses -Wself-move).
    {
        osf::StreamingWriter& ref = w;
        w = std::move(ref);
    }

    // Writer remains usable after self-assignment.
    ASSERT_TRUE(w.writeTimestampedSample<double>(
        0, /*ts=*/6LL, /*v=*/43.0).has_value());
    ASSERT_TRUE(w.close().has_value());

    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    auto const* tsCh = std::get_if<osf::TimestampedChannel>(
        mgr->channel("sm"));
    ASSERT_NE(tsCh, nullptr);
    auto const pairs = osf::asDoublesFlat(*tsCh);
    ASSERT_TRUE(pairs.has_value());
    EXPECT_EQ(pairs->size(), 2u);
}
