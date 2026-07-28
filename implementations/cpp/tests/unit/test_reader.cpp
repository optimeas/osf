// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Unit tests for osf::BlockReader against synthetic byte sequences.
//
// Direct port of implementations/rust/osf-core/src/reader.rs tests
// covering the same code paths (truncation, unknown channel index,
// Unsupported channel skipping, capture-skipped opt-in, deprecated /
// reserved control bytes, every supported control byte's decode,
// trailer consumption).

#include <gtest/gtest.h>

#include <osf/osf.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------
// Builders for synthetic MetaBlocks and byte streams.
// ---------------------------------------------------------------------

osf::Channel makeChannel(std::uint16_t index, osf::DataType dt,
                          std::uint8_t sizeOfLengthValue,
                          osf::ChannelType ct = osf::ChannelType::Scalar) {
    osf::Channel ch;
    ch.index = index;
    ch.name = "ch" + std::to_string(index);
    ch.channelType = ct;
    ch.dataType = dt;
    ch.sizeOfLengthValue = sizeOfLengthValue;
    return ch;
}

osf::MetaBlock makeMeta(std::vector<osf::Channel> channels) {
    osf::MetaBlock mb;
    mb.fileInfo.version = 5;
    mb.channels = std::move(channels);
    return mb;
}

osf::MetaBlock makeMetaV4(std::vector<osf::Channel> channels) {
    osf::MetaBlock mb;
    mb.fileInfo.version = 4;
    mb.channels = std::move(channels);
    return mb;
}

void putU16(std::vector<std::uint8_t>& dst, std::uint16_t v) {
    dst.push_back(static_cast<std::uint8_t>( v        & 0xFF));
    dst.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFF));
}

void putU32(std::vector<std::uint8_t>& dst, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        dst.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

void putI16(std::vector<std::uint8_t>& dst, std::int16_t v) {
    putU16(dst, static_cast<std::uint16_t>(v));
}

void putI64(std::vector<std::uint8_t>& dst, std::int64_t v) {
    auto const u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i) {
        dst.push_back(static_cast<std::uint8_t>((u >> (8 * i)) & 0xFF));
    }
}

void putF64(std::vector<std::uint8_t>& dst, double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        dst.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF));
    }
}

void putF32(std::vector<std::uint8_t>& dst, float v) {
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
    auto meta = makeMeta({});
    ByteStream s({});
    osf::BlockReader r(s.get(), meta);
    EXPECT_FALSE(r.next().has_value());
    EXPECT_EQ(r.blocksTruncated(), 0u);
}

TEST(BlockReader, truncation_in_channel_index_is_silent_eof) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int16, 2)});
    ByteStream s({0x00});  // single byte; can't form u16
    osf::BlockReader r(s.get(), meta);
    EXPECT_FALSE(r.next().has_value());
    EXPECT_EQ(r.blocksTruncated(), 0u);
}

TEST(BlockReader, truncation_in_length_field_bumps_counter) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int16, 4)});
    // channel 0, then only 2 of 4 length bytes.
    ByteStream s({0x00, 0x00, 0x01, 0x00});
    osf::BlockReader r(s.get(), meta);
    EXPECT_FALSE(r.next().has_value());
    EXPECT_EQ(r.blocksTruncated(), 1u);
}

TEST(BlockReader, truncation_mid_payload_bumps_counter) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int16, 2)});
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);   // channel
    putU16(bytes, 10);  // length = 10 but only 5 bytes follow
    bytes.push_back(0x08);
    bytes.insert(bytes.end(), {0, 0, 0, 0});
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    EXPECT_FALSE(r.next().has_value());
    EXPECT_EQ(r.blocksTruncated(), 1u);
}

// ---------------------------------------------------------------------
// Hard errors
// ---------------------------------------------------------------------

