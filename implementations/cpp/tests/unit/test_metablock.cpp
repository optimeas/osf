// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for parse_metablock_json.
//
// Mirrors implementations/rust/osf-core/src/meta_json.rs tests plus
// C++-specific edge cases (pointer/size buffer overload, string_view
// overload, malformed JSON, null pointer with non-zero size).

#include <gtest/gtest.h>

#include <osf/osf.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

osf::Result<osf::MetaBlock> parse(std::string_view text) {
    return osf::parse_metablock_json(text);
}

// ---------------------------------------------------------------------
// Happy-path coverage
// ---------------------------------------------------------------------

TEST(ParseMetablockJson, parses_minimal_metablock) {
    constexpr std::string_view body = R"({
      "osf": {
        "format": "osf5",
        "version": 5,
        "file": { "creator": "test" },
        "channels": [
          { "index": 0, "name": "a", "channeltype": "scalar",
            "datatype": "double", "sizeoflengthvalue": 2 }
        ]
      }
    })";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->file_info.version, 5u);
    ASSERT_TRUE(r->file_info.creator.has_value());
    EXPECT_EQ(*r->file_info.creator, "test");
    ASSERT_EQ(r->channels.size(), 1u);
    EXPECT_EQ(r->channels[0].name, "a");
    EXPECT_EQ(r->channels[0].data_type, osf::DataType::Double);
    EXPECT_EQ(r->channels[0].data_type_raw, "double");
    EXPECT_EQ(r->channels[0].channel_type, osf::ChannelType::Scalar);
    EXPECT_EQ(r->channels[0].size_of_length_value, 2);
}

TEST(ParseMetablockJson, parses_full_channel_with_optional_fields) {
    constexpr std::string_view body = R"({
      "osf": {
        "version": 5,
        "channels": [
          { "index": 7, "name": "Sensor.T", "channeltype": "scalar",
            "datatype": "double", "sizeoflengthvalue": 4,
            "timeincrement": 1000000,
            "mimetype": "application/x-foo",
            "physicalunit": "°C",
            "physicaldimension": "temperature",
            "displayname": "Temp",
            "comment": "main sensor",
            "reference": "ref-1",
            "spectrumtype": "realimag" }
        ]
      }
    })";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    auto const& c = r->channels.at(0);
    EXPECT_EQ(c.index, 7);
    EXPECT_EQ(c.time_increment_ns.value_or(-1), 1000000);
    EXPECT_EQ(c.mime_type.value_or(""), "application/x-foo");
    EXPECT_EQ(c.physical_unit.value_or(""), "\xc2\xb0""C");
    EXPECT_EQ(c.physical_dimension.value_or(""), "temperature");
    EXPECT_EQ(c.display_name.value_or(""), "Temp");
    EXPECT_EQ(c.comment.value_or(""), "main sensor");
    EXPECT_EQ(c.reference.value_or(""), "ref-1");
    ASSERT_TRUE(c.spectrum_type.has_value());
    EXPECT_EQ(*c.spectrum_type, osf::SpectrumType::RealImag);
}

TEST(ParseMetablockJson, timeincrement_zero_is_kept_explicitly) {
    constexpr std::string_view body = R"({
      "osf": { "version": 5,
        "channels": [
          { "index":0,"name":"a","channeltype":"scalar",
            "datatype":"double","sizeoflengthvalue":2,
            "timeincrement":0 }
        ]
      }
    })";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_TRUE(r->channels[0].time_increment_ns.has_value());
    EXPECT_EQ(*r->channels[0].time_increment_ns, 0);
}

TEST(ParseMetablockJson, bytearray_alias_normalises_to_binary) {
    constexpr std::string_view body = R"({
      "osf": { "version": 5,
        "channels": [
          { "index":0,"name":"a","channeltype":"scalar",
            "datatype":"bytearray","sizeoflengthvalue":4 }
        ]
      }
    })";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->channels[0].data_type, osf::DataType::Binary);
    EXPECT_EQ(r->channels[0].data_type_raw, "bytearray");
}

TEST(ParseMetablockJson, short_geolocation_spelling_accepted) {
    constexpr std::string_view body = R"({
      "osf": { "version": 5,
        "file": { "latitude": 47.5, "longitude": 13.0, "altitude": 800.0 },
        "channels": []
      }
    })";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_TRUE(r->file_info.created_at_latitude.has_value());
    EXPECT_DOUBLE_EQ(*r->file_info.created_at_latitude, 47.5);
    EXPECT_DOUBLE_EQ(r->file_info.created_at_longitude.value_or(0), 13.0);
    EXPECT_DOUBLE_EQ(r->file_info.created_at_altitude.value_or(0), 800.0);
}

TEST(ParseMetablockJson, parses_infos_with_default_string_datatype) {
    constexpr std::string_view body = R"({
      "osf": { "version": 5,
        "channels": [],
        "infos": [
          { "name": "device", "value": "tester-1" },
          { "name": "n",      "value": "42", "datatype": "int32" }
        ]
      }
    })";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->infos.size(), 2u);
    EXPECT_EQ(r->infos[0].name, "device");
    EXPECT_EQ(r->infos[0].value, "tester-1");
    EXPECT_EQ(r->infos[0].data_type, osf::DataType::String);
    EXPECT_EQ(r->infos[1].data_type, osf::DataType::Int32);
}

TEST(ParseMetablockJson, unknown_top_level_field_is_ignored) {
    constexpr std::string_view body = R"({
      "osf": { "version": 5, "channels": [],
        "future_extension": "ignored"
      }
    })";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_TRUE(r->channels.empty());
}

