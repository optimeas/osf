// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/binarysample.h"
#include "osf/block.h"
#include "osf/blockwriter.h"
#include "osf/datachannel.h"
#include "osf/manager.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

osf::ChannelDef dbl(std::string name) {
    osf::ChannelDef d;
    d.name = std::move(name);
    d.data_type = osf::DataType::Double;
    d.channel_type = osf::ChannelType::Scalar;
    d.size_of_length_value = 2;
    return d;
}

TEST(BlockWriter, AddChannelReturnsIndexInOrder) {
    osf::BlockWriter w;
    auto i0 = w.add_channel(dbl("a"));
    auto i1 = w.add_channel(dbl("b"));
    ASSERT_TRUE(i0.has_value());
    ASSERT_TRUE(i1.has_value());
    EXPECT_EQ(*i0, 0u);
    EXPECT_EQ(*i1, 1u);
    EXPECT_EQ(w.channel_count(), 2u);
    EXPECT_EQ(w.channel_index("a"), std::optional<std::uint16_t>{0u});
    EXPECT_EQ(w.channel_index("missing"), std::nullopt);
}

TEST(BlockWriter, AddChannelRejectsBadSizeOfLengthValue) {
    osf::BlockWriter w;
    auto d = dbl("a");
    d.size_of_length_value = 3;
    auto r = w.add_channel(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(BlockWriter, AddChannelRejectsUnsupportedDataType) {
    osf::BlockWriter w;
    auto d = dbl("a");
    d.data_type = osf::DataType::Unsupported;
    auto r = w.add_channel(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(BlockWriter, EmptyBuilderWriteReturnsInvalidArgument) {
    osf::BlockWriter w;
    std::ostringstream out;
    auto r = w.write_to(out);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(BlockWriter, EquidistantDoubleRoundtripsThroughOstream) {
    osf::BlockWriter w;
    w.set_creator("test:1");
    auto idx = w.add_channel([] {
        osf::ChannelDef d;
        d.name = "eq";
        d.data_type = osf::DataType::Double;
        d.channel_type = osf::ChannelType::Equidistant;
        d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());

    std::vector<double> samples{1.0, 2.0, 3.0, 4.0, 5.0};
    ASSERT_TRUE(w.add_equidistant_segment(*idx, /*start_ns=*/1000,
        /*rate_hz=*/100.0, samples.data(), samples.size()).has_value());

    std::ostringstream out;
    ASSERT_TRUE(w.write_to(out).has_value());

    std::string bytes = out.str();
    std::istringstream in(bytes, std::ios::binary);
    auto mgr = osf::DataManager::load_from_stream(in);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch = mgr->channel("eq");
    ASSERT_NE(ch, nullptr);
    auto const* eq = std::get_if<osf::EquidistantChannel>(ch);
    ASSERT_NE(eq, nullptr);
    auto flat = osf::as_doubles_flat(*eq);
    ASSERT_TRUE(flat.has_value());
    ASSERT_EQ(flat->size(), 5u);
    EXPECT_DOUBLE_EQ((*flat)[0], 1.0);
    EXPECT_DOUBLE_EQ((*flat)[4], 5.0);
}

TEST(BlockWriter, AddEquidistantRejectsNonPositiveRate) {
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d; d.name = "eq"; d.data_type = osf::DataType::Double;
        d.channel_type = osf::ChannelType::Equidistant; d.size_of_length_value = 2;
        return d;
    }());
    double s = 1.0;
    auto r = w.add_equidistant_segment(*idx, 0, 0.0, &s, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

// ── Task 5: Timestamped numeric ───────────────────────────────────────

TEST(BlockWriter, TimestampedInt32Roundtrips) {
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d; d.name = "ts"; d.data_type = osf::DataType::Int32;
        d.channel_type = osf::ChannelType::Timestamped; d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());
    std::vector<std::int64_t> ts{10, 20, 30};
    std::vector<std::int32_t> v{-7, 0, 99};
    ASSERT_TRUE(w.add_timestamped_samples<std::int32_t>(*idx, ts.data(), v.data(), 3).has_value());
    std::ostringstream out;
    ASSERT_TRUE(w.write_to(out).has_value());
    std::string bytes = out.str();
    std::istringstream in(bytes, std::ios::binary);
    auto mgr = osf::DataManager::load_from_stream(in);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch = mgr->channel("ts");
    ASSERT_NE(ch, nullptr);
    auto const* ts_ch = std::get_if<osf::TimestampedChannel>(ch);
    ASSERT_NE(ts_ch, nullptr);
    // as_int32_flat returns Result<vector<pair<int64_t, int32_t>>>
    auto flat = osf::as_int32_flat(*ts_ch);
    ASSERT_TRUE(flat.has_value());
    ASSERT_EQ(flat->size(), 3u);
    EXPECT_EQ((*flat)[0].second, -7);
    EXPECT_EQ((*flat)[2].second, 99);
}

TEST(BlockWriter, TimestampedDoubleRoundtrips) {
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d; d.name = "tsd"; d.data_type = osf::DataType::Double;
        d.channel_type = osf::ChannelType::Timestamped; d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());
    std::vector<std::int64_t> ts{100, 200, 300};
    std::vector<double> v{1.5, -2.5, 3.5};
    ASSERT_TRUE(w.add_timestamped_samples<double>(*idx, ts.data(), v.data(), 3).has_value());
    std::ostringstream out;
    ASSERT_TRUE(w.write_to(out).has_value());
    std::string bytes = out.str();
    std::istringstream in(bytes, std::ios::binary);
    auto mgr = osf::DataManager::load_from_stream(in);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch = mgr->channel("tsd");
    ASSERT_NE(ch, nullptr);
    auto const* ts_ch = std::get_if<osf::TimestampedChannel>(ch);
    ASSERT_NE(ts_ch, nullptr);
    auto flat = osf::as_doubles_flat(*ts_ch);
    ASSERT_TRUE(flat.has_value());
    ASSERT_EQ(flat->size(), 3u);
    EXPECT_DOUBLE_EQ((*flat)[0].second, 1.5);
    EXPECT_DOUBLE_EQ((*flat)[2].second, 3.5);
}

TEST(BlockWriter, MixedBlockTypesRejected) {
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d; d.name = "eq"; d.data_type = osf::DataType::Double;
        d.channel_type = osf::ChannelType::Equidistant; d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());
    double s = 1.0;
    ASSERT_TRUE(w.add_equidistant_segment(*idx, 0, 100.0, &s, 1).has_value());
    // Attempt to write timestamped samples on an equidistant channel
    std::int64_t ts = 10;
    double v = 2.0;
    auto r = w.add_timestamped_samples<double>(*idx, &ts, &v, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidBlock);
}

TEST(BlockWriter, TimestampedDatatypeMismatchRejected) {
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d; d.name = "i32"; d.data_type = osf::DataType::Int32;
        d.channel_type = osf::ChannelType::Timestamped; d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());
    // Channel declared Int32, but we try to write double
    std::int64_t ts = 10;
    double v = 1.0;
    auto r = w.add_timestamped_samples<double>(*idx, &ts, &v, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::DataTypeMismatch);
}

// Helper: parse the magic header to find block-stream offset and return the
// block-stream bytes from an ostringstream output.
static std::vector<std::uint8_t> extract_block_stream(std::string const& bytes) {
    // Magic line: "OSF5 <len>\n"
    std::size_t nl = bytes.find('\n');
    std::string magic_line = bytes.substr(0, nl);
    std::size_t space = magic_line.find(' ');
    std::size_t metablock_len = std::stoull(magic_line.substr(space + 1));
    std::size_t block_start = nl + 1 + metablock_len;
    std::vector<std::uint8_t> block_stream;
    block_stream.reserve(bytes.size() - block_start);
    for (std::size_t i = block_start; i < bytes.size(); ++i) {
        block_stream.push_back(static_cast<std::uint8_t>(bytes[i]));
    }
    return block_stream;
}

TEST(BlockWriter, TimestampedBoolByteExact) {
    // bool: true should encode as 0x01, false as 0x00
    {
        osf::BlockWriter w;
        auto idx = w.add_channel([] {
            osf::ChannelDef d; d.name = "b"; d.data_type = osf::DataType::Bool;
            d.channel_type = osf::ChannelType::Timestamped; d.size_of_length_value = 2;
            return d;
        }());
        ASSERT_TRUE(idx.has_value());
        // Two samples: true, false
        std::int64_t ts[] = {0, 1};
        bool v[] = {true, false};
        ASSERT_TRUE(w.add_timestamped_samples<bool>(*idx, ts, v, 2).has_value());
        std::ostringstream out;
        ASSERT_TRUE(w.write_to(out).has_value());
        auto bs = extract_block_stream(out.str());
        // Multi-sample: ctrl(1) + N(4) + [ts(8)+val(1)] * 2 = 1+4+18 = 23
        // Frame: sov(2) + channel(2) + payload = 2+2+23 = 27
        ASSERT_EQ(bs.size(), 27u);
        // Frame layout: [u16 len][u16 channel][u8 ctrl][u32 N][N × (i64 ts + 1-byte value)]
        constexpr std::size_t kHeader    = 2 + 2 + 1 + 4;  // len + channel + ctrl + N
        constexpr std::size_t kPerSample = 8 + 1;           // i64 ts + value byte
        // value byte of sample i is at: kHeader + i*kPerSample + 8
        EXPECT_EQ(bs[kHeader + 0 * kPerSample + 8], 0x01u);  // true  (== bs[17])
        EXPECT_EQ(bs[kHeader + 1 * kPerSample + 8], 0x00u);  // false (== bs[26])
    }

    // int8_t: -1 should encode as 0xFF
    {
        osf::BlockWriter w;
        auto idx = w.add_channel([] {
            osf::ChannelDef d; d.name = "i8"; d.data_type = osf::DataType::Int8;
            d.channel_type = osf::ChannelType::Timestamped; d.size_of_length_value = 2;
            return d;
        }());
        ASSERT_TRUE(idx.has_value());
        std::int64_t ts = 0;
        std::int8_t v = -1;
        ASSERT_TRUE(w.add_timestamped_sample<std::int8_t>(*idx, ts, v).has_value());
        std::ostringstream out;
        ASSERT_TRUE(w.write_to(out).has_value());
        auto bs = extract_block_stream(out.str());
        // Single-sample: ctrl(1)+ts(8)+val(1)=10; frame=14
        ASSERT_EQ(bs.size(), 14u);
        // Single-sample frame layout: [u16 len][u16 channel][u8 ctrl][i64 ts][value]
        // (no N field — multi-sample bit is clear)
        constexpr std::size_t kSingleVal = 2 + 2 + 1 + 8;  // len + channel + ctrl + ts (== 13)
        EXPECT_EQ(bs[kSingleVal], 0xFFu);  // -1 as int8 (== bs[13])
    }

    // uint8_t: 200 should encode as 0xC8
    {
        osf::BlockWriter w;
        auto idx = w.add_channel([] {
            osf::ChannelDef d; d.name = "u8"; d.data_type = osf::DataType::UInt8;
            d.channel_type = osf::ChannelType::Timestamped; d.size_of_length_value = 2;
            return d;
        }());
        ASSERT_TRUE(idx.has_value());
        std::int64_t ts = 0;
        std::uint8_t v = 200;
        ASSERT_TRUE(w.add_timestamped_sample<std::uint8_t>(*idx, ts, v).has_value());
        std::ostringstream out;
        ASSERT_TRUE(w.write_to(out).has_value());
        auto bs = extract_block_stream(out.str());
        // Single-sample: ctrl(1)+ts(8)+val(1)=10; frame=14
        ASSERT_EQ(bs.size(), 14u);
        // Single-sample frame layout: [u16 len][u16 channel][u8 ctrl][i64 ts][value]
        // (no N field — multi-sample bit is clear)
        constexpr std::size_t kSingleVal2 = 2 + 2 + 1 + 8;  // len + channel + ctrl + ts (== 13)
        EXPECT_EQ(bs[kSingleVal2], 0xC8u);  // 200 (== bs[13])
    }
}

// ── Task 6: GPS timestamped ───────────────────────────────────────────

TEST(BlockWriter, GpsRoundtrips) {
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d; d.name = "gps"; d.data_type = osf::DataType::GpsLocation;
        d.channel_type = osf::ChannelType::Timestamped; d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());

    std::vector<std::int64_t> ts{1000, 2000};
    std::vector<osf::GpsLocation> gps{
        {47.1, 8.2, 450.0},
        {47.2, 8.3, 451.5}
    };
    ASSERT_TRUE(w.add_timestamped_gps_samples(*idx, ts.data(), gps.data(), gps.size()).has_value());

    std::ostringstream out;
    ASSERT_TRUE(w.write_to(out).has_value());

    std::string bytes = out.str();
    std::istringstream in(bytes, std::ios::binary);
    auto mgr = osf::DataManager::load_from_stream(in);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch = mgr->channel("gps");
    ASSERT_NE(ch, nullptr);
    auto const* ts_ch = std::get_if<osf::TimestampedChannel>(ch);
    ASSERT_NE(ts_ch, nullptr);
    auto flat = osf::as_gps_flat(*ts_ch);
    ASSERT_TRUE(flat.has_value());
    ASSERT_EQ(flat->size(), 2u);
    EXPECT_DOUBLE_EQ((*flat)[0].second.latitude,  47.1);
    EXPECT_DOUBLE_EQ((*flat)[0].second.longitude, 8.2);
    EXPECT_DOUBLE_EQ((*flat)[0].second.altitude,  450.0);
    EXPECT_DOUBLE_EQ((*flat)[1].second.latitude,  47.2);
    EXPECT_DOUBLE_EQ((*flat)[1].second.altitude,  451.5);
}

// ── Task 6: String variable ───────────────────────────────────────────

TEST(BlockWriter, StringRoundtrips) {
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d; d.name = "str"; d.data_type = osf::DataType::String;
        d.channel_type = osf::ChannelType::Scalar; d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());

    std::vector<std::int64_t> ts{10, 20};
    std::vector<std::string_view> svs{"alpha", "bravo"};
    ASSERT_TRUE(w.add_string_samples(*idx, ts.data(), svs.data(), svs.size()).has_value());

    std::ostringstream out;
    ASSERT_TRUE(w.write_to(out).has_value());

    std::string bytes = out.str();
    std::istringstream in(bytes, std::ios::binary);
    auto mgr = osf::DataManager::load_from_stream(in);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch = mgr->channel("str");
    ASSERT_NE(ch, nullptr);
    auto const* var_ch = std::get_if<osf::VariableChannel>(ch);
    ASSERT_NE(var_ch, nullptr);
    auto strs = var_ch->as_strings();
    ASSERT_TRUE(strs.has_value());
    ASSERT_EQ((*strs)->size(), 2u);
    EXPECT_EQ((**strs)[0], "alpha");
    EXPECT_EQ((**strs)[1], "bravo");
}

