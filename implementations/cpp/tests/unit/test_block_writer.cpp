// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/block_writer.hpp"
#include "osf/data_channel.hpp"
#include "osf/manager.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
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
        // val byte for sample 0 (true) is at offset 4+1+4+8 = 17 within block_stream
        // Frame layout: [len_lo][len_hi][ch_lo][ch_hi] [ctrl] [N*4LE] [ts0*8LE] [val0]
        EXPECT_EQ(bs[17], 0x01u);  // true
        // val byte for sample 1 (false) is at offset 17 + 8 + 1 = 26
        EXPECT_EQ(bs[26], 0x00u);  // false
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
        EXPECT_EQ(bs[13], 0xFFu);  // -1 as int8
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
        EXPECT_EQ(bs[13], 0xC8u);  // 200
    }
}

}  // namespace
