// SPDX-License-Identifier: MIT
//
// Unit tests for osf::BlockReader against synthetic byte sequences.
//
// Direct port of implementations/rust/osf-core/src/reader.rs tests
// covering the same code paths (truncation, unknown channel index,
// Unsupported channel skipping, capture-skipped opt-in, deprecated /
// reserved control bytes, every supported control byte's decode,
// trailer consumption).

#include <gtest/gtest.h>

#include <osf/osf.hpp>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------
// Builders for synthetic MetaBlocks and byte streams.
// ---------------------------------------------------------------------

osf::Channel make_channel(std::uint16_t index, osf::DataType dt,
                          std::uint8_t size_of_length_value,
                          osf::ChannelType ct = osf::ChannelType::Scalar) {
    osf::Channel ch;
    ch.index = index;
    ch.name = "ch" + std::to_string(index);
    ch.channel_type = ct;
    ch.data_type = dt;
    ch.size_of_length_value = size_of_length_value;
    return ch;
}

osf::MetaBlock make_meta(std::vector<osf::Channel> channels) {
    osf::MetaBlock mb;
    mb.file_info.version = 5;
    mb.channels = std::move(channels);
    return mb;
}

osf::MetaBlock make_meta_v4(std::vector<osf::Channel> channels) {
    osf::MetaBlock mb;
    mb.file_info.version = 4;
    mb.channels = std::move(channels);
    return mb;
}

void put_u16(std::vector<std::uint8_t>& dst, std::uint16_t v) {
    dst.push_back(static_cast<std::uint8_t>( v        & 0xFF));
    dst.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFF));
}

void put_u32(std::vector<std::uint8_t>& dst, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        dst.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

void put_i16(std::vector<std::uint8_t>& dst, std::int16_t v) {
    put_u16(dst, static_cast<std::uint16_t>(v));
}

void put_i64(std::vector<std::uint8_t>& dst, std::int64_t v) {
    auto const u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i) {
        dst.push_back(static_cast<std::uint8_t>((u >> (8 * i)) & 0xFF));
    }
}

void put_f64(std::vector<std::uint8_t>& dst, double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        dst.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF));
    }
}

void put_f32(std::vector<std::uint8_t>& dst, float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 4; ++i) {
        dst.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF));
    }
}

// Stream wrapper holding the bytes so a fresh stringstream isn't lost
// between iterations.
class ByteStream {
public:
    explicit ByteStream(std::vector<std::uint8_t> bytes)
        : stream_(std::string{reinterpret_cast<char const*>(bytes.data()),
                              bytes.size()},
                  std::ios::in | std::ios::binary) {}
    std::istream& get() { return stream_; }
private:
    std::stringstream stream_;
};

// ---------------------------------------------------------------------
// Truncation paths
// ---------------------------------------------------------------------

TEST(BlockReader, empty_stream_yields_nullopt_without_error) {
    auto meta = make_meta({});
    ByteStream s({});
    osf::BlockReader r(s.get(), meta);
    EXPECT_FALSE(r.next().has_value());
    EXPECT_EQ(r.blocks_truncated(), 0u);
}

TEST(BlockReader, truncation_in_channel_index_is_silent_eof) {
    auto meta = make_meta({make_channel(0, osf::DataType::Int16, 2)});
    ByteStream s({0x00});  // single byte; can't form u16
    osf::BlockReader r(s.get(), meta);
    EXPECT_FALSE(r.next().has_value());
    EXPECT_EQ(r.blocks_truncated(), 0u);
}

TEST(BlockReader, truncation_in_length_field_bumps_counter) {
    auto meta = make_meta({make_channel(0, osf::DataType::Int16, 4)});
    // channel 0, then only 2 of 4 length bytes.
    ByteStream s({0x00, 0x00, 0x01, 0x00});
    osf::BlockReader r(s.get(), meta);
    EXPECT_FALSE(r.next().has_value());
    EXPECT_EQ(r.blocks_truncated(), 1u);
}

TEST(BlockReader, truncation_mid_payload_bumps_counter) {
    auto meta = make_meta({make_channel(0, osf::DataType::Int16, 2)});
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);   // channel
    put_u16(bytes, 10);  // length = 10 but only 5 bytes follow
    bytes.push_back(0x08);
    bytes.insert(bytes.end(), {0, 0, 0, 0});
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    EXPECT_FALSE(r.next().has_value());
    EXPECT_EQ(r.blocks_truncated(), 1u);
}

