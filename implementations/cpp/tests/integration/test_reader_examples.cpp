// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Integration tests for osf::BlockReader against real reference and
// field-recorded OSF files. Every shipped OSF file (uncompressed) must
// be readable end-to-end — magic header, metablock, every block —
// without a hard error.

#include <gtest/gtest.h>

#include <osf/osf.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class ReaderExamplesTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        auto dir = std::filesystem::path{OSF_EXAMPLES_DIR};
        ASSERT_TRUE(std::filesystem::exists(dir))
            << "OSF_EXAMPLES_DIR does not exist: " << dir;
    }

    static std::filesystem::path examplesDir() {
        return std::filesystem::path{OSF_EXAMPLES_DIR};
    }

    // Open an OSF file, parse its magic header + metablock, and return
    // an open `std::ifstream` positioned right after the metablock.
    struct OpenedFile {
        std::ifstream stream;
        osf::MetaBlock meta;
        std::uintmax_t fileSize = 0;
    };

    static OpenedFile openOsf(std::filesystem::path const& path) {
        OpenedFile out;
        out.fileSize = std::filesystem::file_size(path);

        // Read magic header from a separate stream so we don't perturb
        // the file position of `out.stream`.
        auto hdr = osf::parseMagicHeader(path);
        EXPECT_TRUE(hdr.has_value())
            << "magic header parse failed for " << path << ": "
            << (hdr ? std::string{} : hdr.error().message);

        out.stream.open(path, std::ios::binary);
        EXPECT_TRUE(out.stream.is_open()) << "open failed: " << path;
        if (!out.stream.is_open() || !hdr) return out;

        // Skip the magic-header line by reading up to and including
        // the newline. parseMagicHeader guarantees a terminating
        // newline within MAX_MAGIC_HEADER_LEN bytes.
        std::string discard;
        std::getline(out.stream, discard);

        // Read exactly metablockLen bytes for the metablock body.
        std::vector<std::uint8_t> buf(
            static_cast<std::size_t>(hdr->metablockLen));
        out.stream.read(reinterpret_cast<char*>(buf.data()),
                        static_cast<std::streamsize>(buf.size()));
        EXPECT_EQ(static_cast<std::size_t>(out.stream.gcount()), buf.size())
            << "short read on metablock: " << path;

        auto mb = (hdr->version == osf::OsfVersion::Osf5)
            ? osf::parseMetablockJson(buf.data(), buf.size())
            : osf::parseMetablockXml(buf.data(), buf.size());
        EXPECT_TRUE(mb.has_value()) << "metablock parse failed for "
                                    << path << ": "
                                    << (mb ? std::string{} : mb.error().message);
        if (mb) out.meta = std::move(*mb);
        return out;
    }
};

}  // namespace

// ---------------------------------------------------------------------
// Generic loop: every shipped uncompressed OSF reference file must
// stream-read end-to-end with at least one decoded block per file.
// ---------------------------------------------------------------------

TEST_F(ReaderExamplesTest, every_generated_reference_file_reads_clean) {
    auto generated = examplesDir() / "generated";
    ASSERT_TRUE(std::filesystem::exists(generated));

    int read = 0;
    for (auto const& entry : std::filesystem::directory_iterator{generated}) {
        if (entry.path().extension() != ".osf") continue;
        auto filename = entry.path().filename().string();
        SCOPED_TRACE("file: " + filename);

        auto opened = openOsf(entry.path());
        ASSERT_TRUE(opened.stream.is_open());

        osf::BlockReader r(opened.stream, opened.meta);
        r.withFileSize(opened.fileSize);

        std::uint64_t produced = 0;
        std::uint64_t errors = 0;
        for (auto& blkR : r) {
            if (!blkR.has_value()) {
                ++errors;
                ADD_FAILURE() << "block read error in " << filename << ": "
                              << blkR.error().message;
                break;
            }
            ++produced;
        }
        EXPECT_EQ(errors, 0u) << filename;
        EXPECT_GT(produced, 0u) << filename << ": no blocks produced";
        ++read;
    }

    EXPECT_GT(read, 0) << "no .osf files found under " << generated;
}

// ---------------------------------------------------------------------
// One specific OSF5 file: the int64 scalar reference. Pins the first
// block (single-sample bcAbsTimeStampData, ctl=0x08) so a regression
// in the AbsTs decode fails loudly.
// ---------------------------------------------------------------------

