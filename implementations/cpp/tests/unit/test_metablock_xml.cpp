// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Unit tests for parse_metablock_xml.
//
// Mirrors implementations/rust/osf-core/src/meta_xml.rs tests plus
// C++-specific edge cases (pointer/size buffer overload, string_view
// overload, malformed XML, null pointer with non-zero size).

#include <gtest/gtest.h>

#include <osf/osf.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

osf::Result<osf::MetaBlock> parse(std::string_view text) {
    return osf::parse_metablock_xml(text);
}

// ---------------------------------------------------------------------
// Happy-path coverage
// ---------------------------------------------------------------------

TEST(ParseMetablockXml, parses_minimal_metablock) {
    constexpr std::string_view body = R"(<?xml version="1.0" encoding="UTF-8"?>
<optimeas creator="test" created_utc="2026-05-05T00:00:00Z">
  <channels count="1">
    <channel index="0" name="a" channeltype="scalar"
             datatype="double" sizeoflengthvalue="2"/>
  </channels>
</optimeas>)";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->file_info.version, 4u);
    ASSERT_TRUE(r->file_info.creator.has_value());
    EXPECT_EQ(*r->file_info.creator, "test");
    ASSERT_TRUE(r->file_info.created_utc.has_value());
    EXPECT_EQ(*r->file_info.created_utc, "2026-05-05T00:00:00Z");
    ASSERT_EQ(r->channels.size(), 1u);
    EXPECT_EQ(r->channels[0].name, "a");
    EXPECT_EQ(r->channels[0].data_type, osf::DataType::Double);
    EXPECT_EQ(r->channels[0].data_type_raw, "double");
    EXPECT_EQ(r->channels[0].channel_type, osf::ChannelType::Scalar);
    EXPECT_EQ(r->channels[0].size_of_length_value, 2);
}

TEST(ParseMetablockXml, parses_full_channel_with_optional_attrs) {
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<optimeas creator="OSFGenerator/1.0" namespacesep="/" tag="demo" reason="GENERATOR">
  <channels count="1">
    <channel index="7" name="Sensor/T" channeltype="scalar"
             datatype="double" sizeoflengthvalue="4"
             timeincrement="1000000"
             mimetype="application/x-foo"
             physicalunit="C"
             physicaldimension="temperature"
             displayname="Temp"
             comment="main sensor"
             reference="ref-1"
             spectrumtype="realimag"/>
  </channels>
</optimeas>)";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->file_info.namespace_sep.value_or(""), "/");
    EXPECT_EQ(r->file_info.tag.value_or(""), "demo");
    EXPECT_EQ(r->file_info.reason.value_or(""), "GENERATOR");
    auto const& c = r->channels.at(0);
    EXPECT_EQ(c.index, 7);
    EXPECT_EQ(c.time_increment_ns.value_or(-1), 1000000);
    EXPECT_EQ(c.mime_type.value_or(""), "application/x-foo");
    EXPECT_EQ(c.physical_unit.value_or(""), "C");
    EXPECT_EQ(c.physical_dimension.value_or(""), "temperature");
    EXPECT_EQ(c.display_name.value_or(""), "Temp");
    EXPECT_EQ(c.comment.value_or(""), "main sensor");
    EXPECT_EQ(c.reference.value_or(""), "ref-1");
    ASSERT_TRUE(c.spectrum_type.has_value());
    EXPECT_EQ(*c.spectrum_type, osf::SpectrumType::RealImag);
}

TEST(ParseMetablockXml, bytearray_alias_normalises_to_binary) {
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<optimeas>
  <channels count="1">
    <channel index="0" name="a" channeltype="scalar"
             datatype="bytearray" sizeoflengthvalue="4"/>
  </channels>
</optimeas>)";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->channels[0].data_type, osf::DataType::Binary);
    EXPECT_EQ(r->channels[0].data_type_raw, "bytearray");
}

TEST(ParseMetablockXml, short_geolocation_spelling_accepted) {
    // OSFGenerator-style files use the short spelling without the
    // `created_at_` prefix. Real osf4_*.osf reference files do this.
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<optimeas longitude="11.5755" latitude="48.1374" altitude="519">
  <channels count="0"/>
</optimeas>)";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_TRUE(r->file_info.created_at_latitude.has_value());
    EXPECT_DOUBLE_EQ(*r->file_info.created_at_latitude, 48.1374);
    EXPECT_DOUBLE_EQ(r->file_info.created_at_longitude.value_or(0), 11.5755);
    EXPECT_DOUBLE_EQ(r->file_info.created_at_altitude.value_or(0), 519.0);
}

