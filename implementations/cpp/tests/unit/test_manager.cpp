// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Unit tests for the DataManager builder state machine.
//
// Tests build a synthetic OSF5 stream (magic header + JSON metablock +
// hand-crafted block bytes), feed it through DataManager::loadFromStream,
// and assert on the assembled channel list. This exercises the full
// builder state machine (Pending → Equidistant / Timestamped / Variable),
// the data-type-mismatch checks, and the ChannelMixedBlockTypes /
// ContinuedDataWithoutStart / RelStampWithoutAnchor branches.
//
// Mirror of implementations/rust/osf-core/src/manager.rs tests.

#include <gtest/gtest.h>

#include <osf/osf.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------
// Byte builders.
// ---------------------------------------------------------------------

void putU16(std::vector<std::uint8_t>& dst, std::uint16_t v) {
    dst.push_back(static_cast<std::uint8_t>(v        & 0xFF));
    dst.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void putU32(std::vector<std::uint8_t>& dst, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        dst.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}

void putI32(std::vector<std::uint8_t>& dst, std::int32_t v) {
    putU32(dst, static_cast<std::uint32_t>(v));
}

void putI64(std::vector<std::uint8_t>& dst, std::int64_t v) {
    auto const u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i)
        dst.push_back(static_cast<std::uint8_t>((u >> (8 * i)) & 0xFF));
}

void putF64(std::vector<std::uint8_t>& dst, double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i)
        dst.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF));
}

void putF32(std::vector<std::uint8_t>& dst, float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 4; ++i)
        dst.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF));
}

// Wrap header + metablock JSON + block bytes into a single stream.
std::stringstream makeOsf5Stream(std::string const& metablockJson,
                                   std::vector<std::uint8_t> const& blocks) {
    std::string body;
    body.reserve(20 + metablockJson.size() + blocks.size());
    body += "OSF5 ";
    body += std::to_string(metablockJson.size());
    body += "\n";
    body += metablockJson;
    body.insert(body.end(),
                reinterpret_cast<char const*>(blocks.data()),
                reinterpret_cast<char const*>(blocks.data() + blocks.size()));
    std::stringstream ss(body, std::ios::in | std::ios::binary);
    return ss;
}

// ---------------------------------------------------------------------
// Block constructors — produce the exact wire bytes that BlockReader
// would consume.
// ---------------------------------------------------------------------

// bcStartData for double channel sizeoflengthvalue=2.
// payload = 1 ctl + 8 ts + 8 rate + 8*n samples
void appendStartDouble(std::vector<std::uint8_t>& out, std::uint16_t channel,
                         std::int64_t ts, double rate,
                         std::vector<double> const& samples) {
    bool const multi = samples.size() != 1;
    std::uint16_t const len = static_cast<std::uint16_t>(
        1 + 8 + 8 + (multi ? 4 : 0) + 8 * samples.size());
    putU16(out, channel);
    putU16(out, len);
    out.push_back(static_cast<std::uint8_t>(multi ? 0x86 : 0x06));
    putI64(out, ts);
    putF64(out, rate);
    if (multi) putU32(out, static_cast<std::uint32_t>(samples.size()));
    for (double v : samples) putF64(out, v);
}

// bcContinuedData for double, sizeoflengthvalue=2.
void appendContinuedDouble(std::vector<std::uint8_t>& out,
                             std::uint16_t channel,
                             std::vector<double> const& samples) {
    bool const multi = samples.size() != 1;
    std::uint16_t const len = static_cast<std::uint16_t>(
        1 + (multi ? 4 : 0) + 8 * samples.size());
    putU16(out, channel);
    putU16(out, len);
    out.push_back(static_cast<std::uint8_t>(multi ? 0x85 : 0x05));
    if (multi) putU32(out, static_cast<std::uint32_t>(samples.size()));
    for (double v : samples) putF64(out, v);
}

