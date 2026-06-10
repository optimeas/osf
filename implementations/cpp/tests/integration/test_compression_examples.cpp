// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Integration tests for transparent OSFZ decompression on read.
// Re-compresses a real reference file to gzip and zlib in memory and
// confirms it loads through DataManager identically to the plain source,
// and loads the real gzip-OSFZ field sample weather_station.osfz.

#include "roundtrip_helper.hpp"

#include <zlib.h>

#include <gtest/gtest.h>

#include <osf/compression.hpp>
#include <osf/data_channel.hpp>
#include <osf/manager.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

class CompressionExamplesTest : public ::testing::Test {
protected:
    static std::filesystem::path examples_dir() {
        return std::filesystem::path{OSF_EXAMPLES_DIR};
    }

    static std::string read_file(std::filesystem::path const& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    }

    // Deflate with explicit windowBits: 15 → zlib, 15 + 16 → gzip.
    static std::string deflate_with(std::string const& input,
                                    int window_bits) {
        z_stream zs{};
        EXPECT_EQ(deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                               window_bits, 8, Z_DEFAULT_STRATEGY),
                  Z_OK);
        std::vector<unsigned char> out(static_cast<std::size_t>(
            deflateBound(&zs, static_cast<uLong>(input.size()))));
        zs.next_in =
            reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
        zs.avail_in = static_cast<uInt>(input.size());
        zs.next_out = out.data();
        zs.avail_out = static_cast<uInt>(out.size());
        EXPECT_EQ(deflate(&zs, Z_FINISH), Z_STREAM_END);
        out.resize(out.size() - zs.avail_out);
        deflateEnd(&zs);
        return std::string(reinterpret_cast<char const*>(out.data()),
                           out.size());
    }
};

}  // namespace

// ---------------------------------------------------------------------
// A gzip/zlib re-wrap of steam_loco.osf must load identically to plain.
// ---------------------------------------------------------------------

TEST_F(CompressionExamplesTest, steam_loco_gzip_and_zlib_match_plain) {
    auto const plain_path = examples_dir() / "steam_loco.osf";
    if (!std::filesystem::exists(plain_path)) {
        GTEST_SKIP() << "steam_loco.osf missing";
    }
    auto const plain = osf::DataManager::load_from_file(plain_path);
    ASSERT_TRUE(plain.has_value()) << plain.error().message;
    EXPECT_FALSE(plain->stats.compressed);

    std::string const raw = read_file(plain_path);

    struct Case {
        std::string label;
        int window_bits;
        osf::CompressionFormat format;
    };
    Case const cases[] = {
        {"gzip", 15 + 16, osf::CompressionFormat::Gzip},
        {"zlib", 15, osf::CompressionFormat::Zlib},
    };

    for (auto const& c : cases) {
        std::istringstream src(deflate_with(raw, c.window_bits),
                               std::ios::binary);
        auto const got = osf::DataManager::load_from_stream(src);
        ASSERT_TRUE(got.has_value())
            << c.label << ": " << got.error().message;
        EXPECT_TRUE(got->stats.compressed) << c.label;
        EXPECT_EQ(got->stats.compression_format, c.format) << c.label;
        EXPECT_TRUE(osf_test::roundtrip_managers_equal(*plain, *got))
            << c.label;
    }
}

// ---------------------------------------------------------------------
// The real gzip-OSFZ field sample loads transparently.
// ---------------------------------------------------------------------

TEST_F(CompressionExamplesTest, weather_station_osfz_loads_transparently) {
    auto const path = examples_dir() / "weather_station.osfz";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "weather_station.osfz missing";
    }
    auto const mgr = osf::DataManager::load_from_file(path);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    EXPECT_TRUE(mgr->stats.compressed);
    EXPECT_EQ(mgr->stats.compression_format, osf::CompressionFormat::Gzip);
    ASSERT_FALSE(mgr->channels().empty());

    bool any_non_empty = false;
    for (auto const& ch : mgr->channels()) {
        if (!osf::channel_is_empty(ch)) {
            any_non_empty = true;
            break;
        }
    }
    EXPECT_TRUE(any_non_empty);
}
