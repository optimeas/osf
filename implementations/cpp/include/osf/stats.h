// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// \file stats.h
/// Reader-side telemetry.
///
/// `ReaderStats` is populated by `BlockReader` during iteration. The
/// fields are deliberately concrete (counts and sizes, not opaque
/// metric handles) so any consuming application can format the
/// values without having to introspect.
///
/// Skip reasons are tracked separately rather than under a single
/// `blocksSkipped` counter because each carries different operational
/// meaning:
///
/// - **Unsupported channels** (forward-compat skips) usually mean the
///   file uses a future-spec datatype this build does not yet handle.
/// - **Deprecated block types** (`bcTrustedTimestamp`,
///   `bcStatusEvent`, `bcMessageEvent`) appear in older field files
///   and tell you the file predates spec rev 2026-05-04.
/// - **Reserved block types** (`bcReserved`, `bcTimebaseRealign`,
///   anything with bits 0–6 ≥ 9) are either spec-internal or
///   genuinely unknown.
/// - **Zero-length blocks** (a length field that reads `0`) are always
///   a non-conforming writer artefact (OSF-UP3) — a conforming block
///   always carries at least its control byte. Kept separate from
///   `blocksSkippedReservedType` so this diagnosable writer bug is
///   never silently folded into a legitimate forward-compat skip.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <osf/integrity.h>

namespace osf {

/// Compression format detected on the input stream.
enum class CompressionFormat {
    /// No compression — stream was a regular OSF file.
    None,
    /// zlib stream (RFC 1950).
    Zlib,
    /// gzip stream (RFC 1952). Optimeas devices' current OSFZ wire
    /// format.
    Gzip,
};

/// Per-channel reader telemetry.
struct ChannelStats {
    /// Channel name from the metablock; copied here so per-channel
    /// stats can be rendered without a back-pointer to the metablock.
    std::string name;
    /// Blocks the reader returned as typed variants for this channel.
    std::uint64_t blocksRead = 0;
    /// Blocks the reader skipped on this channel (any reason).
    std::uint64_t blocksSkipped = 0;
    /// Sum of sample counts across all blocks of this channel
    /// (`string` / `binary` count one sample per block).
    std::uint64_t samplesTotal = 0;
    /// Sum of length-field values across all blocks of this channel.
    /// Useful as a rough payload-size proxy.
    std::uint64_t bytesPayload = 0;
    /// Number of `bcStartData` blocks observed (= number of distinct
    /// equidistant segments).
    std::uint32_t segments = 0;
    /// Earliest and latest absolute timestamp the reader observed for
    /// this channel. `std::nullopt` if the channel only produced
    /// blocks without absolute timestamps.
    std::optional<std::pair<std::int64_t, std::int64_t>> timeRangeNs;

    /// Extend the recorded time range to include `ts`. Used by the
    /// reader on each `bcStartData` (with the reconstructed last
    /// sample timestamp) and on every per-sample timestamp emitted by
    /// `bcAbsTimeStampData`.
    void observeTimestamp(std::int64_t ts) noexcept;
};

/// Aggregated counts and timings produced by reading an OSF file.
struct ReaderStats {
    /// Size of the source file in bytes, when known. `std::nullopt`
    /// for streaming sources.
    std::optional<std::uint64_t> fileSizeBytes;
    /// Bytes consumed by the magic-header line (populated by a
    /// convenience wrapper; the `BlockReader` does not see the header
    /// itself).
    std::uint64_t headerSizeBytes = 0;
    /// Bytes consumed by the metablock body (matches the
    /// `metablockLen` field of the magic header).
    std::uint64_t metablockSizeBytes = 0;
    /// Bytes consumed by the block-stream section. Includes channel
    /// indices, length prefixes, control bytes, and payloads;
    /// excludes the magic-header and metablock bytes.
    std::uint64_t dataSectionSizeBytes = 0;
    /// Wall-clock time elapsed while the `BlockReader` was iterating.
    /// Refreshed on each `BlockReader::stats()` call.
    std::chrono::nanoseconds elapsed{0};