// ---------------------------------------------------------------------
// Hard errors
// ---------------------------------------------------------------------

TEST(BlockReader, unknown_channel_index_is_hard_error) {
    auto meta = make_meta({make_channel(0, osf::DataType::Int16, 2)});
    // channel 7 is not in the metablock.
    std::vector<std::uint8_t> bytes = {7, 0, 1, 0, 0};
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto out = r.next();
    ASSERT_TRUE(out.has_value());
    ASSERT_FALSE(out->has_value());
    EXPECT_EQ(out->error().code, osf::Error::Code::UnknownChannelIndex);
}

// ---------------------------------------------------------------------
// Unsupported-channel skipping
// ---------------------------------------------------------------------

TEST(BlockReader, unsupported_data_type_yields_skipped_and_keeps_stream_aligned) {
    auto meta = make_meta({make_channel(0, osf::DataType::Unsupported, 2)});
    std::vector<std::uint8_t> bytes;
    // Two back-to-back blocks; the first must be skipped, the second
    // must be reachable.
    put_u16(bytes, 0);
    put_u16(bytes, 5);
    bytes.insert(bytes.end(), {0xAA, 1, 2, 3, 4});
    put_u16(bytes, 0);
    put_u16(bytes, 5);
    bytes.insert(bytes.end(), {0xAA, 5, 6, 7, 8});
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);

    auto first = r.next();
    ASSERT_TRUE(first && first->has_value());
    auto const& blk1 = **first;
    EXPECT_EQ(blk1.channel_index, 0u);
    auto const& sk1 = std::get<osf::Skipped>(blk1.kind);
    EXPECT_EQ(sk1.reason.kind, osf::SkipReason::Kind::UnsupportedDataType);
    EXPECT_EQ(sk1.bytes_skipped, 5u);
    EXPECT_FALSE(sk1.payload.has_value());  // default capture is off

    auto second = r.next();
    ASSERT_TRUE(second && second->has_value());
    EXPECT_EQ((*second)->channel_index, 0u);  // stream still aligned
}

TEST(BlockReader, capture_skipped_payload_keeps_body_bytes) {
    auto meta = make_meta({make_channel(0, osf::DataType::Int16, 2)});
    // Reserved control byte 0 → Skipped; body is 2 bytes after the
    // control byte.
    std::vector<std::uint8_t> bytes = {0, 0, 3, 0, 0x00, 0xAA, 0xBB};
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    r.with_capture_skipped_payload(true);

    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& sk = std::get<osf::Skipped>((*blk_r)->kind);
    EXPECT_EQ(sk.reason.kind, osf::SkipReason::Kind::ReservedBlockType);
    EXPECT_EQ(sk.reason.raw_byte, 0u);
    EXPECT_EQ(sk.bytes_skipped, 3u);
    ASSERT_TRUE(sk.payload.has_value());
    EXPECT_EQ(*sk.payload, (std::vector<std::uint8_t>{0xAA, 0xBB}));
}

// ---------------------------------------------------------------------
// Deprecated / reserved control bytes
// ---------------------------------------------------------------------

TEST(BlockReader, deprecated_control_bytes_route_to_deprecated_skip_reason) {
    auto meta = make_meta({make_channel(0, osf::DataType::Int16, 2)});
    for (std::uint8_t raw : {std::uint8_t{1}, std::uint8_t{3}, std::uint8_t{4}}) {
        std::vector<std::uint8_t> bytes = {0, 0, 1, 0, raw};
        ByteStream s(std::move(bytes));
        osf::BlockReader r(s.get(), meta);
        auto blk_r = r.next();
        ASSERT_TRUE(blk_r && blk_r->has_value());
        auto const& sk = std::get<osf::Skipped>((*blk_r)->kind);
        EXPECT_EQ(sk.reason.kind, osf::SkipReason::Kind::DeprecatedBlockType)
            << "raw byte " << int{raw};
        EXPECT_EQ(sk.reason.raw_byte, raw) << "raw byte " << int{raw};
    }
}

