// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <gtest/gtest.h>

#include <osf/osf.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

// Convenience: parse a string_view via the buffer overload.
osf::Result<osf::MagicHeader> parseBytes(std::string_view bytes) {
    return osf::parseMagicHeader(
        reinterpret_cast<std::uint8_t const*>(bytes.data()),
        bytes.size());
}

// RAII helper for the path-overload tests. Writes the content
// once, removes the file in the destructor.
struct TempFile {
    std::filesystem::path path;

    TempFile(std::string_view filename, std::string_view content) {
        path = std::filesystem::temp_directory_path() / filename;
        std::ofstream out{path, std::ios::binary};
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    TempFile(TempFile const&) = delete;
    TempFile& operator=(TempFile const&) = delete;
};

}  // namespace

// ----- 1..4: positive identifier parsing -----

TEST(MagicHeader, parses_modern_osf4_identifier) {
    auto result = parseBytes("OSF4 928\n<rest>");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->version, osf::OsfVersion::Osf4);
    EXPECT_EQ(result->metablockLen, 928u);
}

TEST(MagicHeader, parses_legacy_ocean_stream_format4_identifier) {
    auto result = parseBytes("OCEAN_STREAM_FORMAT4 26279\n");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->version, osf::OsfVersion::Osf4);
    EXPECT_EQ(result->metablockLen, 26279u);
}

TEST(MagicHeader, parses_legacy_ocean_streaming_format4_identifier) {
    auto result = parseBytes("OCEAN_STREAMING_FORMAT4 12345\n");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->version, osf::OsfVersion::Osf4);
    EXPECT_EQ(result->metablockLen, 12345u);
}

TEST(MagicHeader, parses_osf5_identifier) {
    auto result = parseBytes("OSF5 895\n{\"osf\":...");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->version, osf::OsfVersion::Osf5);
    EXPECT_EQ(result->metablockLen, 895u);
}

// ----- 5..9: negative cases (parse failures) -----

TEST(MagicHeader, rejects_unknown_identifier) {
    auto result = parseBytes("OSF99 100\n");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, osf::Error::Code::UnsupportedVersion);
}

TEST(MagicHeader, rejects_missing_length) {
    auto result = parseBytes("OSF5\n");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, osf::Error::Code::InvalidMagicHeader);
}

TEST(MagicHeader, rejects_non_numeric_length) {
    auto result = parseBytes("OSF5 abc\n");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, osf::Error::Code::InvalidMagicHeader);
}

TEST(MagicHeader, rejects_runaway_input_without_newline) {
    std::string payload(osf::MAX_MAGIC_HEADER_LEN + 10, 'X');
    auto result = parseBytes(payload);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, osf::Error::Code::MagicHeaderTooLong);
}

TEST(MagicHeader, rejects_truncated_input) {
    auto result = parseBytes("OSF5 895");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, osf::Error::Code::InvalidMagicHeader);
}

// ----- 10: CRLF tolerance -----

TEST(MagicHeader, tolerates_crlf_terminator) {
    auto result = parseBytes("OSF5 42\r\n");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->version, osf::OsfVersion::Osf5);
    EXPECT_EQ(result->metablockLen, 42u);
}

// ----- 11: stream invariant -----

TEST(MagicHeader, stream_position_after_newline) {
    std::string input = "OSF5 7\nMETABLOCK_BYTES_FOLLOW";
    std::istringstream stream{input};
    auto result = osf::parseMagicHeader(static_cast<std::istream&>(stream));
    ASSERT_TRUE(result.has_value()) << result.error().message;

    std::ostringstream remainder;
    remainder << stream.rdbuf();
    EXPECT_EQ(remainder.str(), "METABLOCK_BYTES_FOLLOW");
}

// ----- 12: buffer overload matches istream overload -----

TEST(MagicHeader, buffer_overload_matches_istream_overload) {
    std::string input = "OSF5 100\n";

    auto bufferResult = osf::parseMagicHeader(
        reinterpret_cast<std::uint8_t const*>(input.data()), input.size());

    std::istringstream stream{input};
    auto istreamResult = osf::parseMagicHeader(static_cast<std::istream&>(stream));

    ASSERT_TRUE(bufferResult.has_value()) << bufferResult.error().message;
    ASSERT_TRUE(istreamResult.has_value()) << istreamResult.error().message;
    EXPECT_EQ(*bufferResult, *istreamResult);
}

// ----- 13..14: path overload -----

TEST(MagicHeader, path_overload_works) {
    TempFile tmp{"osf_test_path_overload.osf", "OSF5 1234\nrest_of_file"};
    auto result = osf::parseMagicHeader(tmp.path);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->version, osf::OsfVersion::Osf5);
    EXPECT_EQ(result->metablockLen, 1234u);
}

TEST(MagicHeader, path_overload_handles_missing_file) {
    auto missing = std::filesystem::temp_directory_path()
                 / "osf_test_definitely_missing_a3f9.osf";
    std::error_code ec;
    std::filesystem::remove(missing, ec);  // ensure not present
    auto result = osf::parseMagicHeader(missing);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, osf::Error::Code::IoError);
}

// ----- 15: equality semantics on MagicHeader -----

TEST(MagicHeader, magic_header_equality) {
    osf::MagicHeader a{osf::OsfVersion::Osf5, 100};
    osf::MagicHeader b{osf::OsfVersion::Osf5, 100};
    osf::MagicHeader c{osf::OsfVersion::Osf4, 100};
    osf::MagicHeader d{osf::OsfVersion::Osf5, 200};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
}

// ----- 16: lone CR is not a terminator (future-proofing) -----

TEST(MagicHeader, rejects_lone_cr_without_lf) {
    auto result = parseBytes("OSF5 42\r");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, osf::Error::Code::InvalidMagicHeader);
}
