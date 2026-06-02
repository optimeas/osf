// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "roundtrip_helper.hpp"

#include <gtest/gtest.h>

#include <osf/binary_sample.hpp>
#include <osf/data_channel.hpp>
#include <osf/manager.hpp>
#include <osf/streaming_writer.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

class StreamingWriterExamples : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        auto dir = std::filesystem::path{OSF_EXAMPLES_DIR};
        ASSERT_TRUE(std::filesystem::exists(dir))
            << "OSF_EXAMPLES_DIR does not exist: " << dir;
    }

    static std::filesystem::path examples_dir() {
        return std::filesystem::path{OSF_EXAMPLES_DIR};
    }
};

std::filesystem::path make_temp_path() {
    static std::atomic<std::uint64_t> counter{0};
    auto const n = counter.fetch_add(1) + 1;
    return std::filesystem::temp_directory_path() /
           ("osf_streaming_writer_xref_" + std::to_string(n) + ".osf");
}

struct TempFileGuard {
    std::filesystem::path path;
    ~TempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// ── Per-channel write dispatch (for the round-trip helper) ───────────

osf::Result<void> write_equidistant(
        osf::StreamingWriter& w, std::uint16_t channel,
        osf::EquidistantChannel const& eq) {
    // Equidistant is float or double only per spec rev 2026-05-04.
    if (auto v = osf::as_doubles_flat(eq); v.has_value()) {
        // Replay each segment using the segment's start_ts + rate +
        // the slice of the flat vector that segment covers.
        for (auto const& seg : eq.segments) {
            auto const* base = v->data() + seg.start_index;
            if (auto r = w.start_equidistant_segment(
                    channel, seg.start_timestamp_ns, seg.sample_rate_hz,
                    base, seg.sample_count); !r) return r;
        }
        return {};
    }
    if (auto v = osf::as_floats_flat(eq); v.has_value()) {
        for (auto const& seg : eq.segments) {
            auto const* base = v->data() + seg.start_index;
            if (auto r = w.start_equidistant_segment(
                    channel, seg.start_timestamp_ns, seg.sample_rate_hz,
                    base, seg.sample_count); !r) return r;
        }
        return {};
    }
    return tl::make_unexpected(osf::Error{
        osf::Error::Code::DataTypeMismatch,
        "equidistant channel is neither float nor double"});
}

#define WRITE_TIMESTAMPED_DISPATCH(SUFFIX, TYPE)                              \
    if (auto v = osf::as_##SUFFIX##_flat(ts); v.has_value()) {                \
        std::vector<std::int64_t> times; times.reserve(v->size());            \
        std::vector<TYPE>         vals;  vals.reserve(v->size());             \
        for (auto const& pair : *v) {                                         \
            times.push_back(pair.first);                                      \
            vals.push_back(pair.second);                                      \
        }                                                                     \
        return w.write_timestamped_samples<TYPE>(                             \
            channel, times.data(), vals.data(), vals.size());                 \
    }

osf::Result<void> write_timestamped(
        osf::StreamingWriter& w, std::uint16_t channel,
        osf::TimestampedChannel const& ts) {
    // bool is special — std::vector<bool> is a bit-packed specialization
    // with no contiguous storage, so we cannot pass &vals[0] / .data()
    // to write_timestamped_samples<bool>(bool const*, ...). Use a
    // heap-allocated bool array to preserve the bool const* interface.
    if (auto v = osf::as_bools_flat(ts); v.has_value()) {
        std::vector<std::int64_t> times; times.reserve(v->size());
        auto vals = std::make_unique<bool[]>(v->size());
        for (std::size_t i = 0; i < v->size(); ++i) {
            times.push_back((*v)[i].first);
            vals[i] = (*v)[i].second;
        }
        return w.write_timestamped_samples<bool>(
            channel, times.data(), vals.get(), v->size());
    }
    WRITE_TIMESTAMPED_DISPATCH(int8,    std::int8_t)
    WRITE_TIMESTAMPED_DISPATCH(int16,   std::int16_t)
    WRITE_TIMESTAMPED_DISPATCH(int32,   std::int32_t)
    WRITE_TIMESTAMPED_DISPATCH(int64,   std::int64_t)
    WRITE_TIMESTAMPED_DISPATCH(uint8,   std::uint8_t)
    WRITE_TIMESTAMPED_DISPATCH(uint16,  std::uint16_t)
    WRITE_TIMESTAMPED_DISPATCH(uint32,  std::uint32_t)
    WRITE_TIMESTAMPED_DISPATCH(uint64,  std::uint64_t)
    WRITE_TIMESTAMPED_DISPATCH(floats,  float)
    WRITE_TIMESTAMPED_DISPATCH(doubles, double)
    if (auto v = osf::as_gps_flat(ts); v.has_value()) {
        std::vector<std::int64_t>     times; times.reserve(v->size());
        std::vector<osf::GpsLocation> vals;  vals.reserve(v->size());
        for (auto const& pair : *v) {
            times.push_back(pair.first);
            vals.push_back(pair.second);
        }
        return w.write_timestamped_gps_samples(
            channel, times.data(), vals.data(), vals.size());
    }
    return tl::make_unexpected(osf::Error{
        osf::Error::Code::DataTypeMismatch,
        "timestamped channel has unrecognised datatype"});
}

#undef WRITE_TIMESTAMPED_DISPATCH

osf::Result<void> write_variable(
        osf::StreamingWriter& w, std::uint16_t channel,
        osf::VariableChannel const& var) {
    if (auto strings = var.as_strings(); strings.has_value()) {
        for (std::size_t i = 0; i < (*strings)->size(); ++i) {
            if (auto r = w.write_timestamped_string(
                    channel, var.timestamps_ns[i],
                    std::string_view{(**strings)[i]}); !r) return r;
        }
        return {};
    }
    if (auto bins = var.as_binaries(); bins.has_value()) {
        for (std::size_t i = 0; i < (*bins)->size(); ++i) {
            auto const& blob = (**bins)[i];
            if (auto r = w.write_timestamped_binary(
                    channel, var.timestamps_ns[i],
                    osf::BinarySample::from_vector(blob)); !r) return r;
        }
        return {};
    }
    return tl::make_unexpected(osf::Error{
        osf::Error::Code::DataTypeMismatch,
        "variable channel is neither string nor binary"});
}

// Derive the writer-side channel_type from the loaded variant.
osf::ChannelType channel_type_from(osf::DataChannel const& ch) {
    if (std::holds_alternative<osf::EquidistantChannel>(ch))
        return osf::ChannelType::Equidistant;
    if (std::holds_alternative<osf::TimestampedChannel>(ch))
        return osf::ChannelType::Timestamped;
    return osf::ChannelType::Scalar;   // VariableChannel — scalar fits per metablock
}

// Round-trip: load src → write all channels via StreamingWriter →
// reload → assert channel count + per-channel name/datatype/sample-count
// AND first/last sample values (via roundtrip_managers_equal).
::testing::AssertionResult roundtrip_via_streaming_writer(
        std::filesystem::path const& src) {
    auto src_mgr = osf::DataManager::load_from_file(src);
    if (!src_mgr) {
        return ::testing::AssertionFailure()
            << "load src failed: " << src_mgr.error().message;
    }

    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        // Copy file-info (only the user-controllable fields).
        if (src_mgr->meta.file_info.creator)
            w.set_creator(*src_mgr->meta.file_info.creator);
        if (src_mgr->meta.file_info.tag)
            w.set_tag(*src_mgr->meta.file_info.tag);
        if (src_mgr->meta.file_info.reason)
            w.set_reason(*src_mgr->meta.file_info.reason);
        if (src_mgr->meta.file_info.namespace_sep)
            w.set_namespace_sep(*src_mgr->meta.file_info.namespace_sep);
        if (src_mgr->meta.file_info.comment)
            w.set_comment(*src_mgr->meta.file_info.comment);
        if (src_mgr->meta.file_info.created_at_latitude &&
            src_mgr->meta.file_info.created_at_longitude &&
            src_mgr->meta.file_info.created_at_altitude) {
            w.set_location(*src_mgr->meta.file_info.created_at_latitude,
                           *src_mgr->meta.file_info.created_at_longitude,
                           *src_mgr->meta.file_info.created_at_altitude);
        }

        // Add channels — derive ChannelDef from the loaded DataChannels.
        for (auto const& ch : src_mgr->channels()) {
            osf::ChannelDef def;
            def.name = osf::channel_name(ch);
            def.data_type = osf::channel_data_type(ch);
            def.channel_type = channel_type_from(ch);
            // Use sov=4 for variable channels (in case the source had
            // payloads near the sov=2 limit). For numeric channels,
            // sov=4 is also safe (fits everything).
            def.size_of_length_value = 4;
            def.physical_unit  = osf::channel_physical_unit(ch);
            def.display_name   = osf::channel_display_name(ch);
            if (auto r = w.add_channel(def); !r) {
                return ::testing::AssertionFailure()
                    << "add_channel failed for "
                    << osf::channel_name(ch) << ": "
                    << r.error().message;
            }
        }
        if (auto r = w.start(); !r) {
            return ::testing::AssertionFailure()
                << "start failed: " << r.error().message;
        }

        // Write samples per channel.
        for (std::uint16_t idx = 0;
             idx < src_mgr->channels().size(); ++idx) {
            auto const& ch = src_mgr->channels()[idx];
            osf::Result<void> r;
            if (auto const* eq = std::get_if<osf::EquidistantChannel>(&ch)) {
                r = write_equidistant(w, idx, *eq);
            } else if (auto const* ts =
                       std::get_if<osf::TimestampedChannel>(&ch)) {
                r = write_timestamped(w, idx, *ts);
            } else if (auto const* var =
                       std::get_if<osf::VariableChannel>(&ch)) {
                r = write_variable(w, idx, *var);
            }
            if (!r) {
                return ::testing::AssertionFailure()
                    << "write channel " << osf::channel_name(ch) << " failed: "
                    << r.error().message;
            }
        }

        if (auto r = w.close(); !r) {
            return ::testing::AssertionFailure()
                << "close failed: " << r.error().message;
        }
    }

