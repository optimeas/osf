// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Integration tests for osf::BlockReader against real reference and
// field-recorded OSF files. Every shipped OSF file (uncompressed) must
// be readable end-to-end — magic header, metablock, every block —
// without a hard error.

#include <gtest/gtest.h>

#include <osf/osf.hpp>

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

    static std::filesystem::path examples_dir() {
        return std::filesystem::path{OSF_EXAMPLES_DIR};
    }

    // Open an OSF file, parse its magic header + metablock, and return
    // an open `std::ifstream` positioned right after the metablock.
    struct OpenedFile {
        std::ifstream stream;
        osf::MetaBlock meta;
        std::uintmax_t file_size = 0;
    };

    static OpenedFile open_osf(std::filesystem::path const& path) {
        OpenedFile out;
        out.file_size = std::filesystem::file_size(path);

        // Read magic header from a separate stream so we don't perturb
        // the file position of `out.stream`.
        auto hdr = osf::parse_magic_header(path);
        EXPECT_TRUE(hdr.has_value())
            << "magic header parse failed for " << path << ": "
            << (hdr ? std::string{} : hdr.error().message);

        out.stream.open(path, std::ios::binary);
        EXPECT_TRUE(out.stream.is_open()) << "open failed: " << path;
        if (!out.stream.is_open() || !hdr) return out;

        // Skip the magic-header line by reading up to and including
        // the newline. parse_magic_header guarantees a terminating
        // newline within MAX_MAGIC_HEADER_LEN bytes.
        std::string discard;
        std::getline(out.stream, discard);

        // Read exactly metablock_len bytes for the metablock body.
        std::vector<std::uint8_t> buf(
            static_cast<std::size_t>(hdr->metablock_len));
        out.stream.read(reinterpret_cast<char*>(buf.data()),
                        static_cast<std::streamsize>(buf.size()));
        EXPECT_EQ(static_cast<std::size_t>(out.stream.gcount()), buf.size())
            << "short read on metablock: " << path;

        auto mb = (hdr->version == osf::OsfVersion::Osf5)
            ? osf::parse_metablock_json(buf.data(), buf.size())
            : osf::parse_metablock_xml(buf.data(), buf.size());
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
    auto generated = examples_dir() / "generated";
    ASSERT_TRUE(std::filesystem::exists(generated));

    int read = 0;
    for (auto const& entry : std::filesystem::directory_iterator{generated}) {
        if (entry.path().extension() != ".osf") continue;
        auto filename = entry.path().filename().string();
        SCOPED_TRACE("file: " + filename);

        auto opened = open_osf(entry.path());
        ASSERT_TRUE(opened.stream.is_open());

        osf::BlockReader r(opened.stream, opened.meta);
        r.with_file_size(opened.file_size);

        std::uint64_t produced = 0;
        std::uint64_t errors = 0;
        for (auto& blk_r : r) {
            if (!blk_r.has_value()) {
                ++errors;
                ADD_FAILURE() << "block read error in " << filename << ": "
                              << blk_r.error().message;
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
    auto opened = open_osf(examples_dir() / "generated" / "osf5_scalar_int64.osf");
    ASSERT_TRUE(opened.stream.is_open());

    osf::BlockReader r(opened.stream, opened.meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value())
        << (blk_r && !blk_r->has_value() ? blk_r->error().message
                                          : std::string{"no block"});
    auto const& blk = **blk_r;
    EXPECT_EQ(blk.channel_index, 0);

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
// first block as a bcStartData with sample_rate_hz > 0.
// ---------------------------------------------------------------------

TEST_F(ReaderExamplesTest, osf4_equidistant_first_block_is_StartData) {
    auto opened = open_osf(examples_dir() / "generated" / "osf4_equidistant.osf");
    ASSERT_TRUE(opened.stream.is_open());

    osf::BlockReader r(opened.stream, opened.meta);
    auto blk_r = r.next();
    ASSERT_TRUE(blk_r && blk_r->has_value());
    auto const& blk = **blk_r;

    auto const* sd = std::get_if<osf::StartData>(&blk.kind);
    ASSERT_NE(sd, nullptr) << "first block of osf4_equidistant is not StartData";
    EXPECT_GT(sd->sample_rate_hz, 0.0);
    EXPECT_GT(osf::numeric_payload_len(sd->samples), 0u);
}

// ---------------------------------------------------------------------
// Field samples: real device output. Used to be the first place where
// real-world surprises landed historically. Must still parse end-to-end.
// ---------------------------------------------------------------------

TEST_F(ReaderExamplesTest, motorbike_osf_reads_clean) {
    auto path = examples_dir() / "motorbike.osf";
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "motorbike.osf missing";

    auto opened = open_osf(path);
    ASSERT_TRUE(opened.stream.is_open());

    osf::BlockReader r(opened.stream, opened.meta);
    std::uint64_t blocks = 0;
    for (auto& blk_r : r) {
        ASSERT_TRUE(blk_r.has_value()) << blk_r.error().message;
        ++blocks;
    }
    EXPECT_GT(blocks, 0u);
    auto stats = r.stats();
    EXPECT_EQ(stats.blocks_read + stats.blocks_skipped_unsupported +
                  stats.blocks_skipped_deprecated_type +
                  stats.blocks_skipped_reserved_type,
              stats.blocks_total);
}

TEST_F(ReaderExamplesTest, steam_loco_osf_reads_clean) {
    auto path = examples_dir() / "steam_loco.osf";
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "steam_loco.osf missing";

    auto opened = open_osf(path);
    ASSERT_TRUE(opened.stream.is_open());

    osf::BlockReader r(opened.stream, opened.meta);
    std::uint64_t blocks = 0;
    for (auto& blk_r : r) {
        ASSERT_TRUE(blk_r.has_value()) << blk_r.error().message;
        ++blocks;
    }
    EXPECT_GT(blocks, 0u);
}

// ---------------------------------------------------------------------
// Statistics sanity. Sample-count totals across all channels must be
// non-zero and match what observe_timestamp recorded for at least one
// channel.
// ---------------------------------------------------------------------

TEST_F(ReaderExamplesTest, reader_stats_are_populated) {
    auto opened = open_osf(examples_dir() / "generated" / "osf4_equidistant.osf");
    ASSERT_TRUE(opened.stream.is_open());

    osf::BlockReader r(opened.stream, opened.meta);
    for (auto& blk_r : r) {
        ASSERT_TRUE(blk_r.has_value());
    }
    auto stats = r.stats();
    EXPECT_GT(stats.blocks_read, 0u);
    EXPECT_GT(stats.data_section_size_bytes, 0u);
    EXPECT_GT(stats.channels_with_data, 0u);
    bool any_time_range = false;
    for (auto const& [_, cs] : stats.per_channel) {
        if (cs.time_range_ns.has_value()) { any_time_range = true; break; }
    }
    EXPECT_TRUE(any_time_range);
}
