// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "roundtriphelper.h"

#include <gtest/gtest.h>

#include <osf/binarysample.h>
#include <osf/datachannel.h>
#include <osf/manager.h>
#include <osf/streamingwriter.h>

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

    static std::filesystem::path examplesDir() {
        return std::filesystem::path{OSF_EXAMPLES_DIR};
    }
};

std::filesystem::path makeTempPath() {
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

osf::Result<void> writeEquidistant(
        osf::StreamingWriter& w, std::uint16_t channel,
        osf::EquidistantChannel const& eq) {
    // Equidistant is float or double only per spec rev 2026-05-04.
    if (auto v = osf::asDoublesFlat(eq); v.has_value()) {
        // Replay each segment using the segment's startTs + rate +
        // the slice of the flat vector that segment covers.
        for (auto const& seg : eq.segments) {
            auto const* base = v->data() + seg.startIndex;
            if (auto r = w.startEquidistantSegment(
                    channel, seg.startTimestampNs, seg.sampleRateHz,
                    base, seg.sampleCount); !r) return r;
        }
        return {};
    }
    if (auto v = osf::asFloatsFlat(eq); v.has_value()) {
        for (auto const& seg : eq.segments) {
            auto const* base = v->data() + seg.startIndex;
            if (auto r = w.startEquidistantSegment(
                    channel, seg.startTimestampNs, seg.sampleRateHz,
                    base, seg.sampleCount); !r) return r;
        }
        return {};
    }
    return tl::make_unexpected(osf::Error{
        osf::Error::Code::DataTypeMismatch,
        "equidistant channel is neither float nor double"});
}

#define WRITE_TIMESTAMPED_DISPATCH(SUFFIX, TYPE)                              \
    if (auto v = osf::as##SUFFIX##Flat(ts); v.has_value()) {                  \
        std::vector<std::int64_t> times; times.reserve(v->size());            \
        std::vector<TYPE>         vals;  vals.reserve(v->size());             \
        for (auto const& pair : *v) {                                         \
            times.push_back(pair.first);                                      \
            vals.push_back(pair.second);                                      \
        }                                                                     \
        return w.writeTimestampedSamples<TYPE>(                             \
            channel, times.data(), vals.data(), vals.size());                 \
    }

osf::Result<void> writeTimestamped(
        osf::StreamingWriter& w, std::uint16_t channel,
        osf::TimestampedChannel const& ts) {
    // bool is special — std::vector<bool> is a bit-packed specialization
    // with no contiguous storage, so we cannot pass &vals[0] / .data()
    // to writeTimestampedSamples<bool>(bool const*, ...). Use a
    // heap-allocated bool array to preserve the bool const* interface.
    if (auto v = osf::asBoolsFlat(ts); v.has_value()) {
        std::vector<std::int64_t> times; times.reserve(v->size());
        auto vals = std::make_unique<bool[]>(v->size());
        for (std::size_t i = 0; i < v->size(); ++i) {
            times.push_back((*v)[i].first);
            vals[i] = (*v)[i].second;
        }
        return w.writeTimestampedSamples<bool>(
            channel, times.data(), vals.get(), v->size());
    }
    WRITE_TIMESTAMPED_DISPATCH(Int8,    std::int8_t)
    WRITE_TIMESTAMPED_DISPATCH(Int16,   std::int16_t)
    WRITE_TIMESTAMPED_DISPATCH(Int32,   std::int32_t)
    WRITE_TIMESTAMPED_DISPATCH(Int64,   std::int64_t)
    WRITE_TIMESTAMPED_DISPATCH(Uint8,   std::uint8_t)
    WRITE_TIMESTAMPED_DISPATCH(Uint16,  std::uint16_t)
    WRITE_TIMESTAMPED_DISPATCH(Uint32,  std::uint32_t)
    WRITE_TIMESTAMPED_DISPATCH(Uint64,  std::uint64_t)
    WRITE_TIMESTAMPED_DISPATCH(Floats,  float)
    WRITE_TIMESTAMPED_DISPATCH(Doubles, double)
    if (auto v = osf::asGpsFlat(ts); v.has_value()) {
        std::vector<std::int64_t>     times; times.reserve(v->size());
        std::vector<osf::GpsLocation> vals;  vals.reserve(v->size());
        for (auto const& pair : *v) {
            times.push_back(pair.first);
            vals.push_back(pair.second);
        }
        return w.writeTimestampedGpsSamples(
            channel, times.data(), vals.data(), vals.size());
    }
    return tl::make_unexpected(osf::Error{
        osf::Error::Code::DataTypeMismatch,
        "timestamped channel has unrecognised datatype"});
}

#undef WRITE_TIMESTAMPED_DISPATCH

osf::Result<void> writeVariable(
        osf::StreamingWriter& w, std::uint16_t channel,
        osf::VariableChannel const& var) {
    if (auto strings = var.asStrings(); strings.has_value()) {
        for (std::size_t i = 0; i < (*strings)->size(); ++i) {
            if (auto r = w.writeTimestampedString(
                    channel, var.timestampsNs[i],
                    std::string_view{(**strings)[i]}); !r) return r;
        }
        return {};
    }
    if (auto bins = var.asBinaries(); bins.has_value()) {
        for (std::size_t i = 0; i < (*bins)->size(); ++i) {
            auto const& blob = (**bins)[i];
            if (auto r = w.writeTimestampedBinary(
                    channel, var.timestampsNs[i],
                    osf::BinarySample::fromVector(blob)); !r) return r;
        }
        return {};
    }
    return tl::make_unexpected(osf::Error{
        osf::Error::Code::DataTypeMismatch,
        "variable channel is neither string nor binary"});
}

// Derive the writer-side channelType from the loaded variant.
osf::ChannelType channelTypeFrom(osf::DataChannel const& ch) {
    if (std::holds_alternative<osf::EquidistantChannel>(ch))
        return osf::ChannelType::Equidistant;
    if (std::holds_alternative<osf::TimestampedChannel>(ch))
        return osf::ChannelType::Timestamped;
    return osf::ChannelType::Scalar;   // VariableChannel — scalar fits per metablock
}

// Round-trip: load src → write all channels via StreamingWriter →
// reload → assert channel count + per-channel name/datatype/sample-count
// AND first/last sample values (via roundtripManagersEqual).
::testing::AssertionResult roundtripViaStreamingWriter(
        std::filesystem::path const& src) {
    auto srcMgr = osf::DataManager::loadFromFile(src);
    if (!srcMgr) {
        return ::testing::AssertionFailure()
            << "load src failed: " << srcMgr.error().message;
    }

    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        // Copy file-info (only the user-controllable fields).
        if (srcMgr->meta.fileInfo.creator)
            w.setCreator(*srcMgr->meta.fileInfo.creator);
        if (srcMgr->meta.fileInfo.tag)
            w.setTag(*srcMgr->meta.fileInfo.tag);
        if (srcMgr->meta.fileInfo.reason)
            w.setReason(*srcMgr->meta.fileInfo.reason);
        if (srcMgr->meta.fileInfo.namespaceSep)
            w.setNamespaceSep(*srcMgr->meta.fileInfo.namespaceSep);
        if (srcMgr->meta.fileInfo.comment)
            w.setComment(*srcMgr->meta.fileInfo.comment);
        if (srcMgr->meta.fileInfo.createdAtLatitude &&
            srcMgr->meta.fileInfo.createdAtLongitude &&
            srcMgr->meta.fileInfo.createdAtAltitude) {
            w.setLocation(*srcMgr->meta.fileInfo.createdAtLatitude,
                           *srcMgr->meta.fileInfo.createdAtLongitude,
                           *srcMgr->meta.fileInfo.createdAtAltitude);
        }

        // Add channels — derive ChannelDef from the loaded DataChannels.
        for (auto const& ch : srcMgr->channels()) {
            osf::ChannelDef def;
            def.name = osf::channelName(ch);
            def.dataType = osf::channelDataType(ch);
            def.channelType = channelTypeFrom(ch);
            // Use sov=4 for variable channels (in case the source had
            // payloads near the sov=2 limit). For numeric channels,
            // sov=4 is also safe (fits everything).
            def.sizeOfLengthValue = 4;
            def.physicalUnit  = osf::channelPhysicalUnit(ch);
            def.displayName   = osf::channelDisplayName(ch);
            if (auto r = w.addChannel(def); !r) {
                return ::testing::AssertionFailure()
                    << "addChannel failed for "
                    << osf::channelName(ch) << ": "
                    << r.error().message;
            }
        }
        if (auto r = w.start(); !r) {
            return ::testing::AssertionFailure()
                << "start failed: " << r.error().message;
        }

        // Write samples per channel.
        for (std::uint16_t idx = 0;
             idx < srcMgr->channels().size(); ++idx) {
            auto const& ch = srcMgr->channels()[idx];
            osf::Result<void> r;
            if (auto const* eq = std::get_if<osf::EquidistantChannel>(&ch)) {
                r = writeEquidistant(w, idx, *eq);
            } else if (auto const* ts =
                       std::get_if<osf::TimestampedChannel>(&ch)) {
                r = writeTimestamped(w, idx, *ts);
            } else if (auto const* var =
                       std::get_if<osf::VariableChannel>(&ch)) {
                r = writeVariable(w, idx, *var);
            }
            if (!r) {
                return ::testing::AssertionFailure()
                    << "write channel " << osf::channelName(ch) << " failed: "
                    << r.error().message;
            }
        }

        if (auto r = w.close(); !r) {
            return ::testing::AssertionFailure()
                << "close failed: " << r.error().message;
        }
    }

    auto outMgr = osf::DataManager::loadFromFile(g.path);
    if (!outMgr) {
        return ::testing::AssertionFailure()
            << "load output failed: " << outMgr.error().message;
    }
    return osftest::roundtripManagersEqual(*srcMgr, *outMgr);
}

}  // namespace