// ── Task 6: Binary variable ───────────────────────────────────────────

TEST(BlockWriter, BinaryRoundtrips) {
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d; d.name = "bin"; d.data_type = osf::DataType::Binary;
        d.channel_type = osf::ChannelType::Scalar; d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());

    std::vector<std::uint8_t> v0{0x01, 0x02, 0x03};
    std::vector<std::uint8_t> v1{0xAA, 0xBB};
    std::vector<std::int64_t> ts{100, 200};
    std::vector<osf::BinarySample> bins{
        osf::BinarySample::from_vector(v0),
        osf::BinarySample::from_vector(v1)
    };
    ASSERT_TRUE(w.add_binary_samples(*idx, ts.data(), bins.data(), bins.size()).has_value());

    std::ostringstream out;
    ASSERT_TRUE(w.write_to(out).has_value());

    std::string bytes = out.str();
    std::istringstream in(bytes, std::ios::binary);
    auto mgr = osf::DataManager::load_from_stream(in);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch = mgr->channel("bin");
    ASSERT_NE(ch, nullptr);
    auto const* var_ch = std::get_if<osf::VariableChannel>(ch);
    ASSERT_NE(var_ch, nullptr);
    auto bins_out = var_ch->as_binaries();
    ASSERT_TRUE(bins_out.has_value());
    ASSERT_EQ((*bins_out)->size(), 2u);
    EXPECT_EQ((**bins_out)[0], v0);
    EXPECT_EQ((**bins_out)[1], v1);
}

