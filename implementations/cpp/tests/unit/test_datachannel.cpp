// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Unit tests for the typed channel model in <osf/datachannel.h>.
// Mirrors implementations/rust/osf-core/src/data_channel.rs tests.

#include <gtest/gtest.h>

#include <osf/osf.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------
// NumericValues helpers
// ---------------------------------------------------------------------

TEST(NumericValues, data_type_is_consistent) {
    osf::NumericValues ints{std::vector<std::int32_t>{1, 2}};
    EXPECT_EQ(osf::numericValuesDataType(ints), osf::DataType::Int32);
    osf::NumericValues doubles{std::vector<double>{1.0}};
    EXPECT_EQ(osf::numericValuesDataType(doubles), osf::DataType::Double);
    osf::NumericValues gps{std::vector<osf::GpsLocation>{}};
    EXPECT_EQ(osf::numericValuesDataType(gps), osf::DataType::GpsLocation);
}

TEST(NumericValues, empty_for_returns_nullopt_for_variable_and_unsupported) {
    EXPECT_FALSE(osf::numericValuesEmptyFor(osf::DataType::String));
    EXPECT_FALSE(osf::numericValuesEmptyFor(osf::DataType::Binary));
    EXPECT_FALSE(osf::numericValuesEmptyFor(osf::DataType::ByteArray));
    EXPECT_FALSE(osf::numericValuesEmptyFor(osf::DataType::Unsupported));
    EXPECT_TRUE(osf::numericValuesEmptyFor(osf::DataType::Double));
    EXPECT_TRUE(osf::numericValuesEmptyFor(osf::DataType::GpsLocation));
}

// ---------------------------------------------------------------------
// EquidistantChannel::samplesVector
// ---------------------------------------------------------------------

osf::EquidistantChannel makeEq(osf::NumericValues samples,
                                std::vector<osf::Segment> segments) {
    osf::EquidistantChannel c;
    c.index = 0;
    c.name = "test";
    c.dataType = osf::numericValuesDataType(samples);
    c.samples = std::move(samples);
    c.segments = std::move(segments);
    return c;
}

