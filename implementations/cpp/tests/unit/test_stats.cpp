// SPDX-License-Identifier: MIT
//
// Unit tests for the stats helpers in <osf/stats.hpp>.

#include <gtest/gtest.h>

#include <osf/stats.hpp>

#include <chrono>
#include <sstream>

namespace {

TEST(ChannelStats, observe_timestamp_grows_range_in_both_directions) {
    osf::ChannelStats cs;
    cs.observe_timestamp(100);
    cs.observe_timestamp(200);
    cs.observe_timestamp(50);
    ASSERT_TRUE(cs.time_range_ns.has_value());
    EXPECT_EQ(cs.time_range_ns->first, 50);
    EXPECT_EQ(cs.time_range_ns->second, 200);
}

TEST(StatsFormat, fmt_bytes_picks_unit_thresholds) {
    EXPECT_EQ(osf::format_bytes(0),       "0 B");
    EXPECT_EQ(osf::format_bytes(1023),    "1023 B");
    EXPECT_EQ(osf::format_bytes(1024),    "1.00 KB");
    EXPECT_EQ(osf::format_bytes(1024u * 1024u), "1.00 MB");
    EXPECT_EQ(osf::format_bytes(1024ull * 1024ull * 1024ull), "1.00 GB");
}

TEST(StatsFormat, fmt_duration_picks_ms_or_s) {
    EXPECT_EQ(osf::format_duration(std::chrono::milliseconds(7)),  "7 ms");
    EXPECT_EQ(osf::format_duration(std::chrono::milliseconds(999)), "999 ms");
    // 1500 ms → 1.50 s
    EXPECT_EQ(osf::format_duration(std::chrono::milliseconds(1500)), "1.50 s");
}

TEST(StatsFormat, compression_format_name) {
    EXPECT_EQ(osf::compression_format_name(osf::CompressionFormat::None), "none");
    EXPECT_EQ(osf::compression_format_name(osf::CompressionFormat::Zlib), "zlib");
    EXPECT_EQ(osf::compression_format_name(osf::CompressionFormat::Gzip), "gzip");
}

TEST(StatsFormat, reader_stats_ostream_emits_expected_lines) {
    osf::ReaderStats stats;
    stats.file_size_bytes = 2048;
    stats.header_size_bytes = 24;
    stats.metablock_size_bytes = 800;
    stats.data_section_size_bytes = 1224;
    stats.elapsed = std::chrono::milliseconds(7);
    stats.channels_total = 5;
    stats.channels_with_data = 4;
    stats.channels_unsupported = 1;
    stats.blocks_total = 100;
    stats.blocks_read = 99;
    stats.blocks_skipped_unsupported = 1;
    stats.trailer_seen = true;

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
    cs.blocks_read = 3;
    cs.blocks_skipped = 1;
    cs.samples_total = 100;
    cs.bytes_payload = 2048;
    cs.segments = 2;
    cs.observe_timestamp(10);
    cs.observe_timestamp(20);

    std::ostringstream oss;
    oss << cs;
    auto const text = oss.str();
    EXPECT_NE(text.find("blocks=3+1skipped"), std::string::npos);
    EXPECT_NE(text.find("samples=100"),       std::string::npos);
    EXPECT_NE(text.find("segments=2"),        std::string::npos);
    EXPECT_NE(text.find("10..20"),            std::string::npos);
}

}  // namespace