TEST(ParseMetablockJson, deprecated_scale_field_is_tolerated) {
    constexpr std::string_view body = R"({
      "osf": { "version": 5,
        "channels": [
          { "index":0,"name":"a","channeltype":"scalar",
            "datatype":"double","sizeoflengthvalue":2,
            "scale":1.0,"offset":0.0,
            "physicalunit1":"V","physicaldimension1":"voltage" }
        ]
      }
    })";
    auto r = parse(body);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->channels.size(), 1u);
    // physicalunit (singular) was not set in the body; the deprecated
    // physicalunit1 must not leak through to the supported field.
    EXPECT_FALSE(r->channels[0].physical_unit.has_value());
}

// ---------------------------------------------------------------------
// Negative cases
// ---------------------------------------------------------------------

TEST(ParseMetablockJson, gpsdata_datatype_is_rejected) {
    constexpr std::string_view body = R"({
      "osf": { "version": 5,
        "channels": [
          { "index":0,"name":"a","channeltype":"scalar",
            "datatype":"gpsdata","sizeoflengthvalue":4 }
        ]
      }
    })";
    auto r = parse(body);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::RemovedInSpec);
}

TEST(ParseMetablockJson, missing_osf_envelope_is_rejected) {
    constexpr std::string_view body =
        R"({"version":5,"channels":[]})";
    auto r = parse(body);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidMetablock);
}

TEST(ParseMetablockJson, invalid_size_of_length_value_is_rejected) {
    for (auto bad : {0, 1, 3, 5, 8, 100}) {
        std::string body =
            R"({"osf":{"version":5,"channels":[
              {"index":0,"name":"a","channeltype":"scalar",
               "datatype":"double","sizeoflengthvalue":)"
            + std::to_string(bad) + R"(}]}})";
        auto r = parse(body);
        ASSERT_FALSE(r.has_value())
            << "sizeoflengthvalue=" << bad << " unexpectedly accepted";
        EXPECT_EQ(r.error().code, osf::Error::Code::InvalidMetablock);
    }
}

TEST(ParseMetablockJson, malformed_json_returns_JsonParseError) {
    auto r = parse("not-a-json");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::JsonParseError);
}

TEST(ParseMetablockJson, non_object_root_is_rejected) {
    auto r = parse("[1,2,3]");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidMetablock);
}

TEST(ParseMetablockJson, channels_not_an_array_is_rejected) {
    auto r = parse(R"({"osf":{"version":5,"channels":"oops"}})");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidMetablock);
}

TEST(ParseMetablockJson, infos_not_an_array_is_rejected) {
    auto r = parse(R"({"osf":{"version":5,"channels":[],"infos":"oops"}})");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidMetablock);
}

TEST(ParseMetablockJson, channel_missing_required_field_is_rejected) {
    // Each entry leaves one required field out; all must reject.
    constexpr std::string_view variants[] = {
        // missing index
        R"({"osf":{"version":5,"channels":[
            {"name":"a","channeltype":"scalar","datatype":"double","sizeoflengthvalue":2}]}})",
        // missing name
        R"({"osf":{"version":5,"channels":[
            {"index":0,"channeltype":"scalar","datatype":"double","sizeoflengthvalue":2}]}})",
        // missing channeltype
        R"({"osf":{"version":5,"channels":[
            {"index":0,"name":"a","datatype":"double","sizeoflengthvalue":2}]}})",
        // missing datatype
        R"({"osf":{"version":5,"channels":[
            {"index":0,"name":"a","channeltype":"scalar","sizeoflengthvalue":2}]}})",
        // missing sizeoflengthvalue
        R"({"osf":{"version":5,"channels":[
            {"index":0,"name":"a","channeltype":"scalar","datatype":"double"}]}})",
    };
    for (auto body : variants) {
        auto r = parse(body);
        ASSERT_FALSE(r.has_value()) << "accepted: " << body;
        EXPECT_EQ(r.error().code, osf::Error::Code::InvalidMetablock);
    }
}

TEST(ParseMetablockJson, channel_index_out_of_range_is_rejected) {
    constexpr std::string_view body = R"({
      "osf": { "version": 5,
        "channels": [
          { "index":70000,"name":"a","channeltype":"scalar",
            "datatype":"double","sizeoflengthvalue":2 }
        ]
      }
    })";
    auto r = parse(body);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidMetablock);
}

// ---------------------------------------------------------------------
// Overloads and edge cases
// ---------------------------------------------------------------------

TEST(ParseMetablockJson, buffer_and_string_view_overloads_agree) {
    std::string body =
        R"({"osf":{"version":5,"channels":[
            {"index":1,"name":"x","channeltype":"scalar",
             "datatype":"int32","sizeoflengthvalue":2}]}})";

    auto by_view = osf::parse_metablock_json(std::string_view{body});
    auto by_buf  = osf::parse_metablock_json(
        reinterpret_cast<std::uint8_t const*>(body.data()), body.size());

    ASSERT_TRUE(by_view.has_value()) << by_view.error().message;
    ASSERT_TRUE(by_buf.has_value())  << by_buf.error().message;
    ASSERT_EQ(by_view->channels.size(), by_buf->channels.size());
    EXPECT_EQ(by_view->channels[0].name, by_buf->channels[0].name);
    EXPECT_EQ(by_view->channels[0].index, by_buf->channels[0].index);
}

TEST(ParseMetablockJson, null_pointer_with_zero_size_returns_parse_error) {
    // Empty input is not valid JSON; the discarded-result path fires.
    // (Test exercises the null+size=0 branch reaching the parser.)
    auto r = osf::parse_metablock_json(nullptr, 0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::JsonParseError);
}

TEST(ParseMetablockJson, null_pointer_with_nonzero_size_rejected_as_invalid_arg) {
    auto r = osf::parse_metablock_json(nullptr, 42);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

}  // namespace