TEST(EquidistantChannel, samples_with_time_single_segment) {
    auto c = makeEq(
        osf::NumericValues{std::vector<double>{10.0, 20.0, 30.0, 40.0}},
        {{1'000'000'000, 1.0, 0, 4}});
    auto samples = c.samplesVector();
    ASSERT_EQ(samples.size(), 4u);
    EXPECT_EQ(samples[0].timestampNs, 1'000'000'000);
    EXPECT_DOUBLE_EQ(std::get<double>(samples[0].value), 10.0);
    EXPECT_EQ(samples[1].timestampNs, 2'000'000'000);
    EXPECT_EQ(samples[2].timestampNs, 3'000'000'000);
    EXPECT_EQ(samples[3].timestampNs, 4'000'000'000);
    EXPECT_DOUBLE_EQ(std::get<double>(samples[3].value), 40.0);
}

TEST(EquidistantChannel, samples_with_time_three_segments_no_interpolation) {
    auto c = makeEq(
        osf::NumericValues{
            std::vector<std::int32_t>{1, 2, 3, 100, 101, 200, 201}},
        {
            {1'000,         1000.0, 0, 3},
            {1'000'000'000, 1000.0, 3, 2},
            {5'000'000'000, 1000.0, 5, 2},
        });
    auto samples = c.samplesVector();
    ASSERT_EQ(samples.size(), 7u);
    EXPECT_EQ(samples[0].timestampNs, 1'000);
    EXPECT_EQ(std::get<std::int32_t>(samples[0].value), 1);
    // Gaps between segments must NOT be interpolated.
    EXPECT_EQ(samples[3].timestampNs, 1'000'000'000);
    EXPECT_EQ(std::get<std::int32_t>(samples[3].value), 100);
    EXPECT_EQ(samples[5].timestampNs, 5'000'000'000);
    EXPECT_EQ(std::get<std::int32_t>(samples[5].value), 200);
}

TEST(EquidistantChannel, empty_channel_iteration_yields_nothing) {
    auto c = makeEq(osf::NumericValues{std::vector<double>{}}, {});
    auto samples = c.samplesVector();
    EXPECT_TRUE(samples.empty());
    EXPECT_TRUE(osf::numericValuesEmpty(c.samples));
}

TEST(EquidistantChannel, flat_access_mismatch_returns_typed_error) {
    auto c = makeEq(osf::NumericValues{std::vector<std::int32_t>{1, 2, 3}},
                     {{0, 1.0, 0, 3}});
    // Wrong type → mismatch.
    auto wrong = osf::asDoublesFlat(c);
    ASSERT_FALSE(wrong.has_value());
    EXPECT_EQ(wrong.error().code, osf::Error::Code::DataTypeMismatch);
    // Matching access works.
    auto right = osf::asInt32Flat(c);
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(*right, (std::vector<std::int32_t>{1, 2, 3}));
}

// ---------------------------------------------------------------------
// TimestampedChannel::samplesVector
// ---------------------------------------------------------------------

TEST(TimestampedChannel, samples_with_time_pairs_correctly) {
    osf::TimestampedChannel c;
    c.index = 0;
    c.name = "ts";
    c.dataType = osf::DataType::Int32;
    c.timestampsNs = {100, 200, 300};
    c.values = osf::NumericValues{std::vector<std::int32_t>{10, 20, 30}};

    auto samples = c.samplesVector();
    ASSERT_EQ(samples.size(), 3u);
    EXPECT_EQ(samples[0].timestampNs, 100);
    EXPECT_EQ(std::get<std::int32_t>(samples[0].value), 10);
    EXPECT_EQ(samples[2].timestampNs, 300);
    EXPECT_EQ(std::get<std::int32_t>(samples[2].value), 30);

    auto flat = osf::asInt32Flat(c);
    ASSERT_TRUE(flat.has_value());
    EXPECT_EQ(*flat,
              (std::vector<std::pair<std::int64_t, std::int32_t>>{
                  {100, 10}, {200, 20}, {300, 30}}));
}

// ---------------------------------------------------------------------
// VariableChannel
// ---------------------------------------------------------------------

TEST(VariableChannel, string_iteration_collects_values) {
    osf::VariableChannel c;
    c.index = 0;
    c.name = "msg";
    c.dataType = osf::DataType::String;
    c.timestampsNs = {1, 2};
    c.stringValues = std::vector<std::string>{"hi", "bye"};

    auto samples = c.samplesVector();
    ASSERT_EQ(samples.size(), 2u);
    EXPECT_EQ(samples[0].timestampNs, 1);
    EXPECT_EQ(samples[0].value.kind, osf::VariableValueRef::Kind::String);
    EXPECT_EQ(samples[0].value.stringValue, "hi");
    EXPECT_EQ(samples[1].value.stringValue, "bye");

    auto strings = c.asStrings();
    ASSERT_TRUE(strings.has_value());
    EXPECT_EQ(**strings,
              (std::vector<std::string>{"hi", "bye"}));

    auto binaries = c.asBinaries();
    ASSERT_FALSE(binaries.has_value());
    EXPECT_EQ(binaries.error().code, osf::Error::Code::DataTypeMismatch);
}

TEST(VariableChannel, binary_iteration_collects_values) {
    osf::VariableChannel c;
    c.index = 1;
    c.name = "blob";
    c.dataType = osf::DataType::Binary;
    c.timestampsNs = {10, 20};
    c.binaryValues = std::vector<std::vector<std::uint8_t>>{
        {0xFF, 0xD8}, {0x89, 0x50}};

    auto samples = c.samplesVector();
    ASSERT_EQ(samples.size(), 2u);
    EXPECT_EQ(samples[0].value.kind, osf::VariableValueRef::Kind::Binary);
    EXPECT_EQ(samples[0].value.binaryValue,
              (std::vector<std::uint8_t>{0xFF, 0xD8}));

    auto binaries = c.asBinaries();
    ASSERT_TRUE(binaries.has_value());
    EXPECT_EQ((**binaries)[1], (std::vector<std::uint8_t>{0x89, 0x50}));
}

// ---------------------------------------------------------------------
// Common Channel (variant) accessors
// ---------------------------------------------------------------------

TEST(DataChannel, common_accessors_work_for_each_variant) {
    auto eq = makeEq(osf::NumericValues{std::vector<double>{1, 2, 3}},
                      {{0, 1.0, 0, 3}});
    osf::DataChannel dc{std::move(eq)};
    EXPECT_EQ(osf::channelIndex(dc), 0);
    EXPECT_EQ(osf::channelName(dc), "test");
    EXPECT_EQ(osf::channelDataType(dc), osf::DataType::Double);
    EXPECT_EQ(osf::channelSampleCount(dc), 3u);
    EXPECT_FALSE(osf::channelIsEmpty(dc));

    osf::TimestampedChannel ts;
    ts.index = 1;
    ts.name = "ts";
    ts.dataType = osf::DataType::Int32;
    ts.physicalUnit = "V";
    ts.timestampsNs = {10, 20};
    ts.values = osf::NumericValues{std::vector<std::int32_t>{1, 2}};
    osf::DataChannel dc2{std::move(ts)};
    EXPECT_EQ(osf::channelPhysicalUnit(dc2).value_or(""), "V");
    EXPECT_EQ(osf::channelSampleCount(dc2), 2u);

    osf::VariableChannel var;
    var.index = 2;
    var.name = "msg";
    var.dataType = osf::DataType::String;
    var.timestampsNs = {100};
    var.stringValues = std::vector<std::string>{"hello"};
    osf::DataChannel dc3{std::move(var)};
    EXPECT_EQ(osf::channelSampleCount(dc3), 1u);
    EXPECT_EQ(osf::channelDataType(dc3), osf::DataType::String);
}

}  // namespace
