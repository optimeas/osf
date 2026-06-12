// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Unit tests for the stats helpers in <osf/stats.h>.

#include <gtest/gtest.h>

#include <osf/stats.h>

#include <chrono>
#include <sstream>

namespace {

TEST(ChannelStats, observe_timestamp_grows_range_in_both_directions) {
    osf::ChannelStats cs;
    cs.observeTimestamp(100);
    cs.observeTimestamp(200);
    cs.observeTimestamp(50);
    ASSERT_TRUE(cs.timeRangeNs.has_value());
    EXPECT_EQ(cs.timeRangeNs->first, 50);
    EXPECT_EQ(cs.timeRangeNs->second, 200);
}

TEST(StatsFormat, fmt_bytes_picks_unit_thresholds) {
    EXPECT_EQ(osf::formatBytes(0),       "0 B");
    EXPECT_EQ(osf::formatBytes(1023),    "1023 B");
    EXPECT_EQ(osf::formatBytes(1024),    "1.00 KB");
    EXPECT_EQ(osf::formatBytes(1024u * 1024u), "1.00 MB");
    EXPECT_EQ(osf::formatBytes(1024ull * 1024ull * 1024ull), "1.00 GB");
}

TEST(StatsFormat, fmt_duration_picks_ms_or_s) {
    EXPECT_EQ(osf::formatDuration(std::chrono::milliseconds(7)),  "7 ms");
    EXPECT_EQ(osf::formatDuration(std::chrono::milliseconds(999)), "999 ms");
    // 1500 ms → 1.50 s
    EXPECT_EQ(osf::formatDuration(std::chrono::milliseconds(1500)), "1.50 s");
}

TEST(StatsFormat, compressionFormatName) {
    EXPECT_EQ(osf::compressionFormatName(osf::CompressionFormat::None), "none");
    EXPECT_EQ(osf::compressionFormatName(osf::CompressionFormat::Zlib), "zlib");
    EXPECT_EQ(osf::compressionFormatName(osf::CompressionFormat::Gzip), "gzip");
}

TEST(StatsFormat, reader_stats_ostream_emits_expected_lines) {
    osf::ReaderStats stats;
    stats.fileSizeBytes = 2048;
    stats.headerSizeBytes = 24;
    stats.metablockSizeBytes = 800;
    stats.dataSectionSizeBytes = 1224;
    stats.elapsed = std::chrono::milliseconds(7);
    stats.channelsTotal = 5;
    stats.channelsWithData = 4;
    stats.channelsUnsupported = 1;
    stats.blocksTotal = 100;
    stats.blocksRead = 99;
    stats.blocksSkippedUnsupported = 1;
    stats.trailerSeen = true;

    std::ostringstream oss;
    oss << stats;
    auto const text = oss.str();
    EXPECT_NE(text.find("File size:"),                  std::string::npos);
    EXPECT_NE(text.find("Channels total:        5"),    std::string::npos);
    EXPECT_NE(text.find("Trailer block:         present"), std::string::npos);
}

TEST(StatsFormat, channel_stats_ostream_one_line_summary) {
    osf::ChannelStats cs;
    cs.name = "test";
    cs.blocksRead = 3;
    cs.blocksSkipped = 1;
    cs.samplesTotal = 100;
    cs.bytesPayload = 2048;
    cs.segments = 2;
    cs.observeTimestamp(10);
    cs.observeTimestamp(20);

    std::ostringstream oss;
    oss << cs;
    auto const text = oss.str();
    EXPECT_NE(text.find("blocks=3+1skipped"), std::string::npos);
    EXPECT_NE(text.find("samples=100"),       std::string::npos);
    EXPECT_NE(text.find("segments=2"),        std::string::npos);
    EXPECT_NE(text.find("10..20"),            std::string::npos);
}

}  // namespace
