// SPDX-License-Identifier: MIT
//
// Integration tests for parse_metablock_json against the OSF5 reference
// files under examples/generated/. These exercise the parser on real
// fixtures produced by OSFGenerator rather than hand-built JSON; that
// is the cross-validation step against the spec.

#include <gtest/gtest.h>

#include <osf/osf.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class MetablockExamplesTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        auto dir = std::filesystem::path{OSF_EXAMPLES_DIR};
        ASSERT_TRUE(std::filesystem::exists(dir))
            << "OSF_EXAMPLES_DIR does not exist: " << dir;
    }

    static std::filesystem::path examples_dir() {
        return std::filesystem::path{OSF_EXAMPLES_DIR};
    }

    // Read magic header from `path`, then read exactly metablock_len
    // bytes that follow. Returns the metablock bytes; ASSERTs on
    // failure (caller wraps in a fixture method).
    static std::vector<std::uint8_t> read_metablock(std::filesystem::path const& path) {
        auto header = osf::parse_magic_header(path);
        EXPECT_TRUE(header.has_value())
            << "magic header parse failed for " << path << ": "
            << (header ? "" : header.error().message);
        if (!header) return {};

        std::ifstream in{path, std::ios::binary};
        EXPECT_TRUE(in.is_open()) << "open failed: " << path;
        if (!in.is_open()) return {};

        // Skip the magic-header line. read_first_line is byte-by-byte,
        // so re-implement that skip here against the file directly
        // (the public API doesn't expose the post-header file offset).
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
// Snapshot test on one well-known file. Locks down a few values so a
// regression in field parsing fails loudly rather than being lost in
// the generic loop below.
// ---------------------------------------------------------------------

TEST_F(MetablockExamplesTest, osf5_equidistant_snapshot) {
    auto bytes = read_metablock(examples_dir() / "generated" / "osf5_equidistant.osf");
    ASSERT_FALSE(bytes.empty());

    auto mb = osf::parse_metablock_json(bytes.data(), bytes.size());
    ASSERT_TRUE(mb.has_value()) << mb.error().message;

    EXPECT_EQ(mb->file_info.version, 5u);
    ASSERT_TRUE(mb->file_info.creator.has_value());
    EXPECT_EQ(*mb->file_info.creator, "OSFGenerator/1.0");
    EXPECT_TRUE(mb->file_info.created_at_latitude.has_value());

    ASSERT_FALSE(mb->channels.empty());
    auto const& first = mb->channels[0];
    EXPECT_EQ(first.index, 0);
    EXPECT_EQ(first.channel_type, osf::ChannelType::Scalar);
    EXPECT_EQ(first.data_type, osf::DataType::Double);
    EXPECT_TRUE(first.time_increment_ns.has_value());
    EXPECT_GT(*first.time_increment_ns, 0);
}

// ---------------------------------------------------------------------
// Loop test: every osf5_*.osf reference file under examples/generated/
// must parse. No removed-in-spec datatypes are expected.
// ---------------------------------------------------------------------

TEST_F(MetablockExamplesTest, all_osf5_generated_files_parse) {
    auto generated = examples_dir() / "generated";
    ASSERT_TRUE(std::filesystem::exists(generated))
        << "examples/generated/ missing";

    int parsed = 0;
    for (auto const& entry : std::filesystem::directory_iterator{generated}) {
        if (entry.path().extension() != ".osf") continue;
        auto filename = entry.path().filename().string();
        if (filename.rfind("osf5_", 0) != 0) continue;

        SCOPED_TRACE("file: " + filename);

        auto bytes = read_metablock(entry.path());
        ASSERT_FALSE(bytes.empty());

        auto mb = osf::parse_metablock_json(bytes.data(), bytes.size());
        ASSERT_TRUE(mb.has_value()) << mb.error().message;
        EXPECT_EQ(mb->file_info.version, 5u);
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

    EXPECT_GT(parsed, 0) << "no osf5_*.osf files found under " << generated;
}

// ---------------------------------------------------------------------
// Coverage probe: at least one reference file is expected to declare a
// gpslocation channel. This pins the new spec-rev datatype as actually
// exercised end-to-end, not just unit-tested.
// ---------------------------------------------------------------------

TEST_F(MetablockExamplesTest, osf5_gpslocation_file_declares_gpslocation_channel) {
    auto bytes = read_metablock(examples_dir() / "generated" / "osf5_gpslocation.osf");
    ASSERT_FALSE(bytes.empty());

    auto mb = osf::parse_metablock_json(bytes.data(), bytes.size());
    ASSERT_TRUE(mb.has_value()) << mb.error().message;

    bool saw_gps = false;
    for (auto const& ch : mb->channels) {
        if (ch.data_type == osf::DataType::GpsLocation) {
            saw_gps = true;
            EXPECT_EQ(ch.data_type_raw, "gpslocation");
        }
    }
    EXPECT_TRUE(saw_gps)
        << "osf5_gpslocation.osf produced no GpsLocation channel";
}