TEST(BlockReader, unknown_high_control_byte_routes_to_reserved_skip_reason) {
    auto meta = make_meta({make_channel(0, osf::DataType::Int16, 2)});
    std::vector<std::uint8_t> bytes = {0, 0, 1, 0, 0x55};
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& sk = std::get<osf::Skipped>((*blk_r)->kind);
    EXPECT_EQ(sk.reason.kind, osf::SkipReason::Kind::ReservedBlockType);
    EXPECT_EQ(sk.reason.raw_byte, 0x55);
}

// ---------------------------------------------------------------------
// Typed parsers — happy-path coverage
// ---------------------------------------------------------------------

TEST(BlockReader, parses_single_sample_abs_timestamp_int64) {
    auto meta = make_meta({make_channel(0, osf::DataType::Int64, 2)});
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);
    put_u16(bytes, 17);  // 1 ctl + 8 ts + 8 i64
    bytes.push_back(0x08);
    put_i64(bytes, 0x18AC'BBA9'5F76'EC57LL);
    put_i64(bytes, 0);
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blk_r)->kind);
    auto const& v = std::get<std::vector<std::pair<std::int64_t, std::int64_t>>>(
        ad.samples);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].first,  0x18AC'BBA9'5F76'EC57LL);
    EXPECT_EQ(v[0].second, 0);
}

TEST(BlockReader, parses_multi_sample_abs_timestamp_double) {
    auto meta = make_meta({make_channel(0, osf::DataType::Double, 2)});
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);
    std::uint16_t const payload_len = 1 + 4 + 2 * (8 + 8);
    put_u16(bytes, payload_len);
    bytes.push_back(0x88);   // multi
    put_u32(bytes, 2);
    put_i64(bytes, 100);
    put_f64(bytes, 1.5);
    put_i64(bytes, 200);
    put_f64(bytes, 2.5);
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blk_r)->kind);
    auto const& v = std::get<std::vector<std::pair<std::int64_t, double>>>(
        ad.samples);
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].first, 100); EXPECT_DOUBLE_EQ(v[0].second, 1.5);
    EXPECT_EQ(v[1].first, 200); EXPECT_DOUBLE_EQ(v[1].second, 2.5);
}

TEST(BlockReader, parses_start_data_single_sample_double) {
    auto meta = make_meta({make_channel(0, osf::DataType::Double, 2)});
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);
    put_u16(bytes, 25);  // 1 ctl + 8 ts + 8 rate + 8 value
    bytes.push_back(0x06);
    put_i64(bytes, 1'000'000);
    put_f64(bytes, 1000.0);
    put_f64(bytes, 2.5);
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& sd = std::get<osf::StartData>((*blk_r)->kind);
    EXPECT_EQ(sd.start_timestamp_ns, 1'000'000);
    EXPECT_NEAR(sd.sample_rate_hz, 1000.0, 1e-9);
    auto const& v = std::get<std::vector<double>>(sd.samples);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_DOUBLE_EQ(v[0], 2.5);
}

TEST(BlockReader, parses_start_data_multi_sample_float_n10) {
    auto meta = make_meta({make_channel(0, osf::DataType::Float, 2)});
    std::uint32_t const n = 10;
    std::uint16_t const payload_len =
        static_cast<std::uint16_t>(1 + 8 + 8 + 4 + n * 4);
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);
    put_u16(bytes, payload_len);
    bytes.push_back(0x86);     // multi
    put_i64(bytes, 7);
    put_f64(bytes, 500.0);
    put_u32(bytes, n);
    for (std::uint32_t i = 0; i < n; ++i) put_f32(bytes, static_cast<float>(i));
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& sd = std::get<osf::StartData>((*blk_r)->kind);
    auto const& v = std::get<std::vector<float>>(sd.samples);
    ASSERT_EQ(v.size(), n);
    EXPECT_FLOAT_EQ(v[3], 3.0f);
}

