// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// Integration tests for parseMetablockXml against the OSF4 reference
// files under examples/generated/ plus the two field samples
// motorbike.osf and steam_loco.osf. These exercise the parser on real
// OSFGenerator output and on production-device output, not just on
// hand-built XML; that is the cross-validation step against the spec.

#include <gtest/gtest.h>

#include <osf/osf.h>

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

    static std::filesystem::path examplesDir() {
        return std::filesystem::path{OSF_EXAMPLES_DIR};
    }

    static std::vector<std::uint8_t> readMetablock(std::filesystem::path const& path) {
        auto header = osf::parseMagicHeader(path);
        EXPECT_TRUE(header.has_value())
            << "magic header parse failed for " << path << ": "
            << (header ? "" : header.error().message);
        if (!header) return {};

        std::ifstream in{path, std::ios::binary};
        EXPECT_TRUE(in.is_open()) << "open failed: " << path;
        if (!in.is_open()) return {};

        // Skip the magic-header line and then read exactly
        // metablockLen bytes that follow.
        std::string line;
        std::getline(in, line);

        std::vector<std::uint8_t> buf(static_cast<std::size_t>(header->metablockLen));
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
    auto bytes = readMetablock(examplesDir() / "generated" / "osf4_equidistant.osf");
    ASSERT_FALSE(bytes.empty());

    auto mb = osf::parseMetablockXml(bytes.data(), bytes.size());
    ASSERT_TRUE(mb.has_value()) << mb.error().message;

    EXPECT_EQ(mb->fileInfo.version, 4u);
    ASSERT_TRUE(mb->fileInfo.creator.has_value());
    EXPECT_EQ(*mb->fileInfo.creator, "OSFGenerator/1.0");
    // OSFGenerator-style files use the short geolocation spelling.
    EXPECT_TRUE(mb->fileInfo.createdAtLatitude.has_value());
    EXPECT_TRUE(mb->fileInfo.createdAtLongitude.has_value());
    EXPECT_TRUE(mb->fileInfo.createdAtAltitude.has_value());

    ASSERT_FALSE(mb->channels.empty());
    auto const& first = mb->channels[0];
    EXPECT_EQ(first.index, 0);
    EXPECT_EQ(first.channelType, osf::ChannelType::Scalar);
    EXPECT_EQ(first.dataType, osf::DataType::Double);
    EXPECT_TRUE(first.timeIncrementNs.has_value());
    EXPECT_GT(*first.timeIncrementNs, 0);
}

// ---------------------------------------------------------------------
// Loop test: every osf4_*.osf reference file under examples/generated/
// must parse. No removed-in-spec datatypes are expected.
// ---------------------------------------------------------------------

TEST_F(MetablockXmlExamplesTest, all_osf4_generated_files_parse) {
    auto generated = examplesDir() / "generated";
    ASSERT_TRUE(std::filesystem::exists(generated))
        << "examples/generated/ missing";

    int parsed = 0;
    for (auto const& entry : std::filesystem::directory_iterator{generated}) {
        if (entry.path().extension() != ".osf") continue;
        auto filename = entry.path().filename().string();
        if (filename.rfind("osf4_", 0) != 0) continue;

        SCOPED_TRACE("file: " + filename);

        auto bytes = readMetablock(entry.path());
        ASSERT_FALSE(bytes.empty());

        auto mb = osf::parseMetablockXml(bytes.data(), bytes.size());
        ASSERT_TRUE(mb.has_value()) << mb.error().message;
        EXPECT_EQ(mb->fileInfo.version, 4u);
        EXPECT_GT(mb->channels.size(), 0u);

        for (auto const& ch : mb->channels) {
            EXPECT_FALSE(ch.name.empty());
            EXPECT_TRUE(ch.sizeOfLengthValue == 2 ||
                        ch.sizeOfLengthValue == 4)
                << "channel " << ch.index << " (" << ch.name << ") has "
                << "sizeoflengthvalue=" << int{ch.sizeOfLengthValue};
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
    auto bytes = readMetablock(examplesDir() / "generated" / "osf4_gpslocation.osf");
    ASSERT_FALSE(bytes.empty());

    auto mb = osf::parseMetablockXml(bytes.data(), bytes.size());
    ASSERT_TRUE(mb.has_value()) << mb.error().message;

    bool sawGps = false;
    for (auto const& ch : mb->channels) {
        if (ch.dataType == osf::DataType::GpsLocation) {
            sawGps = true;
            EXPECT_EQ(ch.dataTypeRaw, "gpslocation");
        }
    }
    EXPECT_TRUE(sawGps)
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
    auto path = examplesDir() / "motorbike.osf";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "motorbike.osf not present";
    }
    auto bytes = readMetablock(path);
    ASSERT_FALSE(bytes.empty());
    auto mb = osf::parseMetablockXml(bytes.data(), bytes.size());
    ASSERT_TRUE(mb.has_value()) << mb.error().message;
    EXPECT_EQ(mb->fileInfo.version, 4u);
    EXPECT_GT(mb->channels.size(), 0u);
}

TEST_F(MetablockXmlExamplesTest, steam_loco_osf_metablock_parses) {
    auto path = examplesDir() / "steam_loco.osf";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "steam_loco.osf not present";
    }
    auto bytes = readMetablock(path);
    ASSERT_FALSE(bytes.empty());
    auto mb = osf::parseMetablockXml(bytes.data(), bytes.size());
    ASSERT_TRUE(mb.has_value()) << mb.error().message;
    EXPECT_EQ(mb->fileInfo.version, 4u);
    EXPECT_GT(mb->channels.size(), 0u);
}

// ---------------------------------------------------------------------
// Symmetry probe: an OSF4 generated file (parsed by parseMetablockXml)
// and the equivalent OSF5 generated file (parsed by parseMetablockJson)
// produce the same channel list shape: symmetric population with the
// JSON parser — pin one explicit case.
// ---------------------------------------------------------------------

TEST_F(MetablockXmlExamplesTest, equidistant_osf4_and_osf5_have_matching_channels) {
    auto bytes4 = readMetablock(examplesDir() / "generated" / "osf4_equidistant.osf");
    auto bytes5 = readMetablock(examplesDir() / "generated" / "osf5_equidistant.osf");
    ASSERT_FALSE(bytes4.empty());
    ASSERT_FALSE(bytes5.empty());

    auto mb4 = osf::parseMetablockXml(bytes4.data(), bytes4.size());
    auto mb5 = osf::parseMetablockJson(bytes5.data(), bytes5.size());
    ASSERT_TRUE(mb4.has_value()) << mb4.error().message;
    ASSERT_TRUE(mb5.has_value()) << mb5.error().message;

    ASSERT_EQ(mb4->channels.size(), mb5->channels.size());
    for (std::size_t i = 0; i < mb4->channels.size(); ++i) {
        auto const& c4 = mb4->channels[i];
        auto const& c5 = mb5->channels[i];
        EXPECT_EQ(c4.index, c5.index) << "channel " << i << " index";
        EXPECT_EQ(c4.name, c5.name)   << "channel " << i << " name";
        EXPECT_EQ(c4.channelType, c5.channelType)
            << "channel " << i << " channelType";
        EXPECT_EQ(c4.dataType, c5.dataType)
            << "channel " << i << " dataType";
        EXPECT_EQ(c4.sizeOfLengthValue, c5.sizeOfLengthValue)
            << "channel " << i << " sizeoflengthvalue";
        EXPECT_EQ(c4.timeIncrementNs.value_or(-1),
                  c5.timeIncrementNs.value_or(-1))
            << "channel " << i << " timeincrement";
    }
}