TEST_F(ReaderExamplesTest, osf5_scalar_int64_first_block_decodes) {
    auto opened = openOsf(examplesDir() / "generated" / "osf5_scalar_int64.osf");
    ASSERT_TRUE(opened.stream.is_open());

    osf::BlockReader r(opened.stream, opened.meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value())
        << (blkR && !blkR->has_value() ? blkR->error().message
                                          : std::string{"no block"});
    auto const& blk = **blkR;
    EXPECT_EQ(blk.channelIndex, 0);

    auto const* ad = std::get_if<osf::AbsTimestampData>(&blk.kind);
    ASSERT_NE(ad, nullptr) << "first block of osf5_scalar_int64 is not AbsTs";

    auto const* v =
        std::get_if<std::vector<std::pair<std::int64_t, std::int64_t>>>(
            &ad->samples);
    ASSERT_NE(v, nullptr) << "AbsTs samples variant is not Int64";
    EXPECT_FALSE(v->empty());
}

// ---------------------------------------------------------------------
// One specific OSF4 file: equidistant generated reference. Pins the
// first block as a bcStartData with sampleRateHz > 0.
// ---------------------------------------------------------------------

TEST_F(ReaderExamplesTest, osf4_equidistant_first_block_is_StartData) {
    auto opened = openOsf(examplesDir() / "generated" / "osf4_equidistant.osf");
    ASSERT_TRUE(opened.stream.is_open());

    osf::BlockReader r(opened.stream, opened.meta);
    auto blkR = r.next();
    ASSERT_TRUE(blkR && blkR->has_value());
    auto const& blk = **blkR;

    auto const* sd = std::get_if<osf::StartData>(&blk.kind);
    ASSERT_NE(sd, nullptr) << "first block of osf4_equidistant is not StartData";
    EXPECT_GT(sd->sampleRateHz, 0.0);
    EXPECT_GT(osf::numericPayloadLen(sd->samples), 0u);
}

// ---------------------------------------------------------------------
// Field samples: real device output. Used to be the first place where
// real-world surprises landed historically. Must still parse end-to-end.
// ---------------------------------------------------------------------

TEST_F(ReaderExamplesTest, motorbike_osf_reads_clean) {
    auto path = examplesDir() / "motorbike.osf";
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "motorbike.osf missing";

    auto opened = openOsf(path);
    ASSERT_TRUE(opened.stream.is_open());

    osf::BlockReader r(opened.stream, opened.meta);
    std::uint64_t blocks = 0;
    for (auto& blkR : r) {
        ASSERT_TRUE(blkR.has_value()) << blkR.error().message;
        ++blocks;
    }
    EXPECT_GT(blocks, 0u);
    auto stats = r.stats();
    // Cross-check blocksTotal against an independent per-channel roll-up
    // rather than a hand-maintained sum of skip-reason counters — the
    // latter drifts every time a new SkipReason::Kind is added (this is
    // the same bug class fixed in BlockReader::stats() for OSF-UP3: two
    // integrity-related terms and blocksSkippedZeroLength were all missing
    // here before). `ChannelStats::blocksRead` / `blocksSkipped` are
    // incremented for every block regardless of which skip reason applies,
    // so this sum can never silently omit a future counter the way the old
    // hand-summed comparison did.
    std::uint64_t perChannelTotal = 0;
    for (auto const& [_, cs] : stats.perChannel) {
        perChannelTotal += cs.blocksRead + cs.blocksSkipped;
    }
    EXPECT_EQ(perChannelTotal, stats.blocksTotal);
}

TEST_F(ReaderExamplesTest, steam_loco_osf_reads_clean) {
    auto path = examplesDir() / "steam_loco.osf";
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "steam_loco.osf missing";

    auto opened = openOsf(path);
    ASSERT_TRUE(opened.stream.is_open());

    osf::BlockReader r(opened.stream, opened.meta);
    std::uint64_t blocks = 0;
    for (auto& blkR : r) {
        ASSERT_TRUE(blkR.has_value()) << blkR.error().message;
        ++blocks;
    }
    EXPECT_GT(blocks, 0u);
}

// ---------------------------------------------------------------------
// Statistics sanity. Sample-count totals across all channels must be
// non-zero and match what observeTimestamp recorded for at least one
// channel.
// ---------------------------------------------------------------------

TEST_F(ReaderExamplesTest, reader_stats_are_populated) {
    auto opened = openOsf(examplesDir() / "generated" / "osf4_equidistant.osf");
    ASSERT_TRUE(opened.stream.is_open());

    osf::BlockReader r(opened.stream, opened.meta);
    for (auto& blkR : r) {
        ASSERT_TRUE(blkR.has_value());
    }
    auto stats = r.stats();
    EXPECT_GT(stats.blocksRead, 0u);
    EXPECT_GT(stats.dataSectionSizeBytes, 0u);
    EXPECT_GT(stats.channelsWithData, 0u);
    bool anyTimeRange = false;
    for (auto const& [_, cs] : stats.perChannel) {
        if (cs.timeRangeNs.has_value()) { anyTimeRange = true; break; }
    }
    EXPECT_TRUE(anyTimeRange);
}