TEST(ParseMetablockXml, created_at_long_spelling_wins_over_short) {
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<optimeas latitude="1.0" created_at_latitude="2.0">
  <channels count="0"/>
</optimeas>)";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_TRUE(r->file_info.created_at_latitude.has_value());
    EXPECT_DOUBLE_EQ(*r->file_info.created_at_latitude, 2.0);
}

TEST(ParseMetablockXml, parses_infos_with_default_string_datatype) {
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<optimeas>
  <channels count="0"/>
  <infos>
    <info name="device" value="tester-1"/>
    <info name="n" datatype="int32" value="42"/>
  </infos>
</optimeas>)";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->infos.size(), 2u);
    EXPECT_EQ(r->infos[0].name, "device");
    EXPECT_EQ(r->infos[0].value, "tester-1");
    EXPECT_EQ(r->infos[0].data_type, osf::DataType::String);
    EXPECT_EQ(r->infos[1].data_type, osf::DataType::Int32);
}

TEST(ParseMetablockXml, deprecated_scale_offset_are_tolerated) {
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<optimeas>
  <channels count="1">
    <channel index="0" name="a" channeltype="scalar" datatype="double"
             sizeoflengthvalue="4" scale="1" offset="0"
             physicalunit1="V" physicaldimension1="voltage"/>
  </channels>
</optimeas>)";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->channels.size(), 1u);
    EXPECT_EQ(r->channels[0].size_of_length_value, 4);
    // The deprecated physicalunit1 attribute must NOT leak into
    // the supported physical_unit field.
    EXPECT_FALSE(r->channels[0].physical_unit.has_value());
}

TEST(ParseMetablockXml, unknown_channel_attribute_is_ignored) {
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<optimeas>
  <channels count="1">
    <channel index="0" name="a" channeltype="scalar" datatype="double"
             sizeoflengthvalue="2" cannode="0" some_future_attr="x"/>
  </channels>
</optimeas>)";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->channels.size(), 1u);
    EXPECT_EQ(r->channels[0].name, "a");
}

TEST(ParseMetablockXml, parses_multiple_channels_with_infos) {
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<optimeas creator="x">
  <channels count="2">
    <channel index="0" name="a" channeltype="scalar" datatype="int32"
             sizeoflengthvalue="2"/>
    <channel index="1" name="b" channeltype="scalar" datatype="double"
             sizeoflengthvalue="4" timeincrement="1000000"/>
  </channels>
  <infos>
    <info name="machine" datatype="string" value="press42"/>
  </infos>
</optimeas>)";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->channels.size(), 2u);
    EXPECT_EQ(r->channels[1].time_increment_ns.value_or(-1), 1'000'000);
    ASSERT_EQ(r->infos.size(), 1u);
    EXPECT_EQ(r->infos[0].name, "machine");
    EXPECT_EQ(r->infos[0].value, "press42");
}

TEST(ParseMetablockXml, count_attribute_mismatch_is_tolerated) {
    // Real-world OSF4 emitters declare count="N" but sometimes the
    // actual number of <channel> children differs. The Rust reference
    // parser ignores count; this parser must do the same to keep the
    // corpus parseable.
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<optimeas>
  <channels count="999">
    <channel index="0" name="a" channeltype="scalar" datatype="double"
             sizeoflengthvalue="2"/>
  </channels>
</optimeas>)";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->channels.size(), 1u);
}

// ---------------------------------------------------------------------
// Negative cases
// ---------------------------------------------------------------------

TEST(ParseMetablockXml, gpsdata_datatype_is_rejected) {
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<optimeas>
  <channels count="1">
    <channel index="0" name="a" channeltype="scalar" datatype="gpsdata"
             sizeoflengthvalue="4"/>
  </channels>
</optimeas>)";
    auto r = parse(body);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::RemovedInSpec);
}

TEST(ParseMetablockXml, wrong_root_element_is_rejected) {
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<not_optimeas/>)";
    auto r = parse(body);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidMetablock);
}