// ── Task 6: Variable auto-bump sov 2->4 ──────────────────────────────

TEST(BlockWriter, VariableAutoBumpsSovWhenSampleExceedsU16) {
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d; d.name = "big"; d.data_type = osf::DataType::String;
        d.channel_type = osf::ChannelType::Scalar; d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());

    // 70000-byte string exceeds the u16 length field capacity (max payload 65535)
    std::string big(70000, 'X');
    std::string_view sv{big};
    std::int64_t ts0 = 1000;
    ASSERT_TRUE(w.add_string_sample(*idx, ts0, sv).has_value());

    std::ostringstream out;
    ASSERT_TRUE(w.write_to(out).has_value());

    std::string bytes = out.str();
    std::istringstream in(bytes, std::ios::binary);
    auto mgr = osf::DataManager::load_from_stream(in);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch = mgr->channel("big");
    ASSERT_NE(ch, nullptr);
    // sov must have been bumped to 4
    EXPECT_EQ(osf::channel_meta(*ch).size_of_length_value, 4u);
}

// ── Task 6: Binary boundary — exactly at sov=2 capacity (65526) ──────

TEST(BlockWriter, BinaryBoundary65526SucceedsAtSov2) {
    // max_payload_for_sov(2) == 65535; VARIABLE_BLOCK_OVERHEAD_BYTES == 9
    // => capacity == 65526 bytes; at exactly capacity no bump is needed.
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d; d.name = "boundary"; d.data_type = osf::DataType::Binary;
        d.channel_type = osf::ChannelType::Scalar; d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());

    std::vector<std::uint8_t> payload(65526, 0xAB);
    osf::BinarySample bs = osf::BinarySample::from_vector(payload);
    std::int64_t ts0 = 500;
    ASSERT_TRUE(w.add_binary_sample(*idx, ts0, bs).has_value());

    std::ostringstream out;
    ASSERT_TRUE(w.write_to(out).has_value());

    std::string bytes = out.str();
    std::istringstream in(bytes, std::ios::binary);
    auto mgr = osf::DataManager::load_from_stream(in);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch = mgr->channel("boundary");
    ASSERT_NE(ch, nullptr);
    // sov must stay at 2 (no bump needed at exactly capacity)
    EXPECT_EQ(osf::channel_meta(*ch).size_of_length_value, 2u);
    auto const* var_ch = std::get_if<osf::VariableChannel>(ch);
    ASSERT_NE(var_ch, nullptr);
    auto bins_out = var_ch->as_binaries();
    ASSERT_TRUE(bins_out.has_value());
    ASSERT_EQ((*bins_out)->size(), 1u);
    EXPECT_EQ((**bins_out)[0].size(), 65526u);
}