// bcStartData for float channel sizeoflengthvalue=2.
// payload = 1 ctl + 8 ts + 8 rate [+ 4 N if multi] + 4*n samples
void appendStartFloat(std::vector<std::uint8_t>& out, std::uint16_t channel,
                        std::int64_t ts, double rate,
                        std::vector<float> const& samples) {
    bool const multi = samples.size() != 1;
    std::uint16_t const len = static_cast<std::uint16_t>(
        1 + 8 + 8 + (multi ? 4 : 0) + 4 * samples.size());
    putU16(out, channel);
    putU16(out, len);
    out.push_back(static_cast<std::uint8_t>(multi ? 0x86 : 0x06));
    putI64(out, ts);
    putF64(out, rate);
    if (multi) putU32(out, static_cast<std::uint32_t>(samples.size()));
    for (float v : samples) putF32(out, v);
}

// bcContinuedData for float, sizeoflengthvalue=2.
void appendContinuedFloat(std::vector<std::uint8_t>& out,
                            std::uint16_t channel,
                            std::vector<float> const& samples) {
    bool const multi = samples.size() != 1;
    std::uint16_t const len = static_cast<std::uint16_t>(
        1 + (multi ? 4 : 0) + 4 * samples.size());
    putU16(out, channel);
    putU16(out, len);
    out.push_back(static_cast<std::uint8_t>(multi ? 0x85 : 0x05));
    if (multi) putU32(out, static_cast<std::uint32_t>(samples.size()));
    for (float v : samples) putF32(out, v);
}

// bcStartData for int32 channel sizeoflengthvalue=2.
// payload = 1 ctl + 8 ts + 8 rate [+ 4 N if multi] + 4*n samples
void appendStartInt32(std::vector<std::uint8_t>& out, std::uint16_t channel,
                        std::int64_t ts, double rate,
                        std::vector<std::int32_t> const& samples) {
    bool const multi = samples.size() != 1;
    std::uint16_t const len = static_cast<std::uint16_t>(
        1 + 8 + 8 + (multi ? 4 : 0) + 4 * samples.size());
    putU16(out, channel);
    putU16(out, len);
    out.push_back(static_cast<std::uint8_t>(multi ? 0x86 : 0x06));
    putI64(out, ts);
    putF64(out, rate);
    if (multi) putU32(out, static_cast<std::uint32_t>(samples.size()));
    for (std::int32_t v : samples) putI32(out, v);
}

// bcContinuedData for int32, sizeoflengthvalue=2.
void appendContinuedInt32(std::vector<std::uint8_t>& out,
                            std::uint16_t channel,
                            std::vector<std::int32_t> const& samples) {
    bool const multi = samples.size() != 1;
    std::uint16_t const len = static_cast<std::uint16_t>(
        1 + (multi ? 4 : 0) + 4 * samples.size());
    putU16(out, channel);
    putU16(out, len);
    out.push_back(static_cast<std::uint8_t>(multi ? 0x85 : 0x05));
    if (multi) putU32(out, static_cast<std::uint32_t>(samples.size()));
    for (std::int32_t v : samples) putI32(out, v);
}

// bcAbsTimeStampData int32, sizeoflengthvalue=2.
void appendAbsInt32(std::vector<std::uint8_t>& out, std::uint16_t channel,
                      std::vector<std::pair<std::int64_t, std::int32_t>> const& pairs) {
    bool const multi = pairs.size() != 1;
    std::uint16_t const len = static_cast<std::uint16_t>(
        1 + (multi ? 4 : 0) + pairs.size() * (8 + 4));
    putU16(out, channel);
    putU16(out, len);
    out.push_back(static_cast<std::uint8_t>(multi ? 0x88 : 0x08));
    if (multi) putU32(out, static_cast<std::uint32_t>(pairs.size()));
    for (auto const& [ts, v] : pairs) {
        putI64(out, ts);
        putI32(out, v);
    }
}