TEST(BlockReader, parses_continued_data_int16_multi) {
    auto meta = make_meta({make_channel(0, osf::DataType::Int16, 2)});
    std::uint32_t const n = 4;
    std::uint16_t const payload_len =
        static_cast<std::uint16_t>(1 + 4 + n * 2);
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);
    put_u16(bytes, payload_len);
    bytes.push_back(0x85);  // multi
    put_u32(bytes, n);
    for (std::uint32_t i = 0; i < n; ++i)
        put_i16(bytes, static_cast<std::int16_t>(10 * i));
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& cd = std::get<osf::ContinuedData>((*blk_r)->kind);
    auto const& v = std::get<std::vector<std::int16_t>>(cd.samples);
    EXPECT_EQ(v, (std::vector<std::int16_t>{0, 10, 20, 30}));
}

TEST(BlockReader, parses_abs_timestamp_string_osf5_keeps_payload_verbatim) {
    // OSF5 reader: payload bytes are kept verbatim, no terminator
    // stripping. Body = "hi" (no trailing 0x00 per spec rev 2026-05-24).
    auto meta = make_meta({make_channel(0, osf::DataType::String, 4)});
    std::uint8_t const body_string[] = {'h', 'i'};
    std::uint32_t const payload_len =
        static_cast<std::uint32_t>(1 + 4 + 8 + sizeof(body_string));
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);
    put_u32(bytes, payload_len);
    bytes.push_back(0x88);
    put_u32(bytes, 1);
    put_i64(bytes, 42);
    bytes.insert(bytes.end(), body_string, body_string + sizeof(body_string));
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blk_r)->kind);
    auto const& v = std::get<std::vector<std::pair<std::int64_t, std::string>>>(
        ad.samples);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].first, 42);
    EXPECT_EQ(v[0].second, "hi");
}

TEST(BlockReader, parses_abs_timestamp_string_osf4_strips_last_byte) {
    // OSF4 reader: the spec-mandated trailing 0x00 is stripped
    // unconditionally before the payload reaches the manager.
    auto meta = make_meta_v4({make_channel(0, osf::DataType::String, 4)});
    std::uint8_t const body_string[] = {'h', 'i', 0x00};
    std::uint32_t const payload_len =
        static_cast<std::uint32_t>(1 + 4 + 8 + sizeof(body_string));
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);
    put_u32(bytes, payload_len);
    bytes.push_back(0x88);
    put_u32(bytes, 1);
    put_i64(bytes, 42);
    bytes.insert(bytes.end(), body_string, body_string + sizeof(body_string));
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blk_r)->kind);
    auto const& v = std::get<std::vector<std::pair<std::int64_t, std::string>>>(
        ad.samples);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].first, 42);
    EXPECT_EQ(v[0].second, "hi");
}

TEST(BlockReader, parses_abs_timestamp_binary_osf5_keeps_trailing_null_byte) {
    // OSF5 reader: a trailing 0x00 in a binary payload is a real data
    // byte (ASN.1 blob, protobuf message, ...). The reader keeps it
    // verbatim.
    auto meta = make_meta({make_channel(0, osf::DataType::Binary, 4)});
    std::uint8_t const body[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00};
    std::uint32_t const payload_len =
        static_cast<std::uint32_t>(1 + 4 + 8 + sizeof(body));
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);
    put_u32(bytes, payload_len);
    bytes.push_back(0x88);
    put_u32(bytes, 1);
    put_i64(bytes, 123);
    bytes.insert(bytes.end(), body, body + sizeof(body));
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blk_r)->kind);
    auto const& v = std::get<std::vector<std::pair<std::int64_t,
                                                   std::vector<std::uint8_t>>>>(
        ad.samples);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].first, 123);
    EXPECT_EQ(v[0].second,
              (std::vector<std::uint8_t>{0xFF, 0xD8, 0xFF, 0xE0, 0x00}));
}

TEST(BlockReader, parses_abs_timestamp_binary_osf4_strips_trailing_null_byte) {
    // OSF4 reader: the spec-mandated trailing 0x00 is the terminator
    // and is removed before the payload reaches the manager. JPEG
    // magic + null on disk -> JPEG magic only after strip.
    auto meta = make_meta_v4({make_channel(0, osf::DataType::Binary, 4)});
    std::uint8_t const body[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00};
    std::uint32_t const payload_len =
        static_cast<std::uint32_t>(1 + 4 + 8 + sizeof(body));
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);
    put_u32(bytes, payload_len);
    bytes.push_back(0x88);
    put_u32(bytes, 1);
    put_i64(bytes, 123);
    bytes.insert(bytes.end(), body, body + sizeof(body));
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blk_r)->kind);
    auto const& v = std::get<std::vector<std::pair<std::int64_t,
                                                   std::vector<std::uint8_t>>>>(
        ad.samples);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].first, 123);
    EXPECT_EQ(v[0].second, (std::vector<std::uint8_t>{0xFF, 0xD8, 0xFF, 0xE0}));
}