TEST(BlockReader, unknown_channel_index_is_hard_error) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int16, 2)});
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
    auto meta = makeMeta({makeChannel(0, osf::DataType::Unsupported, 2)});
    std::vector<std::uint8_t> bytes;
    // Two back-to-back blocks; the first must be skipped, the second
    // must be reachable.
    putU16(bytes, 0);
    putU16(bytes, 5);
    bytes.insert(bytes.end(), {0xAA, 1, 2, 3, 4});
    putU16(bytes, 0);
    putU16(bytes, 5);
    bytes.insert(bytes.end(), {0xAA, 5, 6, 7, 8});
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);

    auto first = r.next();
    ASSERT_TRUE(first && first->has_value());
    auto const& blk1 = **first;
    EXPECT_EQ(blk1.channelIndex, 0u);
    auto const& sk1 = std::get<osf::Skipped>(blk1.kind);
    EXPECT_EQ(sk1.reason.kind, osf::SkipReason::Kind::UnsupportedDataType);
    EXPECT_EQ(sk1.bytesSkipped, 5u);
    EXPECT_FALSE(sk1.payload.has_value());  // default capture is off

    auto second = r.next();
    ASSERT_TRUE(second && second->has_value());
    EXPECT_EQ((*second)->channelIndex, 0u);  // stream still aligned
}

TEST(BlockReader, capture_skipped_payload_keeps_body_bytes) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int16, 2)});
    // Reserved control byte 0 → Skipped; body is 2 bytes after the
    // control byte.
    std::vector<std::uint8_t> bytes = {0, 0, 3, 0, 0x00, 0xAA, 0xBB};
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    r.withCaptureSkippedPayload(true);

    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& sk = std::get<osf::Skipped>((*blkR)->kind);
    EXPECT_EQ(sk.reason.kind, osf::SkipReason::Kind::ReservedBlockType);
    EXPECT_EQ(sk.reason.rawByte, 0u);
    EXPECT_EQ(sk.bytesSkipped, 3u);
    ASSERT_TRUE(sk.payload.has_value());
    EXPECT_EQ(*sk.payload, (std::vector<std::uint8_t>{0xAA, 0xBB}));
}

// ---------------------------------------------------------------------
// Deprecated / reserved control bytes
// ---------------------------------------------------------------------

TEST(BlockReader, deprecated_control_bytes_route_to_deprecated_skip_reason) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int16, 2)});
    for (std::uint8_t raw : {std::uint8_t{1}, std::uint8_t{3}, std::uint8_t{4}}) {
        std::vector<std::uint8_t> bytes = {0, 0, 1, 0, raw};
        ByteStream s(std::move(bytes));
        osf::BlockReader r(s.get(), meta);
        auto blkR = r.next();
        ASSERT_TRUE(blkR && blkR->has_value());
        auto const& sk = std::get<osf::Skipped>((*blkR)->kind);
        EXPECT_EQ(sk.reason.kind, osf::SkipReason::Kind::DeprecatedBlockType)
            << "raw byte " << int{raw};
        EXPECT_EQ(sk.reason.rawByte, raw) << "raw byte " << int{raw};
    }
}

TEST(BlockReader, unknown_high_control_byte_routes_to_reserved_skip_reason) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int16, 2)});
    std::vector<std::uint8_t> bytes = {0, 0, 1, 0, 0x55};
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& sk = std::get<osf::Skipped>((*blkR)->kind);
    EXPECT_EQ(sk.reason.kind, osf::SkipReason::Kind::ReservedBlockType);
    EXPECT_EQ(sk.reason.rawByte, 0x55);
}