// ── Category F — Cross-implementation roundtrip on three files ───────

TEST_F(StreamingWriterExamples, roundtrip_osf5_equidistant) {
    auto const path = examplesDir() / "generated" / "osf5_equidistant.osf";
    ASSERT_TRUE(std::filesystem::exists(path)) << "missing: " << path;
    EXPECT_TRUE(roundtripViaStreamingWriter(path));
}

TEST_F(StreamingWriterExamples, roundtrip_osf5_scalar_numeric) {
    auto const path = examplesDir() / "generated" / "osf5_scalar_numeric.osf";
    ASSERT_TRUE(std::filesystem::exists(path)) << "missing: " << path;
    EXPECT_TRUE(roundtripViaStreamingWriter(path));
}

TEST_F(StreamingWriterExamples, roundtrip_osf5_mixed_extended) {
    auto const path = examplesDir() / "generated" / "osf5_mixed_extended.osf";
    ASSERT_TRUE(std::filesystem::exists(path)) << "missing: " << path;
    EXPECT_TRUE(roundtripViaStreamingWriter(path));
}

// ── Category G — Reader-truncation regression ────────────────────────

TEST_F(StreamingWriterExamples,
       partial_write_at_block_boundary_remains_readable) {
    TempFileGuard g{makeTempPath()};
    {
        osf::StreamingWriter w{g.path};
        osf::ChannelDef d;
        d.name = "x";
        d.dataType = osf::DataType::Double;
        d.channelType = osf::ChannelType::Timestamped;
        d.sizeOfLengthValue = 2;
        ASSERT_TRUE(w.addChannel(d).has_value());
        ASSERT_TRUE(w.start().has_value());
        for (std::int64_t i = 0; i < 10; ++i) {
            ASSERT_TRUE(w.writeTimestampedSample<double>(
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
    auto const originalSize = std::filesystem::file_size(g.path);
    ASSERT_GT(originalSize, 10u);
    std::filesystem::resize_file(g.path, originalSize - 10);

    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    auto const* tsCh = std::get_if<osf::TimestampedChannel>(
        mgr->channel("x"));
    ASSERT_NE(tsCh, nullptr);
    EXPECT_EQ(tsCh->timestampsNs.size(), 9u);
    EXPECT_EQ(mgr->stats.blocksTruncated, 1u);
    EXPECT_EQ(tsCh->timestampsNs[0], 0);
    EXPECT_EQ(tsCh->timestampsNs[8], 8);
}