// ── Task 6: String datatype mismatch rejected ─────────────────────────

TEST(BlockWriter, StringDatatypeMismatchRejected) {
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d; d.name = "i32"; d.data_type = osf::DataType::Int32;
        d.channel_type = osf::ChannelType::Timestamped; d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());
    std::string_view sv{"hello"};
    std::int64_t ts0 = 10;
    auto r = w.add_string_sample(*idx, ts0, sv);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::DataTypeMismatch);
}

// ── Task 7: from_manager round-trip ──────────────────────────────────

/// Helper: build a BlockWriter source with one equidistant double channel,
/// one timestamped int32 channel, and one string channel; emit it to a
/// DataManager.
static osf::DataManager make_mixed_manager() {
    osf::BlockWriter src;
    src.set_creator("task7-test");

    // Equidistant double channel
    auto eq_idx = src.add_channel([] {
        osf::ChannelDef d;
        d.name = "eq_dbl";
        d.data_type = osf::DataType::Double;
        d.channel_type = osf::ChannelType::Equidistant;
        d.size_of_length_value = 2;
        d.physical_unit = "m/s";
        return d;
    }());
    EXPECT_TRUE(eq_idx.has_value());
    std::vector<double> eq_vals{1.0, 2.0, 3.0};
    EXPECT_TRUE(src.add_equidistant_segment(*eq_idx, 1000, 100.0,
        eq_vals.data(), eq_vals.size()).has_value());

    // Timestamped int32 channel
    auto ts_idx = src.add_channel([] {
        osf::ChannelDef d;
        d.name = "ts_i32";
        d.data_type = osf::DataType::Int32;
        d.channel_type = osf::ChannelType::Timestamped;
        d.size_of_length_value = 2;
        return d;
    }());
    EXPECT_TRUE(ts_idx.has_value());
    std::vector<std::int64_t> ts{10, 20, 30, 40};
    std::vector<std::int32_t> v{-1, 0, 42, 100};
    EXPECT_TRUE(src.add_timestamped_samples<std::int32_t>(*ts_idx, ts.data(), v.data(), 4).has_value());

    // String variable channel
    auto str_idx = src.add_channel([] {
        osf::ChannelDef d;
        d.name = "str_chan";
        d.data_type = osf::DataType::String;
        d.channel_type = osf::ChannelType::Scalar;
        d.size_of_length_value = 2;
        return d;
    }());
    EXPECT_TRUE(str_idx.has_value());
    std::vector<std::int64_t> str_ts{100, 200};
    std::vector<std::string_view> svs{"hello", "world"};
    EXPECT_TRUE(src.add_string_samples(*str_idx, str_ts.data(), svs.data(), svs.size()).has_value());

    std::ostringstream oss;
    auto wr = src.write_to(oss);
    EXPECT_TRUE(wr.has_value());

    std::string bytes = oss.str();
    std::istringstream in(bytes, std::ios::binary);
    auto mgr = osf::DataManager::load_from_stream(in);
    EXPECT_TRUE(mgr.has_value());
    return std::move(*mgr);
}