TEST(BlockReader, zero_length_block_is_skipped_and_scan_continues) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int16, 2)});
    std::vector<std::uint8_t> bytes;
    // Block 1 — the non-conforming case: channel index 0, length field 0.
    // No control byte follows; the frame is these four bytes only.
    putU16(bytes, 0);
    putU16(bytes, 0);
    // Block 2 — a well-formed single-sample bcAbsTimeStampData (control byte 8,
    // bit 7 clear => N = 1): 1 + 8 + 2 = 11 payload bytes.
    putU16(bytes, 0);
    putU16(bytes, 11);
    bytes.push_back(8);
    putI64(bytes, 1);
    putI16(bytes, 42);

    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);

    auto firstR = r.next();
    ASSERT_TRUE(firstR && firstR->has_value());
    auto const& sk = std::get<osf::Skipped>((*firstR)->kind);
    EXPECT_EQ(sk.reason.kind, osf::SkipReason::Kind::ZeroLengthBlock);
    EXPECT_EQ(sk.bytesSkipped, 0u);

    // The scan must continue and decode the block behind the bad frame.
    auto secondR = r.next();
    ASSERT_TRUE(secondR && secondR->has_value());
    EXPECT_TRUE(std::holds_alternative<osf::AbsTimestampData>((*secondR)->kind));

    EXPECT_FALSE(r.next().has_value());
    EXPECT_EQ(r.stats().blocksSkippedZeroLength, 1u);
    EXPECT_EQ(r.stats().blocksSkippedReservedType, 0u);
    EXPECT_EQ(r.blocksTruncated(), 0u);

    // The file has exactly two blocks: one skipped, one decoded.
    // blocksTotal must agree with both the iterator and the per-channel
    // totals (see ReaderStats::blocksTotal doc comment).
    EXPECT_EQ(r.stats().blocksTotal, 2u);
    EXPECT_EQ(r.stats().blocksRead, 1u);
}

// ---------------------------------------------------------------------
// Typed parsers — happy-path coverage
// ---------------------------------------------------------------------

TEST(BlockReader, parses_single_sample_abs_timestamp_int64) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int64, 2)});
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);
    putU16(bytes, 17);  // 1 ctl + 8 ts + 8 i64
    bytes.push_back(0x08);
    putI64(bytes, 0x18AC'BBA9'5F76'EC57LL);
    putI64(bytes, 0);
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blkR)->kind);
    auto const& v = std::get<std::vector<std::pair<std::int64_t, std::int64_t>>>(
        ad.samples);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].first,  0x18AC'BBA9'5F76'EC57LL);
    EXPECT_EQ(v[0].second, 0);
}

TEST(BlockReader, parses_multi_sample_abs_timestamp_double) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Double, 2)});
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);
    std::uint16_t const payloadLen = 1 + 4 + 2 * (8 + 8);
    putU16(bytes, payloadLen);
    bytes.push_back(0x88);   // multi
    putU32(bytes, 2);
    putI64(bytes, 100);
    putF64(bytes, 1.5);
    putI64(bytes, 200);
    putF64(bytes, 2.5);
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blkR)->kind);
    auto const& v = std::get<std::vector<std::pair<std::int64_t, double>>>(
        ad.samples);
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].first, 100); EXPECT_DOUBLE_EQ(v[0].second, 1.5);
    EXPECT_EQ(v[1].first, 200); EXPECT_DOUBLE_EQ(v[1].second, 2.5);
}

TEST(BlockReader, parses_start_data_single_sample_double) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Double, 2)});
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);
    putU16(bytes, 25);  // 1 ctl + 8 ts + 8 rate + 8 value
    bytes.push_back(0x06);
    putI64(bytes, 1'000'000);
    putF64(bytes, 1000.0);
    putF64(bytes, 2.5);
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& sd = std::get<osf::StartData>((*blkR)->kind);
    EXPECT_EQ(sd.startTimestampNs, 1'000'000);
    EXPECT_NEAR(sd.sampleRateHz, 1000.0, 1e-9);
    auto const& v = std::get<std::vector<double>>(sd.samples);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_DOUBLE_EQ(v[0], 2.5);
}