    auto out_mgr = osf::DataManager::load_from_file(g.path);
    if (!out_mgr) {
        return ::testing::AssertionFailure()
            << "load output failed: " << out_mgr.error().message;
    }
    return osf_test::roundtrip_managers_equal(*src_mgr, *out_mgr);
}

}  // namespace

// ── Category F — Cross-implementation roundtrip on three files ───────

TEST_F(StreamingWriterExamples, roundtrip_osf5_equidistant) {
    auto const path = examples_dir() / "generated" / "osf5_equidistant.osf";
    ASSERT_TRUE(std::filesystem::exists(path)) << "missing: " << path;
    EXPECT_TRUE(roundtrip_via_streaming_writer(path));
}

TEST_F(StreamingWriterExamples, roundtrip_osf5_scalar_numeric) {
    auto const path = examples_dir() / "generated" / "osf5_scalar_numeric.osf";
    ASSERT_TRUE(std::filesystem::exists(path)) << "missing: " << path;
    EXPECT_TRUE(roundtrip_via_streaming_writer(path));
}

TEST_F(StreamingWriterExamples, roundtrip_osf5_mixed_extended) {
    auto const path = examples_dir() / "generated" / "osf5_mixed_extended.osf";
    ASSERT_TRUE(std::filesystem::exists(path)) << "missing: " << path;
    EXPECT_TRUE(roundtrip_via_streaming_writer(path));
}

