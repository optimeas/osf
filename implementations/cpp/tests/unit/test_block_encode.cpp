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

using osf::detail::BinarySample;
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