// bcAbsTimeStampData double, sizeoflengthvalue=2.
void appendAbsDouble(std::vector<std::uint8_t>& out, std::uint16_t channel,
                       std::vector<std::pair<std::int64_t, double>> const& pairs) {
    bool const multi = pairs.size() != 1;
    std::uint16_t const len = static_cast<std::uint16_t>(
        1 + (multi ? 4 : 0) + pairs.size() * (8 + 8));
    putU16(out, channel);
    putU16(out, len);
    out.push_back(static_cast<std::uint8_t>(multi ? 0x88 : 0x08));
    if (multi) putU32(out, static_cast<std::uint32_t>(pairs.size()));
    for (auto const& [ts, v] : pairs) {
        putI64(out, ts);
        putF64(out, v);
    }
}

// bcAbsTimeStampData string, sizeoflengthvalue=4. OSF5 layout: no
// trailing 0x00 byte per spec rev 2026-05-24 (consumed by the
// metaOneString metablock template which declares version=5).
void appendAbsString(std::vector<std::uint8_t>& out, std::uint16_t channel,
                       std::int64_t ts, std::string const& value) {
    // Single-sample variant per spec mandate (and easier to test).
    // bit 7 must be set per spec; we always emit multi (matches Rust + our parser).
    std::uint32_t const payloadLen = static_cast<std::uint32_t>(
        1 + 4 + 8 + value.size());  // ctl + N + ts + bytes (no terminator)
    putU16(out, channel);
    putU32(out, payloadLen);
    out.push_back(0x88);
    putU32(out, 1);  // N=1
    putI64(out, ts);
    for (char c : value) out.push_back(static_cast<std::uint8_t>(c));
}

// bcContinuedRelStampData int32, sizeoflengthvalue=2.
void appendRelInt32(std::vector<std::uint8_t>& out, std::uint16_t channel,
                      std::vector<std::pair<std::uint32_t, std::int32_t>> const& pairs) {
    std::uint16_t const len = static_cast<std::uint16_t>(
        1 + 4 + pairs.size() * (4 + 4));
    putU16(out, channel);
    putU16(out, len);
    out.push_back(0x87);  // multi mandatory for rel-stamp
    putU32(out, static_cast<std::uint32_t>(pairs.size()));
    for (auto const& [delta, v] : pairs) {
        putU32(out, delta);
        putI32(out, v);
    }
}

// ---------------------------------------------------------------------
// Per-test metablock templates.
// ---------------------------------------------------------------------

std::string metaOneDouble(std::uint16_t index = 0) {
    return std::string{"{\"osf\":{\"version\":5,\"channels\":["
        "{\"index\":"} + std::to_string(index) +
        ",\"name\":\"ch" + std::to_string(index) + "\","
        "\"channeltype\":\"scalar\",\"datatype\":\"double\","
        "\"sizeoflengthvalue\":2}]}}";
}

std::string metaOneInt32(std::uint16_t index = 0) {
    return std::string{"{\"osf\":{\"version\":5,\"channels\":["
        "{\"index\":"} + std::to_string(index) +
        ",\"name\":\"ch" + std::to_string(index) + "\","
        "\"channeltype\":\"scalar\",\"datatype\":\"int32\","
        "\"sizeoflengthvalue\":2}]}}";
}

std::string metaOneFloat(std::uint16_t index = 0) {
    return std::string{"{\"osf\":{\"version\":5,\"channels\":["
        "{\"index\":"} + std::to_string(index) +
        ",\"name\":\"ch" + std::to_string(index) + "\","
        "\"channeltype\":\"scalar\",\"datatype\":\"float\","
        "\"sizeoflengthvalue\":2}]}}";
}

std::string metaOneString(std::uint16_t index = 0) {
    return std::string{"{\"osf\":{\"version\":5,\"channels\":["
        "{\"index\":"} + std::to_string(index) +
        ",\"name\":\"ch" + std::to_string(index) + "\","
        "\"channeltype\":\"scalar\",\"datatype\":\"string\","
        "\"sizeoflengthvalue\":4}]}}";
}

// ---------------------------------------------------------------------
// Tests — equidistant builder paths.
// ---------------------------------------------------------------------

