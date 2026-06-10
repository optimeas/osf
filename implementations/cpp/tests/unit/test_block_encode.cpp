// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "block_encode.hpp"
#include "binary_io.hpp"
#include "osf/error.hpp"
#include "osf/reader.hpp"
#include "osf/metablock.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

// ---------------------------------------------------------------------------
// Shared test utilities
// ---------------------------------------------------------------------------

namespace {

using osf::GpsLocation;
using osf::detail::BinarySample;
using osf::detail::encode_abs_timestamp_data;
using osf::detail::encode_abs_timestamp_data_gps;
using osf::detail::encode_continued_data;
using osf::detail::encode_start_data;

// Helper: compare a slice of a byte vector against an expected byte sequence.
::testing::AssertionResult bytes_eq(std::vector<std::uint8_t> const& got,
                                    std::size_t offset,
                                    std::vector<std::uint8_t> const& expected) {
    if (got.size() < offset + expected.size()) {
        return ::testing::AssertionFailure()
            << "buffer too short: have " << got.size() << " bytes, need "
            << offset + expected.size();
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (got[offset + i] != expected[i]) {
            return ::testing::AssertionFailure()
                << "byte mismatch at offset " << (offset + i) << ": got 0x"
                << std::hex << static_cast<int>(got[offset + i])
                << " expected 0x" << static_cast<int>(expected[i]);
        }
    }
    return ::testing::AssertionSuccess();
}

// Build a one-channel MetaBlock for the synthetic-bytes roundtrip pattern.
osf::MetaBlock one_channel_meta(osf::DataType dt,
                                osf::ChannelType ct,
                                std::uint8_t sizeoflengthvalue,
                                int osf_version = 5) {
    osf::MetaBlock m;
    m.file_info.version = static_cast<std::uint32_t>(osf_version);
    osf::Channel ch;
    ch.index = 0;
    ch.name = "test_channel";
    ch.data_type = dt;
    ch.channel_type = ct;
    ch.size_of_length_value = sizeoflengthvalue;
    m.channels.push_back(std::move(ch));
    return m;
}

}  // namespace

// ---------------------------------------------------------------------------
// Smoke tests (Task 2)
// ---------------------------------------------------------------------------

TEST(BlockEncodeSmoke, BinarySampleFromPointer) {
    std::uint8_t buf[] = {0xDE, 0xAD, 0xBE, 0xEF};
    BinarySample s{buf, 4};
    EXPECT_EQ(s.data, buf);
    EXPECT_EQ(s.size, std::size_t{4});
}

TEST(BlockEncodeSmoke, BinarySampleFromVector) {
    std::vector<std::uint8_t> v = {0x01, 0x02, 0x03};
    auto s = BinarySample::from_vector(v);
    EXPECT_EQ(s.data, v.data());
    EXPECT_EQ(s.size, std::size_t{3});
}

// ---------------------------------------------------------------------------
// Task 3: byte-exact tests for encode_start_data<float>
// ---------------------------------------------------------------------------

TEST(BlockEncodeStartData, FloatSingleSample_Frame) {
    std::vector<std::uint8_t> out;
    float const samples[] = {1.5f};
    auto r = encode_start_data<float>(out, /*ch=*/7, /*sizeoflengthvalue=*/2,
                                      /*start_ts_ns=*/1'000'000'000LL,
                                      /*sample_rate_hz=*/100.0,
                                      samples, /*count=*/1);
    ASSERT_TRUE(r.has_value()) << "encoder returned error";

    // Frame: [ci=0x07,0x00][len_u16=21][ctrl=0x06][ts=8B][rate=8B][float=4B]
    // 1 (ctrl) + 8 (ts) + 8 (rate) + 4 (float) = 21 -> u16=21=0x0015
    // Total frame = 2 (ci) + 2 (len) + 21 (payload) = 25 bytes.
    ASSERT_EQ(out.size(), 25u);

    EXPECT_TRUE(bytes_eq(out, 0, {0x07, 0x00}));         // channel_index=7
    EXPECT_TRUE(bytes_eq(out, 2, {0x15, 0x00}));         // payload_length u16
    EXPECT_EQ(out[4], 0x06);                              // bcStartData, bit-7=0
}

