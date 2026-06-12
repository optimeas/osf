// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Integration tests for osf::DataManager against real reference and
// field-recorded OSF files. Every shipped uncompressed OSF file must
// load end-to-end through the DataManager API and produce a typed
// channel list with non-empty samples on at least one channel per file.

#include <gtest/gtest.h>

#include <osf/osf.h>

#include <cstddef>
#include <filesystem>
#include <string>

namespace {

class ManagerExamplesTest : public ::testing::Test {
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

}  // namespace

// ---------------------------------------------------------------------
// Every generated reference file must load through DataManager and
// produce a typed channel list with at least one non-empty channel.
// ---------------------------------------------------------------------

TEST_F(ManagerExamplesTest, every_generated_reference_file_loads_clean) {
    auto generated = examplesDir() / "generated";
    ASSERT_TRUE(std::filesystem::exists(generated));

    int read = 0;
    for (auto const& entry : std::filesystem::directory_iterator{generated}) {
        if (entry.path().extension() != ".osf") continue;
        auto filename = entry.path().filename().string();
        SCOPED_TRACE("file: " + filename);

        auto mgr = osf::DataManager::loadFromFile(entry.path());
        ASSERT_TRUE(mgr.has_value()) << "load failed for " << filename
                                     << ": " << mgr.error().message;

        EXPECT_FALSE(mgr->channels().empty());
        EXPECT_GT(mgr->stats.blocksRead, 0u);

        // At least one channel must have produced samples.
        bool anySamples = false;
        for (auto const& ch : mgr->channels()) {
            if (osf::channelSampleCount(ch) > 0) {
                anySamples = true;
                break;
            }
        }
        EXPECT_TRUE(anySamples);

        // channel-by-name lookup works for the first channel.
        if (!mgr->channels().empty()) {
            auto const& first_name =
                osf::channelName(mgr->channels().front());
            EXPECT_NE(mgr->channel(first_name), nullptr);
        }

        ++read;
    }

    EXPECT_GT(read, 0);
}

// ---------------------------------------------------------------------
// Snapshot probes on two specific reference files.
// ---------------------------------------------------------------------

TEST_F(ManagerExamplesTest, osf4_equidistant_first_channel_is_equidistant) {
    auto mgr = osf::DataManager::loadFromFile(
        examplesDir() / "generated" / "osf4_equidistant.osf");
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;

    ASSERT_FALSE(mgr->channels().empty());
    auto const& dc = mgr->channels().front();
    auto const* eq = std::get_if<osf::EquidistantChannel>(&dc);
    ASSERT_NE(eq, nullptr) << "first channel of osf4_equidistant is not Equidistant";
    EXPECT_FALSE(eq->segments.empty());
    EXPECT_GT(eq->segments.front().sampleRateHz, 0.0);

    // samplesVector reconstructs per-sample timestamps; cross-check
    // count with the segment totals.
    auto samples = eq->samplesVector();
    std::size_t expected = 0;
    for (auto const& s : eq->segments) expected += s.sampleCount;
    EXPECT_EQ(samples.size(), expected);
}

TEST_F(ManagerExamplesTest, osf5_gpslocation_has_a_gps_channel) {
    auto mgr = osf::DataManager::loadFromFile(
        examplesDir() / "generated" / "osf5_gpslocation.osf");
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;

    bool sawGps = false;
    for (auto const& ch : mgr->channels()) {
        if (osf::channelDataType(ch) == osf::DataType::GpsLocation) {
            sawGps = true;
            // The reference file emits GPS as a timestamped channel.
            auto const* ts = std::get_if<osf::TimestampedChannel>(&ch);
            ASSERT_NE(ts, nullptr);
            EXPECT_GT(osf::numericValuesLen(ts->values), 0u);
        }
    }
    EXPECT_TRUE(sawGps);
}

TEST_F(ManagerExamplesTest, osf4_timestamped_string_has_string_channel) {
    auto mgr = osf::DataManager::loadFromFile(
        examplesDir() / "generated" / "osf4_timestamped_string.osf");
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;

    bool sawString = false;
    for (auto const& ch : mgr->channels()) {
        if (osf::channelDataType(ch) == osf::DataType::String) {
            sawString = true;
            auto const* var = std::get_if<osf::VariableChannel>(&ch);
            ASSERT_NE(var, nullptr);
            ASSERT_TRUE(var->stringValues.has_value());
            EXPECT_FALSE(var->stringValues->empty());
        }
    }
    EXPECT_TRUE(sawString);
}

// ---------------------------------------------------------------------
// Field samples: motorbike.osf + steam_loco.osf must round-trip
// through DataManager. These exercise the deprecated-block tolerance
// of the reader plus the manager state machine.
// ---------------------------------------------------------------------

TEST_F(ManagerExamplesTest, motorbike_osf_loads_clean) {
    auto path = examplesDir() / "motorbike.osf";
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "motorbike.osf missing";
    auto mgr = osf::DataManager::loadFromFile(path);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    EXPECT_FALSE(mgr->channels().empty());
    EXPECT_GT(mgr->stats.blocksTotal, 0u);
}

TEST_F(ManagerExamplesTest, steam_loco_osf_loads_clean) {
    auto path = examplesDir() / "steam_loco.osf";
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "steam_loco.osf missing";
    auto mgr = osf::DataManager::loadFromFile(path);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    EXPECT_FALSE(mgr->channels().empty());
    EXPECT_GT(mgr->stats.blocksTotal, 0u);
}

// ---------------------------------------------------------------------
// OSFZ: weather_station.osfz loads transparently.
// ---------------------------------------------------------------------

TEST_F(ManagerExamplesTest, weather_station_osfz_loads_transparently) {
    auto path = examplesDir() / "weather_station.osfz";
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "weather_station.osfz missing";
    auto mgr = osf::DataManager::loadFromFile(path);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    EXPECT_TRUE(mgr->stats.compressed);
    EXPECT_EQ(mgr->stats.compressionFormat, osf::CompressionFormat::Gzip);
    EXPECT_FALSE(mgr->channels().empty());
}