TEST(DataManager, one_start_plus_one_continued_yields_one_segment) {
    std::vector<std::uint8_t> blocks;
    std::vector<double> first(100);
    for (std::size_t i = 0; i < 100; ++i) first[i] = static_cast<double>(i);
    std::vector<double> rest(200);
    for (std::size_t i = 0; i < 200; ++i) rest[i] = static_cast<double>(100 + i);
    appendStartDouble(blocks, 0, 1'000, 1000.0, first);
    appendContinuedDouble(blocks, 0, rest);

    auto ss = makeOsf5Stream(metaOneDouble(), blocks);
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    ASSERT_EQ(mgr->channels().size(), 1u);
    auto const& dc = mgr->channels()[0];
    auto const* eq = std::get_if<osf::EquidistantChannel>(&dc);
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->segments.size(), 1u);
    EXPECT_EQ(eq->segments[0].sampleCount, 300u);
    EXPECT_EQ(osf::numericValuesLen(eq->samples), 300u);
}

TEST(DataManager, two_start_blocks_open_two_segments) {
    std::vector<std::uint8_t> blocks;
    appendStartDouble(blocks, 0, 0, 1000.0, std::vector<double>(50, 1.0));
    appendStartDouble(blocks, 0, 1'000'000'000, 2000.0,
                        std::vector<double>(30, 2.0));

    auto ss = makeOsf5Stream(metaOneDouble(), blocks);
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    auto const* eq = std::get_if<osf::EquidistantChannel>(&mgr->channels()[0]);
    ASSERT_NE(eq, nullptr);
    ASSERT_EQ(eq->segments.size(), 2u);
    EXPECT_EQ(eq->segments[0].sampleCount, 50u);
    EXPECT_EQ(eq->segments[0].startIndex, 0u);
    EXPECT_EQ(eq->segments[1].sampleCount, 30u);
    EXPECT_EQ(eq->segments[1].startIndex, 50u);
    EXPECT_EQ(osf::numericValuesLen(eq->samples), 80u);
}

// Coverage probe: the manager state machine and the reader's
// read_numeric_n both accept all 11 numeric data types for
// equidistant blocks even though spec rev 2026-05-04 documents
// float/double only. The Rust reference exercises the same
// permissiveness with `parses_continued_data_int16_multi` in
// reader.rs — we mirror that with end-to-end Float + Int32 tests
// here. Memory-path verification only (no arithmetic); sample
// equality uses EXPECT_EQ for exact bit-pattern match.

TEST(DataManager, one_start_plus_one_continued_float) {
    std::vector<float> const first   = {0.1f, 1.5f, -3.25f, 100.0f, -200.5f};
    std::vector<float> const rest    = {1.0f, 2.0f, 3.0f};

    std::vector<std::uint8_t> blocks;
    appendStartFloat(blocks, 0, 1'000, 1000.0, first);
    appendContinuedFloat(blocks, 0, rest);

    auto ss = makeOsf5Stream(metaOneFloat(), blocks);
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    ASSERT_EQ(mgr->channels().size(), 1u);
    auto const& dc = mgr->channels()[0];
    auto const* eq = std::get_if<osf::EquidistantChannel>(&dc);
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->dataType, osf::DataType::Float);
    ASSERT_EQ(eq->segments.size(), 1u);
    EXPECT_EQ(eq->segments[0].sampleCount, 8u);
    EXPECT_EQ(osf::numericValuesLen(eq->samples), 8u);

    auto flat = osf::asFloatsFlat(*eq);
    ASSERT_TRUE(flat.has_value()) << flat.error().message;
    std::vector<float> expected;
    expected.insert(expected.end(), first.begin(), first.end());
    expected.insert(expected.end(), rest.begin(), rest.end());
    EXPECT_EQ(*flat, expected);
}

