// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file compression.hpp
 * @brief Transparent OSFZ (gzip / zlib) decompression on the read path
 *        (Phase 8).
 *
 * OSFZ files are gzip- or zlib-compressed OSF files with no dedicated
 * magic header — detection is by the leading two bytes of the stream:
 *
 *   | Format | Magic bytes                       | Decoder |
 *   |--------|-----------------------------------|---------|
 *   | gzip   | `0x1F 0x8B`                       | inflate |
 *   | zlib   | `0x78 0x01 / 0x5E / 0x9C / 0xDA`  | inflate |
 *   | none   | anything else (real OSF: `0x4F`)  | direct  |
 *
 * Deployed optiMEAS devices emit gzip-wrapped OSF (`weather_station.osfz`);
 * older tooling used raw zlib. DataManager wraps its input in a
 * DecompressingIStream so both forms load transparently. The low-level
 * `parse_magic_header` deliberately does NOT decompress — OSFZ
 * transparency lives in this read layer.
 *
 * Mirrors the Rust `compression` module
 * (`implementations/rust/osf-core/src/compression.rs`): `detect_and_wrap`
 * + `MaybeCompressed<R>`. The C++ analogue is a decompressing
 * `std::istream` so the existing `BlockReader(std::istream&, …)` reads
 * through it unchanged.
 */

#pragma once

#include "osf/stats.hpp"   // osf::CompressionFormat

#include <istream>
#include <memory>

namespace osf {

/**
 * @brief Classify a stream by its leading two bytes without consuming
 *        them.
 *
 * Peeks via read + seek-back, so the stream must be seekable (the OSF
 * read path uses `std::ifstream` / `std::istringstream`, both seekable).
 * Returns `CompressionFormat::None` for gzip/zlib-mismatched bytes, for
 * streams shorter than two bytes, and for plain OSF (which starts with
 * `OSF` = `0x4F`, colliding with neither magic).
 */
[[nodiscard]] CompressionFormat detect_compression(std::istream& source);

/**
 * @brief A transparently-decompressing input stream over a source
 *        istream.
 *
 * For `CompressionFormat::None` the bytes pass through verbatim; for
 * zlib / gzip they are inflated on demand (constant-memory streaming
 * inflate with automatic header detection — no whole-file buffering).
 * The source stream must outlive this object. Reads consume the source
 * as inflation proceeds.
 *
 * Best-effort on truncation, matching the rest of the read stack: a
 * compressed stream that ends mid-member yields EOF rather than throwing,
 * so a partially-written / truncated OSFZ file still surfaces the blocks
 * it could decode.
 *
 * Not thread-safe.
 */
class DecompressingIStream : public std::istream {
public:
    explicit DecompressingIStream(std::istream& source);
    ~DecompressingIStream() override;

    DecompressingIStream(DecompressingIStream const&) = delete;
    DecompressingIStream& operator=(DecompressingIStream const&) = delete;

    /// The format detected on the source stream.
    [[nodiscard]] CompressionFormat format() const noexcept;

    /// True if the source was detected as zlib- or gzip-compressed.
    [[nodiscard]] bool is_compressed() const noexcept;

private:
    class Streambuf;                       // defined in src/compression.cpp
    std::unique_ptr<Streambuf> buf_;
};

}  // namespace osf