TEST(BlockReader, parses_abs_timestamp_gpslocation) {
    auto meta = make_meta({make_channel(0, osf::DataType::GpsLocation, 2)});
    std::uint16_t const payload_len = 1 + 8 + 24;
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);
    put_u16(bytes, payload_len);
    bytes.push_back(0x08);
    put_i64(bytes, 999);
    put_f64(bytes, 48.1374);
    put_f64(bytes, 11.5755);
    put_f64(bytes, 519.0);
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blk_r)->kind);
    auto const& v = std::get<std::vector<std::pair<std::int64_t,
                                                   osf::GpsLocation>>>(
        ad.samples);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].first, 999);
    EXPECT_NEAR(v[0].second.latitude,  48.1374, 1e-9);
    EXPECT_NEAR(v[0].second.longitude, 11.5755, 1e-9);
    EXPECT_NEAR(v[0].second.altitude,  519.0,   1e-9);
}

TEST(BlockReader, parses_continued_rel_stamp_data_int16) {
    auto meta = make_meta({make_channel(0, osf::DataType::Int16, 2)});
    std::uint16_t const payload_len = 1 + 4 + 2 * (4 + 2);
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);
    put_u16(bytes, payload_len);
    bytes.push_back(0x87);  // multi
    put_u32(bytes, 2);
    put_u32(bytes, 100);
    put_i16(bytes, 7);
    put_u32(bytes, 200);
    put_i16(bytes, 8);
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& rd = std::get<osf::ContinuedRelStampData>((*blk_r)->kind);
    auto const& v = std::get<std::vector<std::pair<std::uint32_t,
                                                   std::int16_t>>>(rd.samples);
    EXPECT_EQ(v, (std::vector<std::pair<std::uint32_t, std::int16_t>>{
        {100, 7}, {200, 8}}));
}

TEST(BlockReader, equidistant_block_with_string_data_type_is_invalid_block) {
    auto meta = make_meta({make_channel(0, osf::DataType::String, 4)});
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0);
    put_u32(bytes, 5);   // length = 5 (u32 per the channel's sizeof=4)
    bytes.push_back(0x05);
    bytes.insert(bytes.end(), {1, 2, 3, 4});
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r);
    ASSERT_FALSE(blk_r->has_value());
    EXPECT_EQ(blk_r->error().code, osf::Error::Code::InvalidBlock);
}

TEST(BlockReader, trailer_block_is_consumed_silently) {
    auto meta = make_meta({make_channel(0, osf::DataType::Int16, 2)});
    // [u16 0xFFFF][u32 length=2][u8 0][u8 0]; no magic trailer.
    std::vector<std::uint8_t> bytes;
    put_u16(bytes, 0xFFFF);
    put_u32(bytes, 2);
    bytes.push_back(0x00);
    bytes.push_back(0x00);
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    EXPECT_FALSE(r.next().has_value());
    EXPECT_TRUE(r.trailer_seen());
    EXPECT_EQ(r.blocks_truncated(), 0u);
}

// ---------------------------------------------------------------------
// Range-based for loop iterator
// ---------------------------------------------------------------------

TEST(BlockReader, range_based_for_visits_all_blocks) {
    auto meta = make_meta({make_channel(0, osf::DataType::Int64, 2)});
    std::vector<std::uint8_t> bytes;
    for (int i = 0; i < 3; ++i) {
        put_u16(bytes, 0);
        put_u16(bytes, 17);
        bytes.push_back(0x08);
        put_i64(bytes, 100 + i);
        put_i64(bytes, i);
    }
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    int seen = 0;
    for (auto& blk_r : r) {
        ASSERT_TRUE(blk_r.has_value()) << blk_r.error().message;
        ++seen;
    }
    EXPECT_EQ(seen, 3);
    EXPECT_EQ(r.stats().blocks_read, 3u);
}

}  // namespace
