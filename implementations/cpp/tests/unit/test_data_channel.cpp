// SPDX-License-Identifier: MIT
//
// Unit tests for the typed channel model in <osf/data_channel.hpp>.
// Mirrors implementations/rust/osf-core/src/data_channel.rs tests.

#include <gtest/gtest.h>

#include <osf/osf.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------
// NumericValues helpers
// ---------------------------------------------------------------------

TEST(NumericValues, data_type_is_consistent) {
    osf::NumericValues ints{std::vector<std::int32_t>{1, 2}};
    EXPECT_EQ(osf::numeric_values_data_type(ints), osf::DataType::Int32);
    osf::NumericValues doubles{std::vector<double>{1.0}};
    EXPECT_EQ(osf::numeric_values_data_type(doubles), osf::DataType::Double);
    osf::NumericValues gps{std::vector<osf::GpsLocation>{}};
    EXPECT_EQ(osf::numeric_values_data_type(gps), osf::DataType::GpsLocation);
}

TEST(NumericValues, empty_for_returns_nullopt_for_variable_and_unsupported) {
    EXPECT_FALSE(osf::numeric_values_empty_for(osf::DataType::String));
    EXPECT_FALSE(osf::numeric_values_empty_for(osf::DataType::Binary));
    EXPECT_FALSE(osf::numeric_values_empty_for(osf::DataType::ByteArray));
    EXPECT_FALSE(osf::numeric_values_empty_for(osf::DataType::Unsupported));
    EXPECT_TRUE(osf::numeric_values_empty_for(osf::DataType::Double));
    EXPECT_TRUE(osf::numeric_values_empty_for(osf::DataType::GpsLocation));
}

// ---------------------------------------------------------------------
// EquidistantChannel::samples_vector
// ---------------------------------------------------------------------

osf::EquidistantChannel make_eq(osf::NumericValues samples,
                                std::vector<osf::Segment> segments) {
    osf::EquidistantChannel c;
    c.index = 0;
    c.name = "test";
    c.data_type = osf::numeric_values_data_type(samples);
    c.samples = std::move(samples);
    c.segments = std::move(segments);
    return c;
}