TEST(BlockWriter, FromManagerRoundtripsMixedChannels) {
    auto mgr = make_mixed_manager();

    // Round-trip through from_manager
    auto bw = osf::BlockWriter::from_manager(mgr);
    ASSERT_TRUE(bw.has_value());

    std::ostringstream oss2;
    ASSERT_TRUE(bw->write_to(oss2).has_value());

    std::string bytes2 = oss2.str();
    std::istringstream in2(bytes2, std::ios::binary);
    auto mgr2 = osf::DataManager::load_from_stream(in2);
    ASSERT_TRUE(mgr2.has_value());

    // Same number of channels
    ASSERT_EQ(mgr2->channels().size(), 3u);

    // Equidistant double channel
    auto const* eq_ch = mgr2->channel("eq_dbl");
    ASSERT_NE(eq_ch, nullptr);
    EXPECT_EQ(osf::channel_data_type(*eq_ch), osf::DataType::Double);
    auto const* eq = std::get_if<osf::EquidistantChannel>(eq_ch);
    ASSERT_NE(eq, nullptr);
    ASSERT_EQ(eq->segments.size(), 1u);
    EXPECT_EQ(eq->segments[0].sample_count, 3u);
    auto flat = osf::as_doubles_flat(*eq);
    ASSERT_TRUE(flat.has_value());
    ASSERT_EQ(flat->size(), 3u);
    EXPECT_DOUBLE_EQ((*flat)[0], 1.0);
    EXPECT_DOUBLE_EQ((*flat)[2], 3.0);

    // Timestamped int32 channel
    auto const* ts_ch = mgr2->channel("ts_i32");
    ASSERT_NE(ts_ch, nullptr);
    EXPECT_EQ(osf::channel_data_type(*ts_ch), osf::DataType::Int32);
    auto const* ts = std::get_if<osf::TimestampedChannel>(ts_ch);
    ASSERT_NE(ts, nullptr);
    EXPECT_EQ(ts->timestamps_ns.size(), 4u);
    auto flat32 = osf::as_int32_flat(*ts);
    ASSERT_TRUE(flat32.has_value());
    ASSERT_EQ(flat32->size(), 4u);
    EXPECT_EQ((*flat32)[0].second, -1);
    EXPECT_EQ((*flat32)[3].second, 100);

    // String variable channel
    auto const* str_ch = mgr2->channel("str_chan");
    ASSERT_NE(str_ch, nullptr);
    EXPECT_EQ(osf::channel_data_type(*str_ch), osf::DataType::String);
    auto const* var = std::get_if<osf::VariableChannel>(str_ch);
    ASSERT_NE(var, nullptr);
    auto strs = var->as_strings();
    ASSERT_TRUE(strs.has_value());
    ASSERT_EQ((*strs)->size(), 2u);
    EXPECT_EQ((**strs)[0], "hello");
    EXPECT_EQ((**strs)[1], "world");
}

