// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Integration + cross-validation tests for the OSF5 "crc" integrity
// profile (CRC32C). Covers:
//   * round-trip through both writers (StreamingWriter, BlockWriter) with
//     integrity on, and equivalence to the plain writers' channel data;
//   * reading the four cross-implementation reference files (two written by
//     the Rust writer, two by the Delphi writer) with zero CRC failures —
//     this is the byte-level cross-validation of the frame/metablock CRC;
//   * the negative suite mirroring the Rust reader tests: metablock byte
//     flip, numeric data-block byte flip, string data-block byte flip,
//     unknown header token, OSF4 + token, and a control-byte-9 signature
//     block (skipped, counted, profile-independent);
//   * transparent decompression of a gzip-wrapped CRC file (the CRC is
//     verified on the decompressed stream).

#include <gtest/gtest.h>

#include <osf/blockwriter.h>
#include <osf/manager.h>
#include <osf/streamingwriter.h>

#include "roundtriphelper.h"

#include <zlib.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path genDir() {
    return std::filesystem::path{OSF_EXAMPLES_DIR} / "generated";
}
std::filesystem::path crcDir() { return genDir() / "integrity"; }

std::string readFileBytes(std::filesystem::path const& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

osf::Result<osf::DataManager> loadBytes(std::string const& bytes) {
    std::istringstream in(bytes, std::ios::binary);
    return osf::DataManager::loadFromStream(in);
}

// Header line = "OSF5 <metablockLen> [crc32c:XXXXXXXX]\n". Returns the byte
// length of the header line including the terminating newline, and the
// metablock body length that follows it.
struct HeaderGeom {
    std::size_t headerLen = 0;    // bytes up to and including '\n'
    std::size_t metablockLen = 0; // metablock body length
};
HeaderGeom headerGeom(std::string const& s) {
    std::size_t const nl = s.find('\n');
    std::size_t const sp1 = s.find(' ');
    std::size_t sp2 = s.find(' ', sp1 + 1);
    if (sp2 == std::string::npos || sp2 > nl) sp2 = nl;
    std::size_t const len =
        static_cast<std::size_t>(std::stoul(s.substr(sp1 + 1, sp2 - sp1 - 1)));
    return {nl + 1, len};
}

void putU16(std::string& s, std::uint16_t v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void putU32(std::string& s, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

// Gzip-compress (windowBits 15 + 16) — same primitive the compression tests
// use, kept local to avoid a cross-test dependency.
std::string gzip(std::string const& input) {
    z_stream zs{};
    EXPECT_EQ(deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                           Z_DEFAULT_STRATEGY),
              Z_OK);
    std::vector<unsigned char> out(static_cast<std::size_t>(
        deflateBound(&zs, static_cast<uLong>(input.size()))));
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    zs.avail_in = static_cast<uInt>(input.size());
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(out.size());
    EXPECT_EQ(deflate(&zs, Z_FINISH), Z_STREAM_END);
    out.resize(out.size() - zs.avail_out);
    deflateEnd(&zs);
    return std::string(reinterpret_cast<char const*>(out.data()), out.size());
}

// Author a fixed multi-channel dataset onto a fresh StreamingWriter at
// `path`, optionally with the crc profile. Kept identical across the two
// integrity settings so the reloaded managers can be compared byte-for-byte.
void authorStreaming(std::filesystem::path const& path,
                     osf::IntegrityProfile integrity) {
    osf::StreamingWriter w(path);
    if (integrity != osf::IntegrityProfile::None) w.setIntegrity(integrity);

    osf::ChannelDef d0;
    d0.name = "sensor/temp";
    d0.dataType = osf::DataType::Double;
    d0.channelType = osf::ChannelType::Scalar;
    auto c0 = w.addChannel(d0);
    ASSERT_TRUE(c0.has_value()) << c0.error().message;

    osf::ChannelDef d1;
    d1.name = "sensor/count";
    d1.dataType = osf::DataType::Int64;
    d1.channelType = osf::ChannelType::Scalar;
    auto c1 = w.addChannel(d1);
    ASSERT_TRUE(c1.has_value()) << c1.error().message;

    ASSERT_TRUE(w.start().has_value());
    for (int i = 0; i < 40; ++i) {
        std::int64_t const ts = 1'000'000'000LL + i * 1'000'000LL;
        ASSERT_TRUE(w.writeTimestampedSample<double>(*c0, ts, 20.0 + i * 0.5)
                        .has_value());
        ASSERT_TRUE(
            w.writeTimestampedSample<std::int64_t>(*c1, ts, i).has_value());
    }
    ASSERT_TRUE(w.close().has_value());
}

}  // namespace

// ── Writer round-trips ────────────────────────────────────────────────

TEST(CppIntegrityRoundtrip, block_writer_crc_matches_plain_and_source) {
    auto src = osf::DataManager::loadFromFile(genDir() / "osf5_mixed.osf");
    ASSERT_TRUE(src.has_value()) << src.error().message;

    auto bw = osf::BlockWriter::fromManager(*src);
    ASSERT_TRUE(bw.has_value()) << bw.error().message;
    bw->setIntegrity(osf::IntegrityProfile::Crc32c);
    std::ostringstream oss(std::ios::binary);
    ASSERT_TRUE(bw->writeTo(oss).has_value());
    std::string const bytes = oss.str();

    // The header carries the crc32c token.
    EXPECT_NE(bytes.find(" crc32c:"), std::string::npos);

    auto rt = loadBytes(bytes);
    ASSERT_TRUE(rt.has_value()) << rt.error().message;
    EXPECT_EQ(rt->stats.integrity, osf::IntegrityProfile::Crc32c);
    EXPECT_EQ(rt->stats.blocksCrcFailed, 0u);
    EXPECT_EQ(rt->stats.blocksSignatureSkipped, 0u);
    EXPECT_EQ(rt->stats.verificationStatus(), "crc_valid");
    EXPECT_TRUE(osftest::roundtripManagersEqual(*src, *rt));
}

TEST(CppIntegrityRoundtrip, streaming_writer_crc_matches_plain) {
    auto const dir = std::filesystem::temp_directory_path();
    auto const plainPath = dir / "osf_crc_stream_plain.osf";
    auto const crcPath = dir / "osf_crc_stream_crc.osf";

    authorStreaming(plainPath, osf::IntegrityProfile::None);
    authorStreaming(crcPath, osf::IntegrityProfile::Crc32c);

    auto plain = osf::DataManager::loadFromFile(plainPath);
    auto crc = osf::DataManager::loadFromFile(crcPath);
    ASSERT_TRUE(plain.has_value()) << plain.error().message;
    ASSERT_TRUE(crc.has_value()) << crc.error().message;

    EXPECT_EQ(plain->stats.integrity, osf::IntegrityProfile::None);
    EXPECT_EQ(crc->stats.integrity, osf::IntegrityProfile::Crc32c);
    EXPECT_EQ(crc->stats.blocksCrcFailed, 0u);
    EXPECT_EQ(crc->stats.verificationStatus(), "crc_valid");
    // Frame CRC is purely additive trailing bytes; the reader strips them,
    // so the reconstructed channel data must be identical.
    EXPECT_TRUE(osftest::roundtripManagersEqual(*plain, *crc));

    std::filesystem::remove(plainPath);
    std::filesystem::remove(crcPath);
}

// ── Cross-implementation reference files ──────────────────────────────
//
// The four integrity reference files (osf5_crc_*.osf + *_crc_delphi.osf) are
// validated — integrity profile, zero CRC failures, channel count, per-channel
// contents — by the manifest-driven test_conformance_manifest, which reads the
// shared examples/reference_manifest.json. Keeping the file list only in the
// manifest gives it a single source of truth shared across implementations.
// The tests below keep the integrity-specific behavioural cases (round-trip,
// byte flips, signature-block skip, gzip) that a manifest entry cannot express.

// ── Negative suite ────────────────────────────────────────────────────

TEST(CppIntegrityNegative, metablock_byte_flip_rejects_load) {
    std::string bytes = readFileBytes(crcDir() / "osf5_crc_equidistant.osf");
    auto const geom = headerGeom(bytes);
    // Flip a byte well inside the metablock body.
    std::size_t const off = geom.headerLen + 20;
    ASSERT_LT(off, geom.headerLen + geom.metablockLen);
    bytes[off] = static_cast<char>(bytes[off] ^ 0xFF);

    auto mgr = loadBytes(bytes);
    ASSERT_FALSE(mgr.has_value());
    EXPECT_EQ(mgr.error().code, osf::Error::Code::MetablockCrcMismatch);
}

TEST(CppIntegrityNegative, numeric_data_block_byte_flip_counts_crc_failure) {
    std::string bytes = readFileBytes(crcDir() / "osf5_crc_equidistant.osf");
    // Flip a byte inside the trailing CRC of the final data block.
    std::size_t const off = bytes.size() - 2;
    bytes[off] = static_cast<char>(bytes[off] ^ 0xFF);

    auto mgr = loadBytes(bytes);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;  // load survives
    EXPECT_GE(mgr->stats.blocksCrcFailed, 1u);
    EXPECT_EQ(mgr->stats.verificationStatus(), "invalid");
}

TEST(CppIntegrityNegative, string_data_block_byte_flip_counts_crc_failure) {
    std::string bytes = readFileBytes(crcDir() / "osf5_crc_variable.osf");
    std::size_t const off = bytes.size() - 2;
    bytes[off] = static_cast<char>(bytes[off] ^ 0xFF);

    auto mgr = loadBytes(bytes);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    EXPECT_GE(mgr->stats.blocksCrcFailed, 1u);
    EXPECT_EQ(mgr->stats.verificationStatus(), "invalid");
}

TEST(CppIntegrityNegative, unknown_header_token_is_rejected) {
    std::string bytes = readFileBytes(genDir() / "osf5_mixed.osf");
    std::size_t const nl = bytes.find('\n');
    ASSERT_NE(nl, std::string::npos);
    bytes.insert(nl, " zz:01");  // unknown must-understand token

    auto mgr = loadBytes(bytes);
    ASSERT_FALSE(mgr.has_value());
    EXPECT_EQ(mgr.error().code, osf::Error::Code::UnknownHeaderToken);
}

TEST(CppIntegrityNegative, token_on_osf4_identifier_is_rejected) {
    // Synthesise an OSF4 magic line carrying a crc32c token. Tokens are an
    // OSF5-only grammar construct, so the line is rejected at the header level
    // as InvalidMagicHeader (matching the Rust reference — a token after an
    // OSF4 identifier is not an "unknown token", the grammar forbids tokens
    // entirely there).
    std::string const line = "OSF4 100 crc32c:DEADBEEF\n";
    auto mgr = loadBytes(line + std::string(100, ' '));
    ASSERT_FALSE(mgr.has_value());
    EXPECT_EQ(mgr.error().code, osf::Error::Code::InvalidMagicHeader);
}

TEST(CppIntegrityNegative, signature_block_is_skipped_and_counted) {
    // Inject a control-byte-9 signature block on the reserved 0xFFFE channel
    // into a file whose header declares an integrity profile — it must be
    // skipped, counted, and leave the real channel data intact. Signature-
    // block interception is gated on an active profile (matching the Rust
    // reference: a profile-less reader treats 0xFFFE as an unknown channel).
    std::string base = readFileBytes(crcDir() / "osf5_crc_equidistant.osf");
    auto const geom = headerGeom(base);
    std::size_t const blockStart = geom.headerLen + geom.metablockLen;

    // inner = control byte 9 + opaque signature bytes. Signature blocks carry
    // a frame CRC like every other block, so the u32 length counts the 4-byte
    // CRC and four trailing bytes follow. The reader drains the block by its
    // length without verifying the signature, so the CRC bytes are opaque.
    std::string inner;
    inner.push_back(static_cast<char>(0x09));
    inner.append(20, static_cast<char>(0xAA));

    std::string sig;
    putU16(sig, 0xFFFE);
    putU32(sig, static_cast<std::uint32_t>(inner.size() + 4));
    sig += inner;
    putU32(sig, 0x00000000u);  // opaque frame-CRC placeholder

    std::string injected = base;
    injected.insert(blockStart, sig);

    auto baseline = loadBytes(base);
    auto mgr = loadBytes(injected);
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    EXPECT_EQ(mgr->stats.blocksSignatureSkipped, 1u);
    EXPECT_EQ(mgr->stats.blocksCrcFailed, 0u);
    // Channel data is unchanged by the skipped signature block.
    EXPECT_EQ(mgr->channels().size(), baseline->channels().size());
    EXPECT_TRUE(osftest::roundtripManagersEqual(*baseline, *mgr));
}

// ── Transparent decompression of a CRC file ───────────────────────────

TEST(CppIntegrityCompression, gzip_wrapped_crc_file_reads_and_verifies) {
    std::string const raw = readFileBytes(crcDir() / "osf5_crc_equidistant.osf");
    std::string const gz = gzip(raw);

    auto mgr = loadBytes(gz);
    ASSERT_TRUE(mgr.has_value()) << mgr.error().message;
    EXPECT_TRUE(mgr->stats.compressed);
    EXPECT_EQ(mgr->stats.integrity, osf::IntegrityProfile::Crc32c);
    EXPECT_EQ(mgr->stats.blocksCrcFailed, 0u);
    EXPECT_EQ(mgr->stats.verificationStatus(), "crc_valid");

    // Identical to reading the plain (uncompressed) CRC file.
    auto plain = loadBytes(raw);
    ASSERT_TRUE(plain.has_value()) << plain.error().message;
    EXPECT_TRUE(osftest::roundtripManagersEqual(*plain, *mgr));
}