TEST(BlockEncodeStartData, FloatSingleSample_NoNPrefix) {
    // For count==1 with bit-7=0 the encoder MUST NOT emit a uint32 N-prefix.
    // The 4 bytes immediately after sample_rate are the sample itself.
    std::vector<std::uint8_t> out;
    float const samples[] = {2.0f};
    auto r = encode_start_data<float>(out, 0, 2, 0LL, 50.0, samples, 1);
    ASSERT_TRUE(r.has_value());

    // Sample at offset 2 + 2 + 1 + 8 + 8 = 21 (last 4 bytes of the 25-byte frame).
    std::uint32_t bits;
    std::memcpy(&bits, out.data() + 21, sizeof(bits));
    float decoded;
    std::memcpy(&decoded, &bits, sizeof(decoded));
    EXPECT_FLOAT_EQ(decoded, 2.0f);
}

TEST(BlockEncodeStartData, FloatThreeSamples_Bit7Set) {
    std::vector<std::uint8_t> out;
    float const samples[] = {1.0f, 2.0f, 3.0f};
    auto r = encode_start_data<float>(out, /*ch=*/0, /*sizeoflengthvalue=*/2,
                                      /*ts=*/0LL, /*rate=*/10.0, samples, 3);
    ASSERT_TRUE(r.has_value());

    // 1 (ctrl) + 8 (ts) + 8 (rate) + 4 (N=3) + 12 (3 floats) = 33 bytes
    // Frame = 2 + 2 + 33 = 37 bytes.
    ASSERT_EQ(out.size(), 37u);

    EXPECT_EQ(out[4], 0x86);                              // bcStartData | bit-7
    EXPECT_TRUE(bytes_eq(out, 21, {0x03, 0x00, 0x00, 0x00}));  // N=3 u32 LE
}

// ---------------------------------------------------------------------------
// Task 3: byte-exact tests for encode_continued_data<double>
// ---------------------------------------------------------------------------

TEST(BlockEncodeContinuedData, DoubleSingleSample_Frame) {
    std::vector<std::uint8_t> out;
    double const samples[] = {3.14};
    auto r = encode_continued_data<double>(out, /*ch=*/3, /*sizeoflengthvalue=*/2,
                                            samples, /*count=*/1);
    ASSERT_TRUE(r.has_value());

    // 1 (ctrl) + 8 (double) = 9 -> u16=0x0009. Frame = 2 + 2 + 9 = 13.
    ASSERT_EQ(out.size(), 13u);

    EXPECT_TRUE(bytes_eq(out, 0, {0x03, 0x00}));         // channel_index=3
    EXPECT_TRUE(bytes_eq(out, 2, {0x09, 0x00}));         // payload_length u16
    EXPECT_EQ(out[4], 0x05);                              // bcContinuedData, bit-7=0
}

TEST(BlockEncodeContinuedData, DoubleFiveSamples_Bit7Set) {
    std::vector<std::uint8_t> out;
    double const samples[] = {1.0, 2.0, 3.0, 4.0, 5.0};
    auto r = encode_continued_data<double>(out, 0, 2, samples, 5);
    ASSERT_TRUE(r.has_value());

    // 1 (ctrl) + 4 (N) + 40 (5 doubles) = 45 bytes. Frame = 2 + 2 + 45 = 49.
    ASSERT_EQ(out.size(), 49u);

    EXPECT_EQ(out[4], 0x85);                              // bcContinuedData | bit-7
    EXPECT_TRUE(bytes_eq(out, 5, {0x05, 0x00, 0x00, 0x00}));  // N=5
}

// ---------------------------------------------------------------------------
// Task 3: error-path tests
// ---------------------------------------------------------------------------

