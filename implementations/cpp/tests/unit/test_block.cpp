// SPDX-License-Identifier: MIT
//
// Unit tests for the block-model primitives in <osf/block.hpp>:
// payload length helpers, control-byte decoder, GpsLocation equality.

#include <gtest/gtest.h>

#include <osf/osf.hpp>

#include <cstdint>
#include <vector>

namespace {

// ---------------------------------------------------------------------
// NumericPayload / TimestampedPayload / RelTimestampedPayload len()
// ---------------------------------------------------------------------

TEST(BlockPayloads, numeric_payload_len_works_for_each_variant) {
    osf::NumericPayload bool_run{std::vector<bool>{true, false}};
    EXPECT_EQ(osf::numeric_payload_len(bool_run), 2u);

    osf::NumericPayload doubles{std::vector<double>(7, 1.0)};
    EXPECT_EQ(osf::numeric_payload_len(doubles), 7u);

    osf::NumericPayload empty_i32{std::vector<std::int32_t>{}};
    EXPECT_TRUE(osf::numeric_payload_empty(empty_i32));
}

TEST(BlockPayloads, timestamped_payload_len_covers_string_binary_gps) {
    using P = osf::TimestampedPayload;
    P strings{std::vector<std::pair<std::int64_t, std::string>>{{0, "x"}}};
    EXPECT_EQ(osf::timestamped_payload_len(strings), 1u);

    P binaries{std::vector<std::pair<std::int64_t, std::vector<std::uint8_t>>>(
        4, {1, std::vector<std::uint8_t>{1, 2, 3}})};
    EXPECT_EQ(osf::timestamped_payload_len(binaries), 4u);

    P empty_double{std::vector<std::pair<std::int64_t, double>>{}};
    EXPECT_TRUE(osf::timestamped_payload_empty(empty_double));

    P gps{std::vector<std::pair<std::int64_t, osf::GpsLocation>>{
        {1, osf::GpsLocation{1, 2, 3}},
        {2, osf::GpsLocation{4, 5, 6}}}};
    EXPECT_EQ(osf::timestamped_payload_len(gps), 2u);
}

TEST(BlockPayloads, rel_timestamped_payload_len_works) {
    osf::RelTimestampedPayload p{
        std::vector<std::pair<std::uint32_t, float>>{{100, 1.0f}, {200, 2.0f}}};
    EXPECT_EQ(osf::rel_timestamped_payload_len(p), 2u);

    osf::RelTimestampedPayload empty{
        std::vector<std::pair<std::uint32_t, bool>>{}};
    EXPECT_TRUE(osf::rel_timestamped_payload_empty(empty));
}

// ---------------------------------------------------------------------
// Control-byte decoder
// ---------------------------------------------------------------------

TEST(ControlByte, decodes_all_documented_values) {
    struct Case {
        std::uint8_t byte;
        osf::ControlKind kind;
    };
    Case const cases[] = {
        {0x00, osf::ControlKind::Reserved},
        {0x01, osf::ControlKind::TrustedTimestamp},
        {0x02, osf::ControlKind::TimebaseRealign},
        {0x03, osf::ControlKind::StatusEvent},
        {0x04, osf::ControlKind::MessageEvent},
        {0x05, osf::ControlKind::ContinuedData},
        {0x06, osf::ControlKind::StartData},
        {0x07, osf::ControlKind::ContinuedRelStampData},
        {0x08, osf::ControlKind::AbsTimeStampData},
    };
    for (auto const& c : cases) {
        auto cb = osf::decode_control_byte(c.byte);
        EXPECT_EQ(cb.kind, c.kind) << "byte 0x" << std::hex << int{c.byte};
        EXPECT_FALSE(cb.multi_sample) << "byte 0x" << std::hex << int{c.byte};
        EXPECT_EQ(cb.raw, c.byte) << "raw mismatch";
    }
}

TEST(ControlByte, recognises_multi_sample_bit) {
    for (int low = 0; low <= 8; ++low) {
        auto cb = osf::decode_control_byte(static_cast<std::uint8_t>(low | 0x80));
        EXPECT_TRUE(cb.multi_sample)
            << "byte 0x" << std::hex << (low | 0x80) << " should be multi";
    }
}

TEST(ControlByte, passes_unknown_values_through) {
    auto cb = osf::decode_control_byte(0x09);
    EXPECT_EQ(cb.kind, osf::ControlKind::Unknown);
    EXPECT_EQ(cb.raw, 0x09);

    cb = osf::decode_control_byte(0x7F);
    EXPECT_EQ(cb.kind, osf::ControlKind::Unknown);
    EXPECT_EQ(cb.raw, 0x7F);

    // High bit must not contaminate the kind.
    cb = osf::decode_control_byte(0x89);
    EXPECT_EQ(cb.kind, osf::ControlKind::Unknown);
    EXPECT_EQ(cb.raw, 0x09);
    EXPECT_TRUE(cb.multi_sample);
}

// ---------------------------------------------------------------------
// GpsLocation equality
// ---------------------------------------------------------------------

TEST(GpsLocation, equality_compares_all_three_fields) {
    osf::GpsLocation const a{1.0, 2.0, 3.0};
    osf::GpsLocation const b{1.0, 2.0, 3.0};
    osf::GpsLocation const c{1.0, 2.0, 4.0};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}

// ---------------------------------------------------------------------
// Skipped block default payload
// ---------------------------------------------------------------------

TEST(Block, skipped_default_payload_is_nullopt) {
    osf::Block blk;
    blk.channel_index = 7;
    osf::Skipped sk;
    sk.reason = osf::SkipReason{osf::SkipReason::Kind::ReservedBlockType, 0};
    sk.bytes_skipped = 1;
    blk.kind = std::move(sk);

    auto const& as_skipped = std::get<osf::Skipped>(blk.kind);
    EXPECT_FALSE(as_skipped.payload.has_value());
    EXPECT_EQ(as_skipped.reason.raw_byte, 0);
}

}  // namespace