TEST(DataManager, one_start_plus_one_continued_int32) {
    std::vector<std::int32_t> const first = {
        0,
        100'000,
        -50'000,
        std::numeric_limits<std::int32_t>::max(),
        std::numeric_limits<std::int32_t>::min(),
    };
    std::vector<std::int32_t> const rest = {1, 2, 3};

    std::vector<std::uint8_t> blocks;
    appendStartInt32(blocks, 0, 2'000, 2000.0, first);
    appendContinuedInt32(blocks, 0, rest);

    auto ss = makeOsf5Stream(metaOneInt32(), blocks);
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    ASSERT_EQ(mgr->channels().size(), 1u);
    auto const& dc = mgr->channels()[0];
    auto const* eq = std::get_if<osf::EquidistantChannel>(&dc);
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->dataType, osf::DataType::Int32);
    ASSERT_EQ(eq->segments.size(), 1u);
    EXPECT_EQ(eq->segments[0].sampleCount, 8u);
    EXPECT_EQ(osf::numericValuesLen(eq->samples), 8u);

    auto flat = osf::asInt32Flat(*eq);
    ASSERT_TRUE(flat.has_value()) << flat.error().message;
    std::vector<std::int32_t> expected;
    expected.insert(expected.end(), first.begin(), first.end());
    expected.insert(expected.end(), rest.begin(), rest.end());
    EXPECT_EQ(*flat, expected);
}

TEST(DataManager, start_then_abs_timestamp_is_mixed_block_types_error) {
    std::vector<std::uint8_t> blocks;
    appendStartDouble(blocks, 0, 0, 1000.0, std::vector<double>(10, 1.0));
    appendAbsDouble(blocks, 0, {{100, 1.0}});

    auto ss = makeOsf5Stream(metaOneDouble(), blocks);
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_FALSE(mgr.has_value());
    EXPECT_EQ(mgr.error().code, osf::Error::Code::ChannelMixedBlockTypes);
}

TEST(DataManager, continued_without_start_is_error) {
    std::vector<std::uint8_t> blocks;
    appendContinuedDouble(blocks, 0, {1.0});

    auto ss = makeOsf5Stream(metaOneDouble(), blocks);
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_FALSE(mgr.has_value());
    EXPECT_EQ(mgr.error().code, osf::Error::Code::ContinuedDataWithoutStart);
}

// ---------------------------------------------------------------------
// Timestamped paths.
// ---------------------------------------------------------------------

TEST(DataManager, abs_timestamped_int32_builds_timestamped_channel) {
    std::vector<std::uint8_t> blocks;
    appendAbsInt32(blocks, 0, {{100, 1}, {200, 2}, {300, 3}});
    appendAbsInt32(blocks, 0, {{400, 4}});

    auto ss = makeOsf5Stream(metaOneInt32(), blocks);
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    auto const* ts = std::get_if<osf::TimestampedChannel>(&mgr->channels()[0]);
    ASSERT_NE(ts, nullptr);
    EXPECT_EQ(ts->timestampsNs,
              (std::vector<std::int64_t>{100, 200, 300, 400}));
    auto const* v = std::get_if<std::vector<std::int32_t>>(&ts->values);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(*v, (std::vector<std::int32_t>{1, 2, 3, 4}));
}