TEST(BlockEncodeStartData, ZeroCountReturnsInvalidArgument) {
    std::vector<std::uint8_t> out;
    float dummy = 0.0f;
    auto r = encode_start_data<float>(out, 0, 2, 0LL, 100.0, &dummy, 0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
    EXPECT_TRUE(out.empty()) << "encoder must not partial-write on error";
}

TEST(BlockEncodeStartData, BadSizeofLengthValueReturnsInvalidArgument) {
    std::vector<std::uint8_t> out;
    float samples[] = {1.0f};
    auto r = encode_start_data<float>(out, 0, /*=3*/3, 0LL, 100.0, samples, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
    EXPECT_TRUE(out.empty());
}

TEST(BlockEncodeStartData, OversizePayloadReturnsInvalidBlock) {
    // With sizeoflengthvalue=2 the payload_length must fit in u16
    // (<=65535). Body = 8 (ts) + 8 (rate) + 4 (N) + N*4 (floats) +
    // 1 (ctrl). For N samples we need 21 + N*4 <= 65535,
    // so N >= 16379 trips the limit (21 + 16379*4 = 65537).
    std::vector<std::uint8_t> out;
    std::vector<float> big(16379, 1.0f);
    auto r = encode_start_data<float>(out, 0, /*=2*/2, 0LL, 100.0,
                                      big.data(), big.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidBlock);

    // Exactly at the boundary (N=16378) succeeds.
    out.clear();
    std::vector<float> ok(16378, 1.0f);
    auto r2 = encode_start_data<float>(out, 0, 2, 0LL, 100.0,
                                       ok.data(), ok.size());
    EXPECT_TRUE(r2.has_value());
}

// ---------------------------------------------------------------------------
// Post-encoder coverage extensions (final-review nits N1/N2/N3)
// ---------------------------------------------------------------------------

// N1 — encode_continued_data has the same count==0 guard as encode_start_data;
// exercise it independently so the error-path matrix is fully covered.
TEST(BlockEncodeContinuedData, ZeroCountReturnsInvalidArgument) {
    std::vector<std::uint8_t> out;
    float dummy = 0.0f;
    auto r = encode_continued_data<float>(out, 0, 2, &dummy, 0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
    EXPECT_TRUE(out.empty()) << "encoder must not partial-write on error";
}

// N2 — verify the u32 length-field path (sizeoflengthvalue=4). The prior
// byte-exact tests only exercise the u16 path; roundtrip tests cover u32
// implicitly but do not pin its on-disk shape.
TEST(BlockEncodeStartData, SizeofLengthValue4_U32LengthField) {
    std::vector<std::uint8_t> out;
    float const samples[] = {1.0f};
    auto r = encode_start_data<float>(out, /*ch=*/0, /*sizeoflengthvalue=*/4,
                                      /*ts=*/0LL, /*rate=*/100.0, samples, 1);
    ASSERT_TRUE(r.has_value());

    // Frame: [ci u16=0][len u32=21][ctrl=0x06][ts 8B][rate 8B][float 4B]
    // = 2 + 4 + 1 + 8 + 8 + 4 = 27 bytes.
    ASSERT_EQ(out.size(), 27u);
    EXPECT_TRUE(bytes_eq(out, 0, {0x00, 0x00}));                      // channel_index=0
    EXPECT_TRUE(bytes_eq(out, 2, {0x15, 0x00, 0x00, 0x00}));          // payload_length u32=21
    EXPECT_EQ(out[6], 0x06);                                           // bcStartData, bit-7=0
}

// N3 — exercise the high byte of the u16 channel_index field. Prior tests
// only used channel indices <= 7, which leaves the second byte zero. A
// non-zero high byte (here 0x01 for channel 0x0142) pins the LE layout.
TEST(BlockEncodeStartData, ChannelIndexHighByte) {
    std::vector<std::uint8_t> out;
    float const samples[] = {1.0f};
    auto r = encode_start_data<float>(out, /*ch=*/0x0142, /*sizeoflengthvalue=*/2,
                                      /*ts=*/0LL, /*rate=*/100.0, samples, 1);
    ASSERT_TRUE(r.has_value());

    // First two bytes are channel_index in little-endian: 0x0142 -> {0x42, 0x01}.
    EXPECT_TRUE(bytes_eq(out, 0, {0x42, 0x01}));
}

// ---------------------------------------------------------------------------
// Task 3: roundtrip tests via BlockReader
// ---------------------------------------------------------------------------

TEST(BlockEncodeRoundtrip, StartDataFloat) {
    std::vector<std::uint8_t> out;
    float const samples[] = {1.5f, 2.5f, 3.5f};
    auto r = encode_start_data<float>(out, 0, 2, 1'000'000'000LL,
                                      100.0, samples, 3);
    ASSERT_TRUE(r.has_value());

    auto meta = one_channel_meta(osf::DataType::Float,
                                 osf::ChannelType::Equidistant, 2);
    std::string s(reinterpret_cast<char const*>(out.data()), out.size());
    std::istringstream in(s);
    osf::BlockReader rdr(in, meta);
    auto block_opt = rdr.next();
    ASSERT_TRUE(block_opt.has_value());
    ASSERT_TRUE(block_opt->has_value());
    auto* sd = std::get_if<osf::StartData>(&block_opt->value().kind);
    ASSERT_NE(sd, nullptr);
    EXPECT_EQ(sd->start_timestamp_ns, 1'000'000'000LL);
    EXPECT_DOUBLE_EQ(sd->sample_rate_hz, 100.0);
    auto* fv = std::get_if<std::vector<float>>(&sd->samples);
    ASSERT_NE(fv, nullptr);
    ASSERT_EQ(fv->size(), 3u);
    EXPECT_FLOAT_EQ((*fv)[0], 1.5f);
    EXPECT_FLOAT_EQ((*fv)[1], 2.5f);
    EXPECT_FLOAT_EQ((*fv)[2], 3.5f);
}

TEST(BlockEncodeRoundtrip, ContinuedDataDouble) {
    std::vector<std::uint8_t> out;
    double const samples[] = {1.1, 2.2, 3.3, 4.4};
    auto r = encode_continued_data<double>(out, 0, 4, samples, 4);
    ASSERT_TRUE(r.has_value());

    auto meta = one_channel_meta(osf::DataType::Double,
                                 osf::ChannelType::Equidistant, 4);
    std::string s(reinterpret_cast<char const*>(out.data()), out.size());
    std::istringstream in(s);
    osf::BlockReader rdr(in, meta);
    auto block_opt = rdr.next();
    ASSERT_TRUE(block_opt.has_value());
    ASSERT_TRUE(block_opt->has_value());
    auto* cd = std::get_if<osf::ContinuedData>(&block_opt->value().kind);
    ASSERT_NE(cd, nullptr);
    auto* dv = std::get_if<std::vector<double>>(&cd->samples);
    ASSERT_NE(dv, nullptr);
    ASSERT_EQ(dv->size(), 4u);
    EXPECT_DOUBLE_EQ((*dv)[0], 1.1);
    EXPECT_DOUBLE_EQ((*dv)[3], 4.4);
}

// ---------------------------------------------------------------------------
// Task 4: byte-exact tests for encode_abs_timestamp_data<T>
// ---------------------------------------------------------------------------

TEST(BlockEncodeAbsTs, Int32SingleSample_Bit7Clear) {
    std::vector<std::uint8_t> out;
    std::int64_t const ts = 42;
    std::int32_t const samples[] = {-1};
    auto r = encode_abs_timestamp_data<std::int32_t>(
        out, /*ch=*/2, /*=2*/2, &ts, samples, 1);
    ASSERT_TRUE(r.has_value());

    // 1 (ctrl) + 8 (ts) + 4 (i32) = 13 -> u16=0x000D. Frame = 2 + 2 + 13 = 17.
    ASSERT_EQ(out.size(), 17u);
    EXPECT_TRUE(bytes_eq(out, 0, {0x02, 0x00}));    // ci=2
    EXPECT_TRUE(bytes_eq(out, 2, {0x0D, 0x00}));    // len=13
    EXPECT_EQ(out[4], 0x08);                         // bcAbsTimeStampData, bit-7=0
}

TEST(BlockEncodeAbsTs, DoubleThreeSamples_Bit7Set) {
    std::vector<std::uint8_t> out;
    std::int64_t const tss[] = {1, 2, 3};
    double const samples[] = {1.0, 2.0, 3.0};
    auto r = encode_abs_timestamp_data<double>(out, 0, 2, tss, samples, 3);
    ASSERT_TRUE(r.has_value());

    // 1 (ctrl) + 4 (N) + 3 * (8 ts + 8 double) = 1 + 4 + 48 = 53.
    // Frame = 2 + 2 + 53 = 57.
    ASSERT_EQ(out.size(), 57u);
    EXPECT_EQ(out[4], 0x88);                         // bcAbsTimeStampData | bit-7
    EXPECT_TRUE(bytes_eq(out, 5, {0x03, 0x00, 0x00, 0x00}));  // N=3
}

TEST(BlockEncodeAbsTs, BoolSingleSample_OneByte) {
    std::vector<std::uint8_t> out;
    std::int64_t const ts = 0;
    bool const samples[] = {true};
    auto r = encode_abs_timestamp_data<bool>(out, 0, 2, &ts, samples, 1);
    ASSERT_TRUE(r.has_value());

    // 1 (ctrl) + 8 (ts) + 1 (bool) = 10 -> u16=0x000A. Frame = 14.
    ASSERT_EQ(out.size(), 14u);
    EXPECT_EQ(out[4], 0x08);                         // bcAbsTimeStampData, bit-7=0
    EXPECT_EQ(out[13], 0x01);                        // true encoded as 0x01
}

// ---------------------------------------------------------------------------
// Task 4: error-path tests for encode_abs_timestamp_data
// ---------------------------------------------------------------------------

TEST(BlockEncodeAbsTs, ZeroCountInvalidArgument) {
    std::vector<std::uint8_t> out;
    std::int64_t ts = 0;
    std::int32_t s = 0;
    auto r = encode_abs_timestamp_data<std::int32_t>(out, 0, 2, &ts, &s, 0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
    EXPECT_TRUE(out.empty());
}

// ---------------------------------------------------------------------------
// Task 4: roundtrip tests for all 11 supported numeric types
// ---------------------------------------------------------------------------

namespace {

template <typename T>
void roundtrip_abs_ts_one(osf::DataType dt, T value) {
    std::vector<std::uint8_t> out;
    std::int64_t const ts = 12345;
    auto r = encode_abs_timestamp_data<T>(out, 0, 2, &ts, &value, 1);
    ASSERT_TRUE(r.has_value()) << "encoder failed for " << static_cast<int>(dt);

    auto meta = one_channel_meta(dt, osf::ChannelType::Timestamped, 2);
    std::string s(reinterpret_cast<char const*>(out.data()), out.size());
    std::istringstream in(s);
    osf::BlockReader rdr(in, meta);
    auto block_opt = rdr.next();
    ASSERT_TRUE(block_opt.has_value());
    ASSERT_TRUE(block_opt->has_value());
    auto* ats = std::get_if<osf::AbsTimestampData>(&block_opt->value().kind);
    ASSERT_NE(ats, nullptr);
    auto* pairs = std::get_if<std::vector<std::pair<std::int64_t, T>>>(
        &ats->samples);
    ASSERT_NE(pairs, nullptr);
    ASSERT_EQ(pairs->size(), 1u);
    EXPECT_EQ((*pairs)[0].first, ts);
    // Use the matcher appropriate for the storage type. Integral types
    // round-trip bit-for-bit so plain equality is fine; floating-point
    // types are compared with the GoogleTest macros that allow a
    // 4-ULP tolerance — exact-equality on float/double is a maintenance
    // hazard even when the literal happens to be representable.
    if constexpr (std::is_same_v<T, float>) {
        EXPECT_FLOAT_EQ((*pairs)[0].second, value);
    } else if constexpr (std::is_same_v<T, double>) {
        EXPECT_DOUBLE_EQ((*pairs)[0].second, value);
    } else {
        EXPECT_EQ((*pairs)[0].second, value);
    }
}

}  // namespace

TEST(BlockEncodeRoundtripAbsTs, AllNumericTypes) {
    roundtrip_abs_ts_one<bool>(osf::DataType::Bool, true);
    roundtrip_abs_ts_one<std::int8_t>(osf::DataType::Int8, -7);
    roundtrip_abs_ts_one<std::int16_t>(osf::DataType::Int16, -2000);
    roundtrip_abs_ts_one<std::int32_t>(osf::DataType::Int32, -2'000'000);
    roundtrip_abs_ts_one<std::int64_t>(osf::DataType::Int64, -2'000'000'000LL);
    roundtrip_abs_ts_one<std::uint8_t>(osf::DataType::UInt8, 200);
    roundtrip_abs_ts_one<std::uint16_t>(osf::DataType::UInt16, 60000);
    roundtrip_abs_ts_one<std::uint32_t>(osf::DataType::UInt32, 4'000'000'000u);
    roundtrip_abs_ts_one<std::uint64_t>(osf::DataType::UInt64, 0xDEADBEEF'CAFEBABEull);
}

TEST(BlockEncodeRoundtripAbsTs, FloatDouble) {
    roundtrip_abs_ts_one<float>(osf::DataType::Float, 3.14f);
    roundtrip_abs_ts_one<double>(osf::DataType::Double, 2.71828);
}

TEST(BlockEncodeRoundtripAbsTs, MultiSampleInt32) {
    std::vector<std::uint8_t> out;
    std::int64_t const tss[] = {10, 20, 30, 40};
    std::int32_t const samples[] = {1, 2, 3, 4};
    auto r = encode_abs_timestamp_data<std::int32_t>(out, 0, 2, tss, samples, 4);
    ASSERT_TRUE(r.has_value());

    auto meta = one_channel_meta(osf::DataType::Int32,
                                 osf::ChannelType::Timestamped, 2);
    std::string s(reinterpret_cast<char const*>(out.data()), out.size());
    std::istringstream in(s);
    osf::BlockReader rdr(in, meta);
    auto block_opt = rdr.next();
    ASSERT_TRUE(block_opt.has_value());
    ASSERT_TRUE(block_opt->has_value());
    auto* ats = std::get_if<osf::AbsTimestampData>(&block_opt->value().kind);
    ASSERT_NE(ats, nullptr);
    auto* pairs = std::get_if<std::vector<std::pair<std::int64_t, std::int32_t>>>(
        &ats->samples);
    ASSERT_NE(pairs, nullptr);
    ASSERT_EQ(pairs->size(), 4u);
    EXPECT_EQ((*pairs)[2].first, 30);
    EXPECT_EQ((*pairs)[2].second, 3);
}

// ---------------------------------------------------------------------------
// Task 5: byte-exact tests for encode_abs_timestamp_data_gps
// ---------------------------------------------------------------------------

TEST(BlockEncodeGps, SingleSample_Frame) {
    std::vector<std::uint8_t> out;
    std::int64_t const ts = 100;
    GpsLocation const samples[] = {{47.5, 9.5, 400.0}};
    auto r = encode_abs_timestamp_data_gps(out, 0, 2, &ts, samples, 1);
    ASSERT_TRUE(r.has_value());

    // 1 (ctrl) + 8 (ts) + 24 (3 doubles) = 33. Frame = 37 bytes.
    ASSERT_EQ(out.size(), 37u);
    EXPECT_EQ(out[4], 0x08);                                  // bcAbsTimeStampData, bit-7=0
    EXPECT_TRUE(bytes_eq(out, 2, {0x21, 0x00}));              // len=33
}

TEST(BlockEncodeGps, MultiSample_Bit7Set) {
    std::vector<std::uint8_t> out;
    std::int64_t const tss[] = {1, 2};
    GpsLocation const samples[] = {{47.0, 9.0, 100.0}, {48.0, 10.0, 200.0}};
    auto r = encode_abs_timestamp_data_gps(out, 0, 2, tss, samples, 2);
    ASSERT_TRUE(r.has_value());

    // 1 (ctrl) + 4 (N) + 2 * 32 = 69. Frame = 73.
    ASSERT_EQ(out.size(), 73u);
    EXPECT_EQ(out[4], 0x88);                                  // bcAbsTimeStampData | bit-7
    EXPECT_TRUE(bytes_eq(out, 5, {0x02, 0x00, 0x00, 0x00}));
}

// ---------------------------------------------------------------------------
// Task 5: error-path + roundtrip tests for GPS encoder
// ---------------------------------------------------------------------------

TEST(BlockEncodeGps, ZeroCountInvalidArgument) {
    std::vector<std::uint8_t> out;
    std::int64_t ts = 0;
    GpsLocation s{0, 0, 0};
    auto r = encode_abs_timestamp_data_gps(out, 0, 2, &ts, &s, 0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
    EXPECT_TRUE(out.empty());
}

TEST(BlockEncodeRoundtripGps, SingleAndMulti) {
    auto encode_and_read = [](std::vector<std::int64_t> const& tss,
                              std::vector<GpsLocation> const& samples) {
        std::vector<std::uint8_t> out;
        auto r = encode_abs_timestamp_data_gps(
            out, 0, 2, tss.data(), samples.data(), samples.size());
        EXPECT_TRUE(r.has_value());

        auto meta = one_channel_meta(osf::DataType::GpsLocation,
                                     osf::ChannelType::Timestamped, 2);
        std::string s(reinterpret_cast<char const*>(out.data()), out.size());
        std::istringstream in(s);
        osf::BlockReader rdr(in, meta);
        auto block_opt = rdr.next();
        ASSERT_TRUE(block_opt.has_value());
        ASSERT_TRUE(block_opt->has_value());
        auto* ats = std::get_if<osf::AbsTimestampData>(&block_opt->value().kind);
        ASSERT_NE(ats, nullptr);
        auto* gps = std::get_if<std::vector<std::pair<std::int64_t, GpsLocation>>>(
            &ats->samples);
        ASSERT_NE(gps, nullptr);
        ASSERT_EQ(gps->size(), samples.size());
        for (std::size_t i = 0; i < samples.size(); ++i) {
            EXPECT_EQ((*gps)[i].first, tss[i]);
            EXPECT_DOUBLE_EQ((*gps)[i].second.latitude, samples[i].latitude);
            EXPECT_DOUBLE_EQ((*gps)[i].second.longitude, samples[i].longitude);
            EXPECT_DOUBLE_EQ((*gps)[i].second.altitude, samples[i].altitude);
        }
    };

    encode_and_read({100}, {{47.5, 9.5, 400.0}});
    encode_and_read({1, 2, 3}, {{47.0, 9.0, 100.0}, {47.5, 9.5, 200.0},
                                {48.0, 10.0, 300.0}});
}

// ---------------------------------------------------------------------------
// Task 6: byte-exact tests for encode_abs_timestamp_data(std::string_view)
// ---------------------------------------------------------------------------

TEST(BlockEncodeString, EmptyString_Frame) {
    std::vector<std::uint8_t> out;
    auto r = encode_abs_timestamp_data(out, /*ch=*/0, /*=2*/2,
                                       /*ts=*/0LL, std::string_view{});
    ASSERT_TRUE(r.has_value());

    // 1 (ctrl) + 8 (ts) + 0 (empty payload) = 9. Frame = 13.
    ASSERT_EQ(out.size(), 13u);
    EXPECT_EQ(out[4], 0x08);                              // bcAbsTimeStampData, bit-7=0
    EXPECT_TRUE(bytes_eq(out, 2, {0x09, 0x00}));          // len=9
}

TEST(BlockEncodeString, NonEmpty_NoTrailingZero) {
    std::vector<std::uint8_t> out;
    auto r = encode_abs_timestamp_data(out, 0, 2, 1LL,
                                       std::string_view{"hello"});
    ASSERT_TRUE(r.has_value());

    // 1 (ctrl) + 8 (ts) + 5 (payload) = 14. Frame = 18.
    ASSERT_EQ(out.size(), 18u);
    // Last 5 bytes are the payload — verify exact bytes and no
    // trailing 0x00 (spec rev 2026-05-24 OSF5 rule).
    EXPECT_EQ(out[13], 'h');
    EXPECT_EQ(out[14], 'e');
    EXPECT_EQ(out[15], 'l');
    EXPECT_EQ(out[16], 'l');
    EXPECT_EQ(out[17], 'o');
    // The byte AT position 18 would only exist if the encoder
    // appended a sentinel. The vector size assertion above
    // already rules that out, but make it explicit:
    EXPECT_EQ(out.size(), 18u) << "no trailing 0x00 may be appended";
}

TEST(BlockEncodeString, PayloadEndingInZeroByteIsPreserved) {
    // OSF5 is version-deterministic: even payloads that legitimately
    // end in 0x00 must be passed through verbatim.
    std::vector<std::uint8_t> out;
    char const data[] = {'a', '\x00', 'b'};  // 3 bytes, middle is zero
    auto r = encode_abs_timestamp_data(out, 0, 2, 1LL,
                                       std::string_view{data, sizeof(data)});
    ASSERT_TRUE(r.has_value());
    // 1 (ctrl) + 8 (ts) + 3 (payload) = 12. Frame = 16.
    ASSERT_EQ(out.size(), 16u);
    EXPECT_EQ(out[13], 'a');
    EXPECT_EQ(out[14], 0x00);
    EXPECT_EQ(out[15], 'b');
}

// ---------------------------------------------------------------------------
// Task 6: byte-exact tests for encode_abs_timestamp_data(BinarySample)
// ---------------------------------------------------------------------------

TEST(BlockEncodeBinary, NonEmpty_NoTrailingZero) {
    std::vector<std::uint8_t> out;
    std::uint8_t const data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    auto r = encode_abs_timestamp_data(out, 0, 2, 0LL,
                                       BinarySample{data, sizeof(data)});
    ASSERT_TRUE(r.has_value());

    // 1 (ctrl) + 8 (ts) + 4 (payload) = 13. Frame = 17.
    ASSERT_EQ(out.size(), 17u);
    EXPECT_EQ(out[4], 0x08);                              // bcAbsTimeStampData, bit-7=0
    EXPECT_TRUE(bytes_eq(out, 13, {0xDE, 0xAD, 0xBE, 0xEF}));
}

TEST(BlockEncodeBinary, FromVectorFactory) {
    std::vector<std::uint8_t> out;
    std::vector<std::uint8_t> v = {0x01, 0x02};
    auto r = encode_abs_timestamp_data(out, 0, 2, 0LL,
                                       BinarySample::from_vector(v));
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(out.size(), 15u);   // 2+2+11 = 15
    EXPECT_EQ(out[13], 0x01);
    EXPECT_EQ(out[14], 0x02);
}

// ---------------------------------------------------------------------------
// Task 6: error-path tests for variable-length encoder
// ---------------------------------------------------------------------------

TEST(BlockEncodeVariable, BadSizeofLengthValueInvalidArgument) {
    std::vector<std::uint8_t> out;
    auto r = encode_abs_timestamp_data(out, 0, /*=5*/5, 0LL,
                                       std::string_view{"x"});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
    EXPECT_TRUE(out.empty());
}

TEST(BlockEncodeVariable, OversizePayloadInvalidBlock) {
    // sizeoflengthvalue=2 implies payload_length <= 65535.
    // Body = 1 (ctrl) + 8 (ts) + payload-bytes. The boundary
    // payload size that just trips is 65527.
    std::vector<std::uint8_t> out;
    std::string big(65527, 'x');
    auto r = encode_abs_timestamp_data(out, 0, 2, 0LL,
                                       std::string_view{big});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidBlock);

    // One byte less fits.
    out.clear();
    std::string ok(65526, 'x');
    auto r2 = encode_abs_timestamp_data(out, 0, 2, 0LL,
                                        std::string_view{ok});
    EXPECT_TRUE(r2.has_value());
}

// ---------------------------------------------------------------------------
// Task 6: roundtrip tests via BlockReader for variable-length encoder
// ---------------------------------------------------------------------------

TEST(BlockEncodeRoundtripVariable, StringSingleSample) {
    std::vector<std::uint8_t> out;
    auto r = encode_abs_timestamp_data(out, 0, 2, 42LL,
                                       std::string_view{"hello"});
    ASSERT_TRUE(r.has_value());

    auto meta = one_channel_meta(osf::DataType::String,
                                 osf::ChannelType::Timestamped, 2);
    std::string s(reinterpret_cast<char const*>(out.data()), out.size());
    std::istringstream in(s);
    osf::BlockReader rdr(in, meta);
    auto block_opt = rdr.next();
    ASSERT_TRUE(block_opt.has_value());
    ASSERT_TRUE(block_opt->has_value());
    auto* ats = std::get_if<osf::AbsTimestampData>(&block_opt->value().kind);
    ASSERT_NE(ats, nullptr);
    auto* strs = std::get_if<std::vector<std::pair<std::int64_t,
                                                   std::string>>>(&ats->samples);
    ASSERT_NE(strs, nullptr);
    ASSERT_EQ(strs->size(), 1u);
    EXPECT_EQ((*strs)[0].first, 42);
    EXPECT_EQ((*strs)[0].second, "hello");
}

TEST(BlockEncodeRoundtripVariable, BinarySingleSample) {
    std::vector<std::uint8_t> out;
    std::vector<std::uint8_t> data = {0x00, 0x01, 0x00, 0xFF, 0x00};
    auto r = encode_abs_timestamp_data(out, 0, 2, 1LL,
                                       BinarySample::from_vector(data));
    ASSERT_TRUE(r.has_value());

    auto meta = one_channel_meta(osf::DataType::Binary,
                                 osf::ChannelType::Timestamped, 2);
    std::string s(reinterpret_cast<char const*>(out.data()), out.size());
    std::istringstream in(s);
    osf::BlockReader rdr(in, meta);
    auto block_opt = rdr.next();
    ASSERT_TRUE(block_opt.has_value());
    ASSERT_TRUE(block_opt->has_value());
    auto* ats = std::get_if<osf::AbsTimestampData>(&block_opt->value().kind);
    ASSERT_NE(ats, nullptr);
    auto* bins = std::get_if<std::vector<std::pair<std::int64_t,
                              std::vector<std::uint8_t>>>>(&ats->samples);
    ASSERT_NE(bins, nullptr);
    ASSERT_EQ(bins->size(), 1u);
    EXPECT_EQ((*bins)[0].first, 1);
    EXPECT_EQ((*bins)[0].second, data);  // including the 0x00 bytes
}
