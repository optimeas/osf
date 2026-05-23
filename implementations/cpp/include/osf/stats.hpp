// SPDX-License-Identifier: MIT

/// \file stats.hpp
/// Reader-side telemetry.
///
/// `ReaderStats` is populated by `BlockReader` during iteration. The
/// fields are deliberately concrete (counts and sizes, not opaque
/// metric handles) so any application using `osf-core` can format the
/// values without having to introspect.
///
/// Skip reasons are tracked separately rather than under a single
/// `blocks_skipped` counter because each carries different operational
/// meaning:
///
/// - **Unsupported channels** (forward-compat skips) usually mean the
///   file uses a future-spec datatype this build does not yet handle.
/// - **Deprecated block types** (`bcTrustedTimestamp`,
///   `bcStatusEvent`, `bcMessageEvent`) appear in older field files
///   such as `examples/motorbike.osf` and tell you the file predates
///   spec rev 2026-05-04.
/// - **Reserved block types** (`bcReserved`, `bcTimebaseRealign`,
///   anything with bits 0–6 ≥ 9) are either spec-internal or
///   genuinely unknown.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace osf {

/// Compression format detected on the input stream. Mirrors the
/// future `osf::compression::CompressionFormat` enum so callers do
/// not need to import the lower-level type once OSFZ lands in
/// Phase 8.
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
    std::uint64_t blocks_read = 0;
    /// Blocks the reader skipped on this channel (any reason).
    std::uint64_t blocks_skipped = 0;
    /// Sum of sample counts across all blocks of this channel
    /// (`string` / `binary` count one sample per block).
    std::uint64_t samples_total = 0;
    /// Sum of length-field values across all blocks of this channel.
    /// Useful as a rough payload-size proxy.
    std::uint64_t bytes_payload = 0;
    /// Number of `bcStartData` blocks observed (= number of distinct
    /// equidistant segments).
    std::uint32_t segments = 0;
    /// Earliest and latest absolute timestamp the reader observed for
    /// this channel. `std::nullopt` if the channel only produced
    /// blocks without absolute timestamps.
    std::optional<std::pair<std::int64_t, std::int64_t>> time_range_ns;

    /// Extend the recorded time range to include `ts`. Used by the
    /// reader on each `bcStartData` (with the reconstructed last
    /// sample timestamp) and on every per-sample timestamp emitted by
    /// `bcAbsTimeStampData`.
    void observe_timestamp(std::int64_t ts) noexcept;
};

/// Aggregated counts and timings produced by reading an OSF file.
struct ReaderStats {
    /// Size of the source file in bytes, when known. `std::nullopt`
    /// for streaming sources.
    std::optional<std::uint64_t> file_size_bytes;
    /// Bytes consumed by the magic-header line (populated by a
    /// convenience wrapper; the `BlockReader` does not see the header
    /// itself).
    std::uint64_t header_size_bytes = 0;
    /// Bytes consumed by the metablock body (matches the
    /// `metablock_len` field of the magic header).
    std::uint64_t metablock_size_bytes = 0;
    /// Bytes consumed by the block-stream section. Includes channel
    /// indices, length prefixes, control bytes, and payloads;
    /// excludes the magic-header and metablock bytes.
    std::uint64_t data_section_size_bytes = 0;
    /// Wall-clock time elapsed while the `BlockReader` was iterating.
    /// Refreshed on each `BlockReader::stats()` call.
    std::chrono::nanoseconds elapsed{0};

    /// Number of channels declared in the metablock.
    std::size_t channels_total = 0;
    /// Channels that produced at least one block during iteration.
    /// Recomputed by `BlockReader::stats()` on demand.
    std::size_t channels_with_data = 0;
    /// Channels declared with `DataType::Unsupported` or
    /// `ChannelType::Unsupported`.
    std::size_t channels_unsupported = 0;

    /// Total number of blocks observed (`blocks_read + blocks_skipped_*`).
    /// Recomputed by `BlockReader::stats()` on demand.
    std::uint64_t blocks_total = 0;
    /// Blocks the reader produced as typed `BlockKind` variants
    /// (everything except `Skipped`).
    std::uint64_t blocks_read = 0;
    /// Blocks skipped because the channel's data or channel type was
    /// `Unsupported`.
    std::uint64_t blocks_skipped_unsupported = 0;
    /// Blocks skipped because the control byte identified a
    /// deprecated block type.
    std::uint64_t blocks_skipped_deprecated_type = 0;
    /// Blocks skipped because the control byte identified a reserved
    /// block type.
    std::uint64_t blocks_skipped_reserved_type = 0;
    /// Number of blocks the reader could not finish before the stream
    /// ended. Capped at 1 by construction.
    std::uint64_t blocks_truncated = 0;
    /// Whether the optional `0xFFFF` info-data block was encountered.
    bool trailer_seen = false;

    /// Whether the source stream was OSFZ-compressed and went through
    /// transparent decompression on read. `false` for plain OSF.
    /// Populated by the future OSFZ layer; default `false` keeps the
    /// reader correct in the meantime.
    bool compressed = false;
    /// Detected compression format on the source stream.
    CompressionFormat compression_format = CompressionFormat::None;

    /// Per-channel detail keyed by channel index. The reader seeds
    /// every metablock channel with a zero-filled entry on
    /// construction.
    std::unordered_map<std::uint16_t, ChannelStats> per_channel;
};

/// Format a byte count using the `1.23 MB` style used by the reference
/// implementations (binary KB / MB / GB thresholds).
[[nodiscard]] std::string format_bytes(std::uint64_t bytes);

/// Format a duration using the `7 ms` / `1.23 s` style used by the
/// reference implementations.
[[nodiscard]] std::string format_duration(std::chrono::nanoseconds d);

/// String name of a compression format (`none` / `zlib` / `gzip`).
[[nodiscard]] std::string_view compression_format_name(CompressionFormat f) noexcept;

/// Multi-line summary suitable for CLI output. Mirrors the Rust
/// `Display for ReaderStats` impl.
std::ostream& operator<<(std::ostream& os, ReaderStats const& s);

/// One-line summary suitable for per-channel listings. Mirrors the
/// Rust `Display for ChannelStats` impl.
std::ostream& operator<<(std::ostream& os, ChannelStats const& s);

}  // namespace osf