TEST(BlockReader, parses_start_data_multi_sample_float_n10) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Float, 2)});
    std::uint32_t const n = 10;
    std::uint16_t const payloadLen =
        static_cast<std::uint16_t>(1 + 8 + 8 + 4 + n * 4);
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);
    putU16(bytes, payloadLen);
    bytes.push_back(0x86);     // multi
    putI64(bytes, 7);
    putF64(bytes, 500.0);
    putU32(bytes, n);
    for (std::uint32_t i = 0; i < n; ++i) putF32(bytes, static_cast<float>(i));
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& sd = std::get<osf::StartData>((*blkR)->kind);
    auto const& v = std::get<std::vector<float>>(sd.samples);
    ASSERT_EQ(v.size(), n);
    EXPECT_FLOAT_EQ(v[3], 3.0f);
}

TEST(BlockReader, parses_continued_data_int16_multi) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int16, 2)});
    std::uint32_t const n = 4;
    std::uint16_t const payloadLen =
        static_cast<std::uint16_t>(1 + 4 + n * 2);
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);
    putU16(bytes, payloadLen);
    bytes.push_back(0x85);  // multi
    putU32(bytes, n);
    for (std::uint32_t i = 0; i < n; ++i)
        putI16(bytes, static_cast<std::int16_t>(10 * i));
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& cd = std::get<osf::ContinuedData>((*blkR)->kind);
    auto const& v = std::get<std::vector<std::int16_t>>(cd.samples);
    EXPECT_EQ(v, (std::vector<std::int16_t>{0, 10, 20, 30}));
}

TEST(BlockReader, parses_abs_timestamp_string_osf5_keeps_payload_verbatim) {
    // OSF5 reader: payload bytes are kept verbatim, no terminator
    // stripping. Body = "hi" (no trailing 0x00 per spec rev 2026-05-24).
    auto meta = makeMeta({makeChannel(0, osf::DataType::String, 4)});
    std::uint8_t const bodyString[] = {'h', 'i'};
    std::uint32_t const payloadLen =
        static_cast<std::uint32_t>(1 + 4 + 8 + sizeof(bodyString));
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);
    putU32(bytes, payloadLen);
    bytes.push_back(0x88);
    putU32(bytes, 1);
    putI64(bytes, 42);
    bytes.insert(bytes.end(), bodyString, bodyString + sizeof(bodyString));
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blkR)->kind);
    auto const& v = std::get<std::vector<std::pair<std::int64_t, std::string>>>(
        ad.samples);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].first, 42);
    EXPECT_EQ(v[0].second, "hi");
}

TEST(BlockReader, parses_abs_timestamp_string_osf4_strips_last_byte) {
    // OSF4 reader: the spec-mandated trailing 0x00 is stripped
    // unconditionally before the payload reaches the manager.
    auto meta = makeMetaV4({makeChannel(0, osf::DataType::String, 4)});
    std::uint8_t const bodyString[] = {'h', 'i', 0x00};
    std::uint32_t const payloadLen =
        static_cast<std::uint32_t>(1 + 4 + 8 + sizeof(bodyString));
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);
    putU32(bytes, payloadLen);
    bytes.push_back(0x88);
    putU32(bytes, 1);
    putI64(bytes, 42);
    bytes.insert(bytes.end(), bodyString, bodyString + sizeof(bodyString));
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blkR)->kind);
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
    auto meta = makeMeta({makeChannel(0, osf::DataType::Binary, 4)});
    std::uint8_t const body[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00};
    std::uint32_t const payloadLen =
        static_cast<std::uint32_t>(1 + 4 + 8 + sizeof(body));
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);
    putU32(bytes, payloadLen);
    bytes.push_back(0x88);
    putU32(bytes, 1);
    putI64(bytes, 123);
    bytes.insert(bytes.end(), body, body + sizeof(body));
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blkR)->kind);
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
    auto meta = makeMetaV4({makeChannel(0, osf::DataType::Binary, 4)});
    std::uint8_t const body[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00};
    std::uint32_t const payloadLen =
        static_cast<std::uint32_t>(1 + 4 + 8 + sizeof(body));
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);
    putU32(bytes, payloadLen);
    bytes.push_back(0x88);
    putU32(bytes, 1);
    putI64(bytes, 123);
    bytes.insert(bytes.end(), body, body + sizeof(body));
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blkR)->kind);
    auto const& v = std::get<std::vector<std::pair<std::int64_t,
                                                   std::vector<std::uint8_t>>>>(
        ad.samples);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].first, 123);
    EXPECT_EQ(v[0].second, (std::vector<std::uint8_t>{0xFF, 0xD8, 0xFF, 0xE0}));
}

