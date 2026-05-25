// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Integration tests for parse_metablock_xml against the OSF4 reference
// files under examples/generated/ plus the two field samples
// motorbike.osf and steam_loco.osf. These exercise the parser on real
// OSFGenerator output and on production-device output, not just on
// hand-built XML; that is the cross-validation step against the spec.

#include <gtest/gtest.h>

#include <osf/osf.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class MetablockXmlExamplesTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        auto dir = std::filesystem::path{OSF_EXAMPLES_DIR};
        ASSERT_TRUE(std::filesystem::exists(dir))
            << "OSF_EXAMPLES_DIR does not exist: " << dir;
    }

    static std::filesystem::path examples_dir() {
        return std::filesystem::path{OSF_EXAMPLES_DIR};
    }

    static std::vector<std::uint8_t> read_metablock(std::filesystem::path const& path) {
        auto header = osf::parse_magic_header(path);
        EXPECT_TRUE(header.has_value())
            << "magic header parse failed for " << path << ": "
            << (header ? "" : header.error().message);
        if (!header) return {};

        std::ifstream in{path, std::ios::binary};
        EXPECT_TRUE(in.is_open()) << "open failed: " << path;
        if (!in.is_open()) return {};

        // Skip the magic-header line and then read exactly
        // metablock_len bytes that follow.
        std::string line;
        std::getline(in, line);

        std::vector<std::uint8_t> buf(static_cast<std::size_t>(header->metablock_len));
        in.read(reinterpret_cast<char*>(buf.data()),
                static_cast<std::streamsize>(buf.size()));
        EXPECT_EQ(static_cast<std::size_t>(in.gcount()), buf.size())
            << "short read on " << path;
        return buf;
    }
};

}  // namespace

// ---------------------------------------------------------------------
// Snapshot test on one well-known OSF4 file. Locks down a few values so
// a regression in field parsing fails loudly rather than being lost in
// the generic loop below. Mirrors the JSON parser's
// osf5_equidistant_snapshot test.
// ---------------------------------------------------------------------

TEST_F(MetablockXmlExamplesTest, osf4_equidistant_snapshot) {
    auto bytes = read_metablock(examples_dir() / "generated" / "osf4_equidistant.osf");
    ASSERT_FALSE(bytes.empty());

    auto mb = osf::parse_metablock_xml(bytes.data(), bytes.size());
    ASSERT_TRUE(mb.has_value()) << mb.error().message;

    EXPECT_EQ(mb->file_info.version, 4u);
    ASSERT_TRUE(mb->file_info.creator.has_value());
    EXPECT_EQ(*mb->file_info.creator, "OSFGenerator/1.0");
    // OSFGenerator-style files use the short geolocation spelling.
    EXPECT_TRUE(mb->file_info.created_at_latitude.has_value());
    EXPECT_TRUE(mb->file_info.created_at_longitude.has_value());
    EXPECT_TRUE(mb->file_info.created_at_altitude.has_value());

    ASSERT_FALSE(mb->channels.empty());
    auto const& first = mb->channels[0];
    EXPECT_EQ(first.index, 0);
    EXPECT_EQ(first.channel_type, osf::ChannelType::Scalar);
    EXPECT_EQ(first.data_type, osf::DataType::Double);
    EXPECT_TRUE(first.time_increment_ns.has_value());
    EXPECT_GT(*first.time_increment_ns, 0);
}

// ---------------------------------------------------------------------
// Loop test: every osf4_*.osf reference file under examples/generated/
// must parse. No removed-in-spec datatypes are expected.
// ---------------------------------------------------------------------

TEST_F(MetablockXmlExamplesTest, all_osf4_generated_files_parse) {
    auto generated = examples_dir() / "generated";
    ASSERT_TRUE(std::filesystem::exists(generated))
        << "examples/generated/ missing";

    int parsed = 0;
    for (auto const& entry : std::filesystem::directory_iterator{generated}) {
        if (entry.path().extension() != ".osf") continue;
        auto filename = entry.path().filename().string();
        if (filename.rfind("osf4_", 0) != 0) continue;

        SCOPED_TRACE("file: " + filename);

        auto bytes = read_metablock(entry.path());
        ASSERT_FALSE(bytes.empty());

        auto mb = osf::parse_metablock_xml(bytes.data(), bytes.size());
        ASSERT_TRUE(mb.has_value()) << mb.error().message;
        EXPECT_EQ(mb->file_info.version, 4u);
        EXPECT_GT(mb->channels.size(), 0u);

        for (auto const& ch : mb->channels) {
            EXPECT_FALSE(ch.name.empty());
            EXPECT_TRUE(ch.size_of_length_value == 2 ||
                        ch.size_of_length_value == 4)
                << "channel " << ch.index << " (" << ch.name << ") has "
                << "sizeoflengthvalue=" << int{ch.size_of_length_value};
        }

        ++parsed;
    }

    EXPECT_GT(parsed, 0) << "no osf4_*.osf files found under " << generated;
}

