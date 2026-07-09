// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Unit tests for the OSF type-string parsers
// (parseDataType, parseChannelType, parseSpectrumType).
//
// Mirrors implementations/rust/osf-core/src/meta.rs tests.

#include <gtest/gtest.h>

#include <osf/osf.h>

namespace {

TEST(ParseDataType, all_current_spellings_parse) {
    EXPECT_EQ(*osf::parseDataType("bool"),        osf::DataType::Bool);
    EXPECT_EQ(*osf::parseDataType("int8"),        osf::DataType::Int8);
    EXPECT_EQ(*osf::parseDataType("int16"),       osf::DataType::Int16);
    EXPECT_EQ(*osf::parseDataType("int32"),       osf::DataType::Int32);
    EXPECT_EQ(*osf::parseDataType("int64"),       osf::DataType::Int64);
    EXPECT_EQ(*osf::parseDataType("uint8"),       osf::DataType::UInt8);
    EXPECT_EQ(*osf::parseDataType("uint16"),      osf::DataType::UInt16);
    EXPECT_EQ(*osf::parseDataType("uint32"),      osf::DataType::UInt32);
    EXPECT_EQ(*osf::parseDataType("uint64"),      osf::DataType::UInt64);
    EXPECT_EQ(*osf::parseDataType("float"),       osf::DataType::Float);
    EXPECT_EQ(*osf::parseDataType("double"),      osf::DataType::Double);
    EXPECT_EQ(*osf::parseDataType("string"),      osf::DataType::String);
    EXPECT_EQ(*osf::parseDataType("binary"),      osf::DataType::Binary);
    EXPECT_EQ(*osf::parseDataType("gpslocation"), osf::DataType::GpsLocation);
}

TEST(ParseDataType, bytearray_normalises_to_binary) {
    auto r = osf::parseDataType("bytearray");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, osf::DataType::Binary);
}

TEST(ParseDataType, gpsdata_is_rejected_with_replacement_hint) {
    auto r = osf::parseDataType("gpsdata");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::RemovedInSpec);
    EXPECT_NE(r.error().message.find("gpsdata"),     std::string::npos);
    EXPECT_NE(r.error().message.find("gpslocation"), std::string::npos);
}

TEST(ParseDataType, pair_triple_candata_are_rejected) {
    for (auto legacy : {"pair", "triple", "candata"}) {
        auto r = osf::parseDataType(legacy);
        ASSERT_FALSE(r.has_value()) << "expected error for " << legacy;
        EXPECT_EQ(r.error().code, osf::Error::Code::RemovedInSpec);
    }
}

TEST(ParseDataType, unknown_spelling_becomes_unsupported) {
    auto r = osf::parseDataType("future_type_xyz");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, osf::DataType::Unsupported);
}

TEST(ParseChannelType, current_spellings_parse) {
    EXPECT_EQ(*osf::parseChannelType("scalar"), osf::ChannelType::Scalar);
    EXPECT_EQ(*osf::parseChannelType("vector"), osf::ChannelType::Vector);
    EXPECT_EQ(*osf::parseChannelType("matrix"), osf::ChannelType::Matrix);
    EXPECT_EQ(*osf::parseChannelType("binary"), osf::ChannelType::Binary);
}

TEST(ParseChannelType, unknown_spelling_becomes_unsupported) {
    // A channeltype outside the spec set is Unsupported (kept, not dropped).
    // equidistant/timestamped are NOT channeltypes.
    for (auto const* s : {"tensor", "equidistant", "timestamped"}) {
        auto r = osf::parseChannelType(s);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(*r, osf::ChannelType::Unsupported) << s;
    }
}

TEST(ParseSpectrumType, current_spellings_parse) {
    EXPECT_EQ(osf::parseSpectrumType("amplitude"),   osf::SpectrumType::Amplitude);
    EXPECT_EQ(osf::parseSpectrumType("realimag"),    osf::SpectrumType::RealImag);
    EXPECT_EQ(osf::parseSpectrumType("ampphaserad"), osf::SpectrumType::AmpPhaseRad);
    EXPECT_EQ(osf::parseSpectrumType("ampphasedeg"), osf::SpectrumType::AmpPhaseDeg);
}

TEST(ParseSpectrumType, unknown_spelling_defaults_to_amplitude) {
    EXPECT_EQ(osf::parseSpectrumType("future_kind"), osf::SpectrumType::Amplitude);
    EXPECT_EQ(osf::parseSpectrumType(""),            osf::SpectrumType::Amplitude);
}

}  // namespace