TEST(BlockReader, parses_abs_timestamp_gpslocation) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::GpsLocation, 2)});
    std::uint16_t const payloadLen = 1 + 8 + 24;
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);
    putU16(bytes, payloadLen);
    bytes.push_back(0x08);
    putI64(bytes, 999);
    putF64(bytes, 48.1374);
    putF64(bytes, 11.5755);
    putF64(bytes, 519.0);
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& ad = std::get<osf::AbsTimestampData>((*blkR)->kind);
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
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int16, 2)});
    std::uint16_t const payloadLen = 1 + 4 + 2 * (4 + 2);
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);
    putU16(bytes, payloadLen);
    bytes.push_back(0x87);  // multi
    putU32(bytes, 2);
    putU32(bytes, 100);
    putI16(bytes, 7);
    putU32(bytes, 200);
    putI16(bytes, 8);
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& rd = std::get<osf::ContinuedRelStampData>((*blkR)->kind);
    auto const& v = std::get<std::vector<std::pair<std::uint32_t,
                                                   std::int16_t>>>(rd.samples);
    EXPECT_EQ(v, (std::vector<std::pair<std::uint32_t, std::int16_t>>{
        {100, 7}, {200, 8}}));
}

TEST(BlockReader, equidistant_block_with_string_data_type_is_invalid_block) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::String, 4)});
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0);
    putU32(bytes, 5);   // length = 5 (u32 per the channel's sizeof=4)
    bytes.push_back(0x05);
    bytes.insert(bytes.end(), {1, 2, 3, 4});
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR);
    ASSERT_FALSE(blkR->has_value());
    EXPECT_EQ(blkR->error().code, osf::Error::Code::InvalidBlock);
}

TEST(BlockReader, trailer_block_is_consumed_silently) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int16, 2)});
    // [u16 0xFFFF][u32 length=2][u8 0][u8 0]; no magic trailer.
    std::vector<std::uint8_t> bytes;
    putU16(bytes, 0xFFFF);
    putU32(bytes, 2);
    bytes.push_back(0x00);
    bytes.push_back(0x00);
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    EXPECT_FALSE(r.next().has_value());
    EXPECT_TRUE(r.trailerSeen());
    EXPECT_EQ(r.blocksTruncated(), 0u);
}

// ---------------------------------------------------------------------
// Range-based for loop iterator
// ---------------------------------------------------------------------

TEST(BlockReader, range_based_for_visits_all_blocks) {
    auto meta = makeMeta({makeChannel(0, osf::DataType::Int64, 2)});
    std::vector<std::uint8_t> bytes;
    for (int i = 0; i < 3; ++i) {
        putU16(bytes, 0);
        putU16(bytes, 17);
        bytes.push_back(0x08);
        putI64(bytes, 100 + i);
        putI64(bytes, i);
    }
    ByteStream s(std::move(bytes));
    osf::BlockReader r(s.get(), meta);
    int seen = 0;
    for (auto& blkR : r) {
        ASSERT_TRUE(blkR.has_value()) << blkR.error().message;
        ++seen;
    }
    EXPECT_EQ(seen, 3);
    EXPECT_EQ(r.stats().blocksRead, 3u);
}

}  // namespace