TEST(DataManager, rel_stamp_after_abs_extends_with_cumulative_timestamps) {
    std::vector<std::uint8_t> blocks;
    appendAbsInt32(blocks, 0, {{1'000, 10}});
    appendRelInt32(blocks, 0, {{50, 11}, {50, 12}});

    auto ss = makeOsf5Stream(metaOneInt32(), blocks);
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    auto const* ts = std::get_if<osf::TimestampedChannel>(&mgr->channels()[0]);
    ASSERT_NE(ts, nullptr);
    EXPECT_EQ(ts->timestampsNs,
              (std::vector<std::int64_t>{1'000, 1'050, 1'100}));
}

TEST(DataManager, rel_stamp_without_anchor_is_error) {
    std::vector<std::uint8_t> blocks;
    appendRelInt32(blocks, 0, {{50, 1}});

    auto ss = makeOsf5Stream(metaOneInt32(), blocks);
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_FALSE(mgr.has_value());
    EXPECT_EQ(mgr.error().code, osf::Error::Code::RelStampWithoutAnchor);
}

// ---------------------------------------------------------------------
// Note on data-type mismatch:
//
// The `data_type_mismatch` check in the manager state machine is
// defensive — it guards against custom block-iterator sources that
// bypass the reader. Through `loadFromStream` it is unreachable
// because the reader already typed-decodes the payload based on the
// channel's declared data type before the manager ever sees a block.
// The Rust reference exercises the check by injecting hand-built
// blocks directly into `build_channels`; equivalent C++ coverage
// would require a public `build_from_blocks` helper that we keep out
// of the API for now. The check stays as belt-and-braces for future
// API surfaces.
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
// Variable channel.
// ---------------------------------------------------------------------

TEST(DataManager, variable_string_channel_collects_strings) {
    std::vector<std::uint8_t> blocks;
    appendAbsString(blocks, 0, 100, "hi");
    appendAbsString(blocks, 0, 200, "bye");

    auto ss = makeOsf5Stream(metaOneString(), blocks);
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    auto const* var = std::get_if<osf::VariableChannel>(&mgr->channels()[0]);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->timestampsNs, (std::vector<std::int64_t>{100, 200}));
    ASSERT_TRUE(var->stringValues.has_value());
    EXPECT_EQ(*var->stringValues,
              (std::vector<std::string>{"hi", "bye"}));
    EXPECT_FALSE(var->binaryValues.has_value());
}

// ---------------------------------------------------------------------
// Unsupported channel drop.
// ---------------------------------------------------------------------

TEST(DataManager, unsupported_channel_does_not_appear_in_output) {
    // Two channels: index 0 declared with an unknown datatype (→
    // Unsupported and dropped); index 1 declared int32 with no
    // blocks at all (kept, empty).
    std::string const metablockJson = R"({"osf":{"version":5,"channels":[
        {"index":0,"name":"ch0","channeltype":"scalar",
         "datatype":"future_xy","sizeoflengthvalue":2},
        {"index":1,"name":"ch1","channeltype":"scalar",
         "datatype":"int32","sizeoflengthvalue":2}
    ]}})";

    auto ss = makeOsf5Stream(metablockJson, {});
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    ASSERT_EQ(mgr->channels().size(), 1u);
    EXPECT_EQ(osf::channelIndex(mgr->channels()[0]), 1);
    EXPECT_NE(mgr->channel("ch1"), nullptr);
    EXPECT_EQ(mgr->channel("ch0"), nullptr);
}

// ---------------------------------------------------------------------
// channel_by_name / channelByIndex lookup.
// ---------------------------------------------------------------------

TEST(DataManager, channel_lookup_by_name_and_index) {
    std::vector<std::uint8_t> blocks;
    appendStartDouble(blocks, 0, 0, 1000.0, {1.0, 2.0});

    auto ss = makeOsf5Stream(metaOneDouble(), blocks);
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;

    auto* byName = mgr->channel("ch0");
    ASSERT_NE(byName, nullptr);
    EXPECT_EQ(osf::channelIndex(*byName), 0);

    auto* byIndex = mgr->channelByIndex(0);
    EXPECT_EQ(byIndex, byName);

    EXPECT_EQ(mgr->channel("nope"), nullptr);
    EXPECT_EQ(mgr->channelByIndex(42), nullptr);
}

// ---------------------------------------------------------------------
// OSFZ: a malformed / truncated compressed stream fails gracefully.
// The decompressor yields best-effort EOF on a truncated gzip member,
// then the header parse fails — no hang, no crash.
// ---------------------------------------------------------------------

TEST(DataManager, truncated_gzip_input_fails_gracefully) {
    std::stringstream ss(
        std::string{"\x1F\x8B\x08\x00\x00\x00\x00\x00", 8},
        std::ios::in | std::ios::binary);
    auto mgr = osf::DataManager::loadFromStream(ss);
    ASSERT_FALSE(mgr.has_value());
    // Confirm the error is a parse failure, not a hang or crash.
    EXPECT_FALSE(mgr.error().message.empty())
        << "expected a non-empty error message, got: "
        << mgr.error().message;
}

}  // namespace
