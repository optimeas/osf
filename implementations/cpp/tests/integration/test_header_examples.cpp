// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <gtest/gtest.h>

#include <osf/osf.hpp>

#include <filesystem>
#include <string>

namespace {

class HeaderExamplesTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        auto dir = std::filesystem::path{OSF_EXAMPLES_DIR};
        ASSERT_TRUE(std::filesystem::exists(dir))
            << "OSF_EXAMPLES_DIR does not exist: " << dir;
        ASSERT_TRUE(std::filesystem::exists(dir / "motorbike.osf"))
            << "examples/motorbike.osf missing — OSF_EXAMPLES_DIR misconfigured?";
    }

    static std::filesystem::path examples_dir() {
        return std::filesystem::path{OSF_EXAMPLES_DIR};
    }
};

}  // namespace

// ----- 1: motorbike.osf — production OSF4 file -----

TEST_F(HeaderExamplesTest, motorbike_osf_has_valid_header) {
    auto result = osf::parse_magic_header(examples_dir() / "motorbike.osf");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->version, osf::OsfVersion::Osf4);
    EXPECT_GT(result->metablock_len, 0u);
}

// ----- 2: steam_loco.osf — production OSF4 file -----

TEST_F(HeaderExamplesTest, steam_loco_osf_has_valid_header) {
    auto result = osf::parse_magic_header(examples_dir() / "steam_loco.osf");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->version, osf::OsfVersion::Osf4);
    EXPECT_GT(result->metablock_len, 0u);
}

// ----- 3: weather_station.osfz — gzip-compressed, must fail -----

TEST_F(HeaderExamplesTest, weather_station_osfz_fails_until_phase8) {
    auto result = osf::parse_magic_header(examples_dir() / "weather_station.osfz");
    ASSERT_FALSE(result.has_value())
        << "OSFZ files should not be parseable as plain OSF until Phase 8";
    // The gzip magic 0x1F 0x8B and the high-entropy bytes that follow
    // can land on any of these three paths, depending on whether the
    // first 128 bytes contain a stray '\n' and a stray ' '.
    auto code = result.error().code;
    EXPECT_TRUE(code == osf::Error::Code::UnsupportedVersion ||
                code == osf::Error::Code::InvalidMagicHeader ||
                code == osf::Error::Code::MagicHeaderTooLong)
        << "unexpected error code: " << osf::error_category_name(code);
}

// ----- 4: generated reference files — all parse, version per filename -----

TEST_F(HeaderExamplesTest, generated_files_all_parse) {
    auto generated = examples_dir() / "generated";
    ASSERT_TRUE(std::filesystem::exists(generated))
        << "examples/generated/ missing";

    int parsed = 0;
    for (auto const& entry : std::filesystem::directory_iterator{generated}) {
        if (entry.path().extension() != ".osf") {
            continue;
        }
        auto filename = entry.path().filename().string();
        SCOPED_TRACE("file: " + filename);

        auto result = osf::parse_magic_header(entry.path());
        ASSERT_TRUE(result.has_value()) << result.error().message;
        EXPECT_GT(result->metablock_len, 0u);

        // Filename prefix tells us the expected version.
        if (filename.rfind("osf4_", 0) == 0) {
            EXPECT_EQ(result->version, osf::OsfVersion::Osf4);
        } else if (filename.rfind("osf5_", 0) == 0) {
            EXPECT_EQ(result->version, osf::OsfVersion::Osf5);
        } else {
            ADD_FAILURE() << "unexpected filename pattern: " << filename;
        }

        ++parsed;
    }

    EXPECT_GT(parsed, 0) << "no .osf files found under " << generated;
}