TEST(EquidistantChannel, samples_with_time_single_segment) {
    auto c = make_eq(
        osf::NumericValues{std::vector<double>{10.0, 20.0, 30.0, 40.0}},
        {{1'000'000'000, 1.0, 0, 4}});
    auto samples = c.samples_vector();
    ASSERT_EQ(samples.size(), 4u);
    EXPECT_EQ(samples[0].timestamp_ns, 1'000'000'000);
    EXPECT_DOUBLE_EQ(std::get<double>(samples[0].value), 10.0);
    EXPECT_EQ(samples[1].timestamp_ns, 2'000'000'000);
    EXPECT_EQ(samples[2].timestamp_ns, 3'000'000'000);
    EXPECT_EQ(samples[3].timestamp_ns, 4'000'000'000);
    EXPECT_DOUBLE_EQ(std::get<double>(samples[3].value), 40.0);
}

TEST(EquidistantChannel, samples_with_time_three_segments_no_interpolation) {
    auto c = make_eq(
        osf::NumericValues{
            std::vector<std::int32_t>{1, 2, 3, 100, 101, 200, 201}},
        {
            {1'000,         1000.0, 0, 3},
            {1'000'000'000, 1000.0, 3, 2},
            {5'000'000'000, 1000.0, 5, 2},
        });
    auto samples = c.samples_vector();
    ASSERT_EQ(samples.size(), 7u);
    EXPECT_EQ(samples[0].timestamp_ns, 1'000);
    EXPECT_EQ(std::get<std::int32_t>(samples[0].value), 1);
    // Gaps between segments must NOT be interpolated.
    EXPECT_EQ(samples[3].timestamp_ns, 1'000'000'000);
    EXPECT_EQ(std::get<std::int32_t>(samples[3].value), 100);
    EXPECT_EQ(samples[5].timestamp_ns, 5'000'000'000);
    EXPECT_EQ(std::get<std::int32_t>(samples[5].value), 200);
}

TEST(EquidistantChannel, empty_channel_iteration_yields_nothing) {
    auto c = make_eq(osf::NumericValues{std::vector<double>{}}, {});
    auto samples = c.samples_vector();
    EXPECT_TRUE(samples.empty());
    EXPECT_TRUE(osf::numeric_values_empty(c.samples));
}

TEST(EquidistantChannel, flat_access_mismatch_returns_typed_error) {
    auto c = make_eq(osf::NumericValues{std::vector<std::int32_t>{1, 2, 3}},
                     {{0, 1.0, 0, 3}});
    // Wrong type → mismatch.
    auto wrong = osf::as_doubles_flat(c);
    ASSERT_FALSE(wrong.has_value());
    EXPECT_EQ(wrong.error().code, osf::Error::Code::DataTypeMismatch);
    // Matching access works.
    auto right = osf::as_int32_flat(c);
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(*right, (std::vector<std::int32_t>{1, 2, 3}));
}

// ---------------------------------------------------------------------
// TimestampedChannel::samples_vector
// ---------------------------------------------------------------------

TEST(TimestampedChannel, samples_with_time_pairs_correctly) {
    osf::TimestampedChannel c;
    c.index = 0;
    c.name = "ts";
    c.data_type = osf::DataType::Int32;
    c.timestamps_ns = {100, 200, 300};
    c.values = osf::NumericValues{std::vector<std::int32_t>{10, 20, 30}};

    auto samples = c.samples_vector();
    ASSERT_EQ(samples.size(), 3u);
    EXPECT_EQ(samples[0].timestamp_ns, 100);
    EXPECT_EQ(std::get<std::int32_t>(samples[0].value), 10);
    EXPECT_EQ(samples[2].timestamp_ns, 300);
    EXPECT_EQ(std::get<std::int32_t>(samples[2].value), 30);

    auto flat = osf::as_int32_flat(c);
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
    c.data_type = osf::DataType::String;
    c.timestamps_ns = {1, 2};
    c.string_values = std::vector<std::string>{"hi", "bye"};

    auto samples = c.samples_vector();
    ASSERT_EQ(samples.size(), 2u);
    EXPECT_EQ(samples[0].timestamp_ns, 1);
    EXPECT_EQ(samples[0].value.kind, osf::VariableValueRef::Kind::String);
    EXPECT_EQ(samples[0].value.string_value, "hi");
    EXPECT_EQ(samples[1].value.string_value, "bye");

    auto strings = c.as_strings();
    ASSERT_TRUE(strings.has_value());
    EXPECT_EQ(**strings,
              (std::vector<std::string>{"hi", "bye"}));

    auto binaries = c.as_binaries();
    ASSERT_FALSE(binaries.has_value());
    EXPECT_EQ(binaries.error().code, osf::Error::Code::DataTypeMismatch);
}

TEST(VariableChannel, binary_iteration_collects_values) {
    osf::VariableChannel c;
    c.index = 1;
    c.name = "blob";
    c.data_type = osf::DataType::Binary;
    c.timestamps_ns = {10, 20};
    c.binary_values = std::vector<std::vector<std::uint8_t>>{
        {0xFF, 0xD8}, {0x89, 0x50}};

    auto samples = c.samples_vector();
    ASSERT_EQ(samples.size(), 2u);
    EXPECT_EQ(samples[0].value.kind, osf::VariableValueRef::Kind::Binary);
    EXPECT_EQ(samples[0].value.binary_value,
              (std::vector<std::uint8_t>{0xFF, 0xD8}));

    auto binaries = c.as_binaries();
    ASSERT_TRUE(binaries.has_value());
    EXPECT_EQ((**binaries)[1], (std::vector<std::uint8_t>{0x89, 0x50}));
}

// ---------------------------------------------------------------------
// Common Channel (variant) accessors
// ---------------------------------------------------------------------

TEST(DataChannel, common_accessors_work_for_each_variant) {
    auto eq = make_eq(osf::NumericValues{std::vector<double>{1, 2, 3}},
                      {{0, 1.0, 0, 3}});
    osf::DataChannel dc{std::move(eq)};
    EXPECT_EQ(osf::channel_index(dc), 0);
    EXPECT_EQ(osf::channel_name(dc), "test");
    EXPECT_EQ(osf::channel_data_type(dc), osf::DataType::Double);
    EXPECT_EQ(osf::channel_sample_count(dc), 3u);
    EXPECT_FALSE(osf::channel_is_empty(dc));

    osf::TimestampedChannel ts;
    ts.index = 1;
    ts.name = "ts";
    ts.data_type = osf::DataType::Int32;
    ts.physical_unit = "V";
    ts.timestamps_ns = {10, 20};
    ts.values = osf::NumericValues{std::vector<std::int32_t>{1, 2}};
    osf::DataChannel dc2{std::move(ts)};
    EXPECT_EQ(osf::channel_physical_unit(dc2).value_or(""), "V");
    EXPECT_EQ(osf::channel_sample_count(dc2), 2u);

    osf::VariableChannel var;
    var.index = 2;
    var.name = "msg";
    var.data_type = osf::DataType::String;
    var.timestamps_ns = {100};
    var.string_values = std::vector<std::string>{"hello"};
    osf::DataChannel dc3{std::move(var)};
    EXPECT_EQ(osf::channel_sample_count(dc3), 1u);
    EXPECT_EQ(osf::channel_data_type(dc3), osf::DataType::String);
}

}  // namespace
