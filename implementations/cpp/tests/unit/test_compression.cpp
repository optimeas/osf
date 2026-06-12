// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/compression.h"

#include <zlib.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Deflate with an explicit windowBits: 15 → zlib (RFC 1950) header,
// 15 + 16 → gzip (RFC 1952) header. Mirrors the flate2 encoders used by
// the Rust compression tests.
std::string deflateWith(std::string const& input, int windowBits) {
    z_stream zs{};
    EXPECT_EQ(deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                           windowBits, 8, Z_DEFAULT_STRATEGY),
              Z_OK);
    std::vector<unsigned char> out(
        static_cast<std::size_t>(deflateBound(
            &zs, static_cast<uLong>(input.size()))));
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

std::string zlibEncode(std::string const& s) { return deflateWith(s, 15); }
std::string gzipEncode(std::string const& s) { return deflateWith(s, 15 + 16); }

std::string readAll(std::istream& in) {
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

const std::string kPlain = "OSF5 42\n{\"osf\":...rest of file...}";

}  // namespace

// ── detectCompression (non-consuming) ───────────────────────────────

TEST(DetectCompression, plain_is_none_and_position_preserved) {
    std::istringstream src(kPlain, std::ios::binary);
    EXPECT_EQ(osf::detectCompression(src), osf::CompressionFormat::None);
    // Position preserved: a subsequent read still sees the first byte.
    EXPECT_EQ(static_cast<char>(src.get()), 'O');
}

TEST(DetectCompression, zlib_and_gzip_are_classified_without_consuming) {
    std::istringstream zsrc(zlibEncode(kPlain), std::ios::binary);
    EXPECT_EQ(osf::detectCompression(zsrc), osf::CompressionFormat::Zlib);
    EXPECT_EQ(static_cast<std::uint8_t>(zsrc.get()), 0x78);

    std::istringstream gsrc(gzipEncode(kPlain), std::ios::binary);
    EXPECT_EQ(osf::detectCompression(gsrc), osf::CompressionFormat::Gzip);
    EXPECT_EQ(static_cast<std::uint8_t>(gsrc.get()), 0x1F);
}

// ── DecompressingIStream round-trips ─────────────────────────────────

TEST(DecompressingIStream, plain_passes_through) {
    std::istringstream src(kPlain, std::ios::binary);
    osf::DecompressingIStream dz(src);
    EXPECT_FALSE(dz.isCompressed());
    EXPECT_EQ(dz.format(), osf::CompressionFormat::None);
    EXPECT_EQ(readAll(dz), kPlain);
}

TEST(DecompressingIStream, zlib_is_decompressed) {
    std::string const compressed = zlibEncode(kPlain);
    ASSERT_EQ(static_cast<std::uint8_t>(compressed[0]), 0x78);
    std::istringstream src(compressed, std::ios::binary);
    osf::DecompressingIStream dz(src);
    EXPECT_TRUE(dz.isCompressed());
    EXPECT_EQ(dz.format(), osf::CompressionFormat::Zlib);
    EXPECT_EQ(readAll(dz), kPlain);
}

TEST(DecompressingIStream, gzip_is_decompressed) {
    std::string const compressed = gzipEncode(kPlain);
    ASSERT_EQ(static_cast<std::uint8_t>(compressed[0]), 0x1F);
    ASSERT_EQ(static_cast<std::uint8_t>(compressed[1]), 0x8B);
    std::istringstream src(compressed, std::ios::binary);
    osf::DecompressingIStream dz(src);
    EXPECT_TRUE(dz.isCompressed());
    EXPECT_EQ(dz.format(), osf::CompressionFormat::Gzip);
    EXPECT_EQ(readAll(dz), kPlain);
}

TEST(DecompressingIStream, byte_0x78_with_invalid_second_byte_is_plain) {
    std::string const bytes = {0x78, '\xFF', 0x00, 0x01};
    std::istringstream src(bytes, std::ios::binary);
    osf::DecompressingIStream dz(src);
    EXPECT_EQ(dz.format(), osf::CompressionFormat::None);
    EXPECT_EQ(readAll(dz), bytes);
}

TEST(DecompressingIStream, single_byte_stream_is_plain) {
    std::string const bytes = {0x78};
    std::istringstream src(bytes, std::ios::binary);
    osf::DecompressingIStream dz(src);
    EXPECT_EQ(dz.format(), osf::CompressionFormat::None);
    EXPECT_EQ(readAll(dz), bytes);
}

TEST(DecompressingIStream, empty_stream_is_plain_and_yields_nothing) {
    std::istringstream src(std::string{}, std::ios::binary);
    osf::DecompressingIStream dz(src);
    EXPECT_EQ(dz.format(), osf::CompressionFormat::None);
    EXPECT_TRUE(readAll(dz).empty());
}

TEST(DecompressingIStream, osf4_legacy_identifier_is_not_misclassified) {
    // OCEAN_STREAM_FORMAT4 starts with 0x4F ('O') — collides with neither
    // zlib (0x78) nor gzip (0x1F).
    std::string const bytes = "OCEAN_STREAM_FORMAT4 100\n<?xml version...";
    std::istringstream src(bytes, std::ios::binary);
    osf::DecompressingIStream dz(src);
    EXPECT_EQ(dz.format(), osf::CompressionFormat::None);
    EXPECT_EQ(readAll(dz), bytes);
}

TEST(DecompressingIStream, large_payload_round_trips_across_chunks) {
    // 256 KiB decompressed exceeds the 64 KiB inflate output buffer, so
    // underflow() must refill several times.
    std::string big;
    big.reserve(256u * 1024u);
    for (std::size_t i = 0; i < 256u * 1024u; ++i) {
        big.push_back(static_cast<char>('A' + (i * 7u + i / 64u) % 26u));
    }
    for (auto const& enc : {gzipEncode(big), zlibEncode(big)}) {
        std::istringstream src(enc, std::ios::binary);
        osf::DecompressingIStream dz(src);
        EXPECT_TRUE(dz.isCompressed());
        EXPECT_EQ(readAll(dz), big);
    }
}