    /// Number of channels declared in the metablock.
    std::size_t channelsTotal = 0;
    /// Channels that produced at least one block during iteration.
    /// Recomputed by `BlockReader::stats()` on demand.
    std::size_t channelsWithData = 0;
    /// Channels declared with `DataType::Unsupported` or
    /// `ChannelType::Unsupported`.
    std::size_t channelsUnsupported = 0;

    /// Total number of blocks observed (`blocksRead + blocks_skipped_*`).
    /// Recomputed by `BlockReader::stats()` on demand.
    std::uint64_t blocksTotal = 0;
    /// Blocks the reader produced as typed `BlockKind` variants
    /// (everything except `Skipped`).
    std::uint64_t blocksRead = 0;
    /// Blocks skipped because the channel's data or channel type was
    /// `Unsupported`.
    std::uint64_t blocksSkippedUnsupported = 0;
    /// Blocks skipped because the control byte identified a
    /// deprecated block type.
    std::uint64_t blocksSkippedDeprecatedType = 0;
    /// Blocks skipped because the control byte identified a reserved
    /// block type.
    std::uint64_t blocksSkippedReservedType = 0;
    /// Blocks skipped because their length field read `0` — a non-conforming
    /// writer artefact (OSF-UP3). A conforming block always carries at least
    /// its control byte.
    std::uint64_t blocksSkippedZeroLength = 0;
    /// Number of blocks the reader could not finish before the stream
    /// ended. Capped at 1 by construction.
    std::uint64_t blocksTruncated = 0;
    /// Whether the optional `0xFFFF` info-data block was encountered.
    bool trailerSeen = false;

    /// Whether the source stream was OSFZ-compressed and went through
    /// transparent decompression on read. `false` for plain OSF.
    bool compressed = false;
    /// Detected compression format on the source stream.
    CompressionFormat compressionFormat = CompressionFormat::None;

    /// Integrity level declared by the file's magic-header token
    /// (`None` when the file carries no integrity profile).
    IntegrityProfile integrity = IntegrityProfile::None;
    /// Blocks dropped because their frame CRC (level `crc`) did not match.
    std::uint64_t blocksCrcFailed = 0;
    /// Integrity signature blocks (channel `0xFFFE`, control byte 9) skipped
    /// because this build reads level `crc` but does not verify signatures.
    std::uint64_t blocksSignatureSkipped = 0;

    /// Per-channel detail keyed by channel index. The reader seeds
    /// every metablock channel with a zero-filled entry on
    /// construction.
    std::unordered_map<std::uint16_t, ChannelStats> perChannel;

    /// Overall integrity verification status, using the vocabulary of the
    /// OSF5 integrity profile: `"none"`, `"crc_valid"`, `"invalid"`, or
    /// `"signature_unverifiable"`. This build implements level `crc`; a
    /// signed file always reports `signature_unverifiable` (its CRC layer is
    /// still checked and the file stays readable).
    [[nodiscard]] std::string_view verificationStatus() const noexcept {
        switch (integrity) {
            case IntegrityProfile::None:
                return "none";
            case IntegrityProfile::Ed25519:
                return "signature_unverifiable";
            case IntegrityProfile::Crc32c:
                return blocksCrcFailed > 0 ? "invalid" : "crc_valid";
        }
        return "none";
    }
};

/// Format a byte count using the `1.23 MB` style (binary KB / MB / GB thresholds).
[[nodiscard]] std::string formatBytes(std::uint64_t bytes);

/// Format a duration using the `7 ms` / `1.23 s` style.
[[nodiscard]] std::string formatDuration(std::chrono::nanoseconds d);

/// String name of a compression format (`none` / `zlib` / `gzip`).
[[nodiscard]] std::string_view compressionFormatName(CompressionFormat f) noexcept;

/// Multi-line summary suitable for CLI output.
std::ostream& operator<<(std::ostream& os, ReaderStats const& s);

/// One-line summary suitable for per-channel listings.
std::ostream& operator<<(std::ostream& os, ChannelStats const& s);

}  // namespace osf