TEST(BlockWriter, FreeWriteToRoundtrips) {
    auto mgr = make_mixed_manager();

    std::ostringstream oss;
    auto r = osf::write_to(mgr, oss);
    ASSERT_TRUE(r.has_value());

    std::string bytes = oss.str();
    std::istringstream in(bytes, std::ios::binary);
    auto mgr2 = osf::DataManager::load_from_stream(in);
    ASSERT_TRUE(mgr2.has_value());

    EXPECT_EQ(mgr2->channels().size(), 3u);
    EXPECT_NE(mgr2->channel("eq_dbl"),  nullptr);
    EXPECT_NE(mgr2->channel("ts_i32"),  nullptr);
    EXPECT_NE(mgr2->channel("str_chan"), nullptr);
    EXPECT_EQ(osf::channel_data_type(*mgr2->channel("eq_dbl")),  osf::DataType::Double);
    EXPECT_EQ(osf::channel_data_type(*mgr2->channel("ts_i32")),  osf::DataType::Int32);
    EXPECT_EQ(osf::channel_data_type(*mgr2->channel("str_chan")), osf::DataType::String);
    EXPECT_EQ(osf::channel_sample_count(*mgr2->channel("eq_dbl")),  3u);
    EXPECT_EQ(osf::channel_sample_count(*mgr2->channel("ts_i32")),  4u);
    EXPECT_EQ(osf::channel_sample_count(*mgr2->channel("str_chan")), 2u);
}