TEST(ParseMetablockXml, invalid_size_of_length_value_is_rejected) {
    for (auto bad : {0, 1, 3, 5, 8, 100}) {
        std::string body =
            R"(<?xml version="1.0"?>
<optimeas><channels count="1">
<channel index="0" name="a" channeltype="scalar" datatype="double"
         sizeoflengthvalue=")"
            + std::to_string(bad) + R"("/>
</channels></optimeas>)";
        auto r = parse(body);
        ASSERT_FALSE(r.has_value())
            << "sizeoflengthvalue=" << bad << " unexpectedly accepted";
        EXPECT_EQ(r.error().code, osf::Error::Code::InvalidMetablock);
    }
}

TEST(ParseMetablockXml, malformed_xml_returns_XmlParseError) {
    auto r = parse("<not-closed");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::XmlParseError);
}

TEST(ParseMetablockXml, empty_input_is_rejected_as_parse_error) {
    // pugixml treats an empty buffer as "no document" — a parse error.
    auto r = parse("");
    ASSERT_FALSE(r.has_value());
    // Either XmlParseError or InvalidMetablock is acceptable here;
    // both signal "no parsable content". Pin to XmlParseError because
    // that is what pugixml actually reports.
    EXPECT_EQ(r.error().code, osf::Error::Code::XmlParseError);
}

TEST(ParseMetablockXml, channel_missing_required_attribute_is_rejected) {
    // Each variant leaves one required attribute out; all must reject.
    constexpr std::string_view variants[] = {
        // missing index
        R"(<?xml version="1.0"?>
<optimeas><channels><channel name="a" channeltype="scalar"
             datatype="double" sizeoflengthvalue="2"/></channels></optimeas>)",
        // missing name
        R"(<?xml version="1.0"?>
<optimeas><channels><channel index="0" channeltype="scalar"
             datatype="double" sizeoflengthvalue="2"/></channels></optimeas>)",
        // missing channeltype
        R"(<?xml version="1.0"?>
<optimeas><channels><channel index="0" name="a"
             datatype="double" sizeoflengthvalue="2"/></channels></optimeas>)",
        // missing datatype
        R"(<?xml version="1.0"?>
<optimeas><channels><channel index="0" name="a" channeltype="scalar"
             sizeoflengthvalue="2"/></channels></optimeas>)",
        // missing sizeoflengthvalue
        R"(<?xml version="1.0"?>
<optimeas><channels><channel index="0" name="a" channeltype="scalar"
             datatype="double"/></channels></optimeas>)",
    };
    for (auto body : variants) {
        auto r = parse(body);
        ASSERT_FALSE(r.has_value()) << "accepted: " << body;
        EXPECT_EQ(r.error().code, osf::Error::Code::InvalidMetablock);
    }
}

TEST(ParseMetablockXml, channel_index_out_of_range_is_rejected) {
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<optimeas><channels><channel index="70000" name="a"
             channeltype="scalar" datatype="double"
             sizeoflengthvalue="2"/></channels></optimeas>)";
    auto r = parse(body);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidMetablock);
}

TEST(ParseMetablockXml, non_numeric_timeincrement_is_rejected) {
    constexpr std::string_view body = R"(<?xml version="1.0"?>
<optimeas><channels><channel index="0" name="a"
             channeltype="scalar" datatype="double"
             sizeoflengthvalue="2" timeincrement="not-a-number"/>
</channels></optimeas>)";
    auto r = parse(body);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidMetablock);
}

// ---------------------------------------------------------------------
// Overloads and edge cases
// ---------------------------------------------------------------------

TEST(ParseMetablockXml, buffer_and_string_view_overloads_agree) {
    std::string body =
        R"(<?xml version="1.0"?>
<optimeas><channels><channel index="1" name="x" channeltype="scalar"
             datatype="int32" sizeoflengthvalue="2"/></channels></optimeas>)";

    auto by_view = osf::parse_metablock_xml(std::string_view{body});
    auto by_buf  = osf::parse_metablock_xml(
        reinterpret_cast<std::uint8_t const*>(body.data()), body.size());

    ASSERT_TRUE(by_view.has_value()) << by_view.error().message;
    ASSERT_TRUE(by_buf.has_value())  << by_buf.error().message;
    ASSERT_EQ(by_view->channels.size(), by_buf->channels.size());
    EXPECT_EQ(by_view->channels[0].name, by_buf->channels[0].name);
    EXPECT_EQ(by_view->channels[0].index, by_buf->channels[0].index);
}

TEST(ParseMetablockXml, null_pointer_with_nonzero_size_rejected_as_invalid_arg) {
    auto r = osf::parse_metablock_xml(nullptr, 42);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

}  // namespace
