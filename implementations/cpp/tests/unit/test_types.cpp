// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the OSF type-string parsers
// (parse_data_type, parse_channel_type, parse_spectrum_type).
//
// Mirrors implementations/rust/osf-core/src/meta.rs tests.

#include <gtest/gtest.h>

#include <osf/osf.hpp>

namespace {

TEST(ParseDataType, all_current_spellings_parse) {
    EXPECT_EQ(*osf::parse_data_type("bool"),        osf::DataType::Bool);
    EXPECT_EQ(*osf::parse_data_type("int8"),        osf::DataType::Int8);
    EXPECT_EQ(*osf::parse_data_type("int16"),       osf::DataType::Int16);
    EXPECT_EQ(*osf::parse_data_type("int32"),       osf::DataType::Int32);
    EXPECT_EQ(*osf::parse_data_type("int64"),       osf::DataType::Int64);
    EXPECT_EQ(*osf::parse_data_type("uint8"),       osf::DataType::UInt8);
    EXPECT_EQ(*osf::parse_data_type("uint16"),      osf::DataType::UInt16);
    EXPECT_EQ(*osf::parse_data_type("uint32"),      osf::DataType::UInt32);
    EXPECT_EQ(*osf::parse_data_type("uint64"),      osf::DataType::UInt64);
    EXPECT_EQ(*osf::parse_data_type("float"),       osf::DataType::Float);
    EXPECT_EQ(*osf::parse_data_type("double"),      osf::DataType::Double);
    EXPECT_EQ(*osf::parse_data_type("string"),      osf::DataType::String);
    EXPECT_EQ(*osf::parse_data_type("binary"),      osf::DataType::Binary);
    EXPECT_EQ(*osf::parse_data_type("gpslocation"), osf::DataType::GpsLocation);
}

TEST(ParseDataType, bytearray_normalises_to_binary) {
    auto r = osf::parse_data_type("bytearray");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, osf::DataType::Binary);
}

TEST(ParseDataType, gpsdata_is_rejected_with_replacement_hint) {
    auto r = osf::parse_data_type("gpsdata");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::RemovedInSpec);
    EXPECT_NE(r.error().message.find("gpsdata"),     std::string::npos);
    EXPECT_NE(r.error().message.find("gpslocation"), std::string::npos);
}

TEST(ParseDataType, pair_triple_candata_are_rejected) {
    for (auto legacy : {"pair", "triple", "candata"}) {
        auto r = osf::parse_data_type(legacy);
        ASSERT_FALSE(r.has_value()) << "expected error for " << legacy;
        EXPECT_EQ(r.error().code, osf::Error::Code::RemovedInSpec);
    }
}

TEST(ParseDataType, unknown_spelling_becomes_unsupported) {
    auto r = osf::parse_data_type("future_type_xyz");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, osf::DataType::Unsupported);
}

TEST(ParseChannelType, current_spellings_parse) {
    EXPECT_EQ(*osf::parse_channel_type("scalar"),      osf::ChannelType::Scalar);
    EXPECT_EQ(*osf::parse_channel_type("equidistant"), osf::ChannelType::Equidistant);
    EXPECT_EQ(*osf::parse_channel_type("timestamped"), osf::ChannelType::Timestamped);
}

TEST(ParseChannelType, unknown_spelling_becomes_unsupported) {
    auto r = osf::parse_channel_type("vector");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, osf::ChannelType::Unsupported);
}

TEST(ParseSpectrumType, current_spellings_parse) {
    EXPECT_EQ(osf::parse_spectrum_type("amplitude"),   osf::SpectrumType::Amplitude);
    EXPECT_EQ(osf::parse_spectrum_type("realimag"),    osf::SpectrumType::RealImag);
    EXPECT_EQ(osf::parse_spectrum_type("ampphaserad"), osf::SpectrumType::AmpPhaseRad);
    EXPECT_EQ(osf::parse_spectrum_type("ampphasedeg"), osf::SpectrumType::AmpPhaseDeg);
}

TEST(ParseSpectrumType, unknown_spelling_defaults_to_amplitude) {
    EXPECT_EQ(osf::parse_spectrum_type("future_kind"), osf::SpectrumType::Amplitude);
    EXPECT_EQ(osf::parse_spectrum_type(""),            osf::SpectrumType::Amplitude);
}

}  // namespace