// ── Task 8 Step 5: multi-segment from_manager ────────────────────────

/// Build a BlockWriter with a double equidistant channel, call
/// add_equidistant_segment TWICE (two distinct segments with different
/// start timestamps), emit, reload, from_manager, re-emit, reload again,
/// and assert the equidistant channel in mgr2 has 5 total samples with
/// both original start timestamps preserved.
TEST(BlockWriter, FromManagerPreservesMultipleEquidistantSegments) {
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d;
        d.name = "eq_multi";
        d.data_type = osf::DataType::Double;
        d.channel_type = osf::ChannelType::Equidistant;
        d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());

    // Segment 1: start=0 ns, rate=100 Hz, 3 samples {1, 2, 3}
    std::vector<double> seg1{1.0, 2.0, 3.0};
    ASSERT_TRUE(w.add_equidistant_segment(
        *idx, /*start_ns=*/0, /*rate_hz=*/100.0,
        seg1.data(), seg1.size()).has_value());

    // Segment 2: start=1 000 000 000 ns (= 1 s), rate=100 Hz, 2 samples {10, 20}
    std::vector<double> seg2{10.0, 20.0};
    ASSERT_TRUE(w.add_equidistant_segment(
        *idx, /*start_ns=*/1'000'000'000LL, /*rate_hz=*/100.0,
        seg2.data(), seg2.size()).has_value());

    // Emit first time.
    std::ostringstream oss1;
    ASSERT_TRUE(w.write_to(oss1).has_value());

    // Reload → mgr1.
    std::istringstream in1(oss1.str(), std::ios::binary);
    auto mgr1 = osf::DataManager::load_from_stream(in1);
    ASSERT_TRUE(mgr1.has_value());

    // from_manager → re-emit → mgr2.
    auto bw2 = osf::BlockWriter::from_manager(*mgr1);
    ASSERT_TRUE(bw2.has_value());

    std::ostringstream oss2;
    ASSERT_TRUE(bw2->write_to(oss2).has_value());

    std::istringstream in2(oss2.str(), std::ios::binary);
    auto mgr2 = osf::DataManager::load_from_stream(in2);
    ASSERT_TRUE(mgr2.has_value());

    auto const* ch = mgr2->channel("eq_multi");
    ASSERT_NE(ch, nullptr);
    auto const* eq = std::get_if<osf::EquidistantChannel>(ch);
    ASSERT_NE(eq, nullptr);

    // 5 total samples across both segments.
    EXPECT_EQ(osf::channel_sample_count(*ch), 5u);

    // Two segments preserved.
    ASSERT_EQ(eq->segments.size(), 2u);

    // Segment start timestamps preserved.
    EXPECT_EQ(eq->segments[0].start_timestamp_ns, 0LL);
    EXPECT_EQ(eq->segments[1].start_timestamp_ns, 1'000'000'000LL);

    // Sample counts preserved.
    EXPECT_EQ(eq->segments[0].sample_count, 3u);
    EXPECT_EQ(eq->segments[1].sample_count, 2u);

    // Spot-check values.
    auto flat = osf::as_doubles_flat(*eq);
    ASSERT_TRUE(flat.has_value());
    ASSERT_EQ(flat->size(), 5u);
    EXPECT_DOUBLE_EQ((*flat)[0], 1.0);
    EXPECT_DOUBLE_EQ((*flat)[2], 3.0);
    EXPECT_DOUBLE_EQ((*flat)[3], 10.0);
    EXPECT_DOUBLE_EQ((*flat)[4], 20.0);
}

}  // namespace
