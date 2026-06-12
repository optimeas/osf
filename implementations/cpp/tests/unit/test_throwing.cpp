// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/throwing.h"

#include "osf/blockwriter.h"
#include "osf/datachannel.h"
#include "osf/error.h"
#include "osf/manager.h"
#include "osf/streamingwriter.h"
#include "osf/types.h"

#include "roundtriphelper.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>

namespace {

std::filesystem::path make_temp_path() {
    static std::atomic<std::uint64_t> counter{0};
    auto const n = counter.fetch_add(1) + 1;
    return std::filesystem::temp_directory_path() /
           ("osf_throwing_test_" + std::to_string(n) + ".osf");
}

struct TempFileGuard {
    std::filesystem::path path;
    ~TempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// Build a small OSF5 fixture (one timestamped double channel) in memory.
osf::BlockWriter make_fixture_writer() {
    osf::BlockWriter w;
    osf::ChannelDef d;
    d.name = "Sensor/T";
    d.dataType = osf::DataType::Double;
    d.channelType = osf::ChannelType::Timestamped;
    auto const ci = w.addChannel(d);
    EXPECT_TRUE(ci.has_value());
    std::int64_t const ts[] = {10, 20, 30};
    double const vs[] = {1.5, 2.5, 3.5};
    EXPECT_TRUE(w.addTimestampedSamples<double>(*ci, ts, vs, 3).has_value());
    return w;
}

// Serialize the fixture to a string (a valid plain OSF5 byte stream).
std::string fixture_bytes() {
    std::ostringstream ss;
    EXPECT_TRUE(make_fixture_writer().writeTo(ss).has_value());
    return ss.str();
}

}  // namespace

// ── load ──────────────────────────────────────────────────────────────

TEST(Throwing, load_success_returns_manager) {
    TempFileGuard g{make_temp_path()};
    ASSERT_TRUE(make_fixture_writer().writeToFile(g.path).has_value());

    auto mgr = osf::throwing::load(g.path);   // by value, no Result
    ASSERT_NE(mgr.channel("Sensor/T"), nullptr);
    EXPECT_FALSE(osf::channelIsEmpty(*mgr.channel("Sensor/T")));
}

TEST(Throwing, load_missing_file_throws) {
    auto const path =
        std::filesystem::temp_directory_path() / "osf_no_such_file_xyz.osf";
    try {
        (void)osf::throwing::load(path);
        FAIL() << "expected osf::Exception";
    } catch (osf::Exception const& e) {
        EXPECT_EQ(e.code(), osf::Error::Code::IoError);
        EXPECT_NE(std::string(e.what()).find("failed to open"),
                  std::string::npos)
            << e.what();
    }
}

TEST(Throwing, load_from_istream_success) {
    std::istringstream in(fixture_bytes(), std::ios::binary);
    auto mgr = osf::throwing::load(in);
    EXPECT_NE(mgr.channel("Sensor/T"), nullptr);
}

TEST(Throwing, load_from_istream_garbage_throws) {
    std::istringstream in("not an osf file", std::ios::binary);
    EXPECT_THROW((void)osf::throwing::load(in), osf::Exception);
}

// ── write ─────────────────────────────────────────────────────────────

TEST(Throwing, write_to_file_round_trips) {
    std::istringstream in(fixture_bytes(), std::ios::binary);
    auto const src = osf::throwing::load(in);

    TempFileGuard g{make_temp_path()};
    osf::throwing::writeToFile(src, g.path);   // void, throws on error

    auto const reloaded = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;
    EXPECT_TRUE(osf_test::roundtrip_managers_equal(src, *reloaded));
}

TEST(Throwing, write_to_ostream_round_trips) {
    std::istringstream in(fixture_bytes(), std::ios::binary);
    auto const src = osf::throwing::load(in);

    std::ostringstream out;
    osf::throwing::writeTo(src, out);

    std::istringstream back(out.str(), std::ios::binary);
    auto const reloaded = osf::DataManager::loadFromStream(back);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;
    EXPECT_TRUE(osf_test::roundtrip_managers_equal(src, *reloaded));
}

// ── unwrap ────────────────────────────────────────────────────────────

TEST(Throwing, unwrap_void_success_does_not_throw) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    auto const idx = osf::throwing::unwrap(w.addChannel([] {
        osf::ChannelDef d;
        d.name = "a";
        d.dataType = osf::DataType::Double;
        d.channelType = osf::ChannelType::Scalar;
        return d;
    }()));
    EXPECT_EQ(idx, 0u);                       // unwrap(Result<uint16_t>)
    EXPECT_NO_THROW(osf::throwing::unwrap(w.start()));  // unwrap(Result<void>)
    EXPECT_NO_THROW(osf::throwing::unwrap(w.close()));
}

TEST(Throwing, unwrap_void_failure_throws) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    // start() with no channels → InvalidArgument.
    try {
        osf::throwing::unwrap(w.start());
        FAIL() << "expected osf::Exception";
    } catch (osf::Exception const& e) {
        EXPECT_EQ(e.code(), osf::Error::Code::InvalidArgument);
    }
}

TEST(Throwing, unwrap_value_returns_underlying_value) {
    // unwrap on a Result<T> from the core API yields T.
    auto const dt = osf::throwing::unwrap(osf::parseDataType("double"));
    EXPECT_EQ(dt, osf::DataType::Double);
}

// ── Exception structured detail ───────────────────────────────────────

TEST(Throwing, exception_carries_code_and_message) {
    try {
        (void)osf::throwing::unwrap(osf::parseDataType("gpsdata"));
        FAIL() << "expected osf::Exception";
    } catch (osf::Exception const& e) {
        EXPECT_EQ(e.code(), osf::Error::Code::RemovedInSpec);
        EXPECT_EQ(e.error().code, osf::Error::Code::RemovedInSpec);
        EXPECT_EQ(e.error().message, std::string(e.what()));
        EXPECT_FALSE(e.error().message.empty());
    }
}