// ── Category G — Reader-truncation regression ────────────────────────

TEST_F(StreamingWriterExamples,
       partial_write_at_block_boundary_remains_readable) {
    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "x";
        d.data_type = osf::DataType::Double;
        d.channel_type = osf::ChannelType::Timestamped;
        d.size_of_length_value = 2;
        ASSERT_TRUE(w.add_channel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        for (std::int64_t i = 0; i < 10; ++i) {
            ASSERT_TRUE(w.write_timestamped_sample<double>(
                0, /*ts=*/i, /*v=*/static_cast<double>(i)).has_value());
        }
        ASSERT_TRUE(w.close().has_value());
    }

    // Each single-sample timestamped-double block frame is 21 bytes:
    //   [u16 ci][u16 len=17][0x08 ctrl][i64 ts][f64 sample]
    //   bytes 0-1: ci, 2-3: len, 4: ctrl, 5-12: i64 ts, 13-20: f64
    // Truncating 10 bytes leaves 11 bytes of the last frame: the header
    // (5 bytes) + only 6 of the 8 timestamp bytes.  The cut falls inside
    // the i64 timestamp field (the last 2 timestamp bytes and the entire
    // f64 sample are gone).
    auto const original_size = std::filesystem::file_size(g.path);
    ASSERT_GT(original_size, 10u);
    std::filesystem::resize_file(g.path, original_size - 10);

    auto mgr = osf::DataManager::load_from_file(g.path);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    auto const* ts_ch = std::get_if<osf::TimestampedChannel>(
        mgr->channel("x"));
    ASSERT_NE(ts_ch, nullptr);
    EXPECT_EQ(ts_ch->timestamps_ns.size(), 9u);
    EXPECT_EQ(mgr->stats.blocks_truncated, 1u);
    EXPECT_EQ(ts_ch->timestamps_ns[0], 0);
    EXPECT_EQ(ts_ch->timestamps_ns[8], 8);
}