// ---------------------------------------------------------------------
// Coverage probe: the gpslocation reference file must produce a
// channel of DataType::GpsLocation, mirroring the JSON parser's
// osf5_gpslocation probe.
// ---------------------------------------------------------------------

TEST_F(MetablockXmlExamplesTest, osf4_gpslocation_file_declares_gpslocation_channel) {
    auto bytes = read_metablock(examples_dir() / "generated" / "osf4_gpslocation.osf");
    ASSERT_FALSE(bytes.empty());

    auto mb = osf::parse_metablock_xml(bytes.data(), bytes.size());
    ASSERT_TRUE(mb.has_value()) << mb.error().message;

    bool saw_gps = false;
    for (auto const& ch : mb->channels) {
        if (ch.data_type == osf::DataType::GpsLocation) {
            saw_gps = true;
            EXPECT_EQ(ch.data_type_raw, "gpslocation");
        }
    }
    EXPECT_TRUE(saw_gps)
        << "osf4_gpslocation.osf produced no GpsLocation channel";
}

// ---------------------------------------------------------------------
// Field-sample coverage: motorbike.osf and steam_loco.osf are real
// production-device recordings. They exercise the encoding-tolerance
// path (CP1252-encoded `°` etc. inside an `encoding="UTF-8"` document)
// and the deprecated-field-tolerance path (scale/offset on every
// channel). The whole metablock must still parse.
// ---------------------------------------------------------------------

TEST_F(MetablockXmlExamplesTest, motorbike_osf_metablock_parses) {
    auto path = examples_dir() / "motorbike.osf";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "motorbike.osf not present";
    }
    auto bytes = read_metablock(path);
    ASSERT_FALSE(bytes.empty());
    auto mb = osf::parse_metablock_xml(bytes.data(), bytes.size());
    ASSERT_TRUE(mb.has_value()) << mb.error().message;
    EXPECT_EQ(mb->file_info.version, 4u);
    EXPECT_GT(mb->channels.size(), 0u);
}

TEST_F(MetablockXmlExamplesTest, steam_loco_osf_metablock_parses) {
    auto path = examples_dir() / "steam_loco.osf";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "steam_loco.osf not present";
    }
    auto bytes = read_metablock(path);
    ASSERT_FALSE(bytes.empty());
    auto mb = osf::parse_metablock_xml(bytes.data(), bytes.size());
    ASSERT_TRUE(mb.has_value()) << mb.error().message;
    EXPECT_EQ(mb->file_info.version, 4u);
    EXPECT_GT(mb->channels.size(), 0u);
}

// ---------------------------------------------------------------------
// Symmetry probe: an OSF4 generated file (parsed by parse_metablock_xml)
// and the equivalent OSF5 generated file (parsed by parse_metablock_json)
// produce the same channel list shape. Phase 4's success criterion is
// "symmetric population with the JSON parser" — pin one explicit case.
// ---------------------------------------------------------------------

TEST_F(MetablockXmlExamplesTest, equidistant_osf4_and_osf5_have_matching_channels) {
    auto bytes_4 = read_metablock(examples_dir() / "generated" / "osf4_equidistant.osf");
    auto bytes_5 = read_metablock(examples_dir() / "generated" / "osf5_equidistant.osf");
    ASSERT_FALSE(bytes_4.empty());
    ASSERT_FALSE(bytes_5.empty());

    auto mb_4 = osf::parse_metablock_xml(bytes_4.data(), bytes_4.size());
    auto mb_5 = osf::parse_metablock_json(bytes_5.data(), bytes_5.size());
    ASSERT_TRUE(mb_4.has_value()) << mb_4.error().message;
    ASSERT_TRUE(mb_5.has_value()) << mb_5.error().message;

    ASSERT_EQ(mb_4->channels.size(), mb_5->channels.size());
    for (std::size_t i = 0; i < mb_4->channels.size(); ++i) {
        auto const& c4 = mb_4->channels[i];
        auto const& c5 = mb_5->channels[i];
        EXPECT_EQ(c4.index, c5.index) << "channel " << i << " index";
        EXPECT_EQ(c4.name, c5.name)   << "channel " << i << " name";
        EXPECT_EQ(c4.channel_type, c5.channel_type)
            << "channel " << i << " channel_type";
        EXPECT_EQ(c4.data_type, c5.data_type)
            << "channel " << i << " data_type";
        EXPECT_EQ(c4.size_of_length_value, c5.size_of_length_value)
            << "channel " << i << " sizeoflengthvalue";
        EXPECT_EQ(c4.time_increment_ns.value_or(-1),
                  c5.time_increment_ns.value_or(-1))
            << "channel " << i << " timeincrement";
    }
}
