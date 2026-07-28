// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// \file block.h
/// OSF block model.
///
/// Every block in the binary stream consists of a 2-byte channel
/// index, a length field (2 or 4 bytes per the channel's
/// `sizeoflengthvalue`), a 1-byte control byte, and a payload. This
/// header defines the typed representation a block lands in once the
/// reader has decoded it. The on-disk encoding itself is documented in
/// the OSF format specification §"Control byte" / "Data structure per
/// control type".
///
/// Two design choices are intentional:
///
/// 1. **Unpacked typed payloads, not zero-copy.** Each `Block` carries
///    its samples in pre-allocated typed vectors (`std::vector<double>`,
///    `std::vector<std::int32_t>`, …). Block sizes in OSF are typically
///    KB to a few MB, so the per-block allocation is acceptable, and
///    the lifetime story is dramatically simpler than a borrowing API.
///    A zero-copy `RawBlockReader` is conceivable as a future second
///    layer if profiling justifies it — not now.
///
/// 2. **Skipped blocks remain visible.** Blocks the reader cannot
///    interpret (deprecated control bytes, `Unsupported` channels,
///    unknown control values) come through as `BlockKind::Skipped`
///    rather than being silently swallowed. Stream position is
///    preserved either way; the caller can opt to inspect skipped
///    payloads via `BlockReader::withCaptureSkippedPayload`.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace osf {

/// Special channel index reserved for the optional OSF4 info-data
/// block at the end of a file. OSF5 no longer writes it but readers
/// must tolerate it. The `BlockReader` consumes it silently and never
/// yields it as a regular `Block`.
inline constexpr std::uint16_t TRAILER_CHANNEL_INDEX = 0xFFFF;

/// Reserved channel index of the file-wide integrity signature block
/// (`bcIntegritySignature`, control byte 9) at integrity level `signed`.
/// Not declared in the metablock; readers without level-signed support skip
/// it via its (always u32) length field.
inline constexpr std::uint16_t SIGNATURE_CHANNEL_INDEX = 0xFFFE;

/// Length of the magic trailer string written after the info block in
/// OSF4 files (`OSF_STREAM_END <pos>===…`). Padded to exactly 40 bytes.
inline constexpr std::size_t MAGIC_TRAILER_LEN = 40;

/// 24-byte `gpslocation` payload: three little-endian `double`s in the
/// order `latitude`, `longitude`, `altitude` per spec revision
/// 2026-05-04.
struct GpsLocation {
    /// Latitude in decimal degrees, north-positive.
    double latitude = 0.0;
    /// Longitude in decimal degrees, east-positive.
    double longitude = 0.0;
    /// Altitude in meters above the WGS-84 ellipsoid.
    double altitude = 0.0;

    friend bool operator==(GpsLocation const& a, GpsLocation const& b) noexcept {
        return a.latitude == b.latitude && a.longitude == b.longitude &&
               a.altitude == b.altitude;
    }
    friend bool operator!=(GpsLocation const& a, GpsLocation const& b) noexcept {
        return !(a == b);
    }
};

// ---------------------------------------------------------------------
// Typed payload sum types
// ---------------------------------------------------------------------

/// Equidistant numeric payload: a single typed vector per supported
/// numeric data type. `string`, `binary`, and `gpslocation` may not
/// appear here per spec — equidistant blocks are numeric-only.
using NumericPayload = std::variant<
    std::vector<bool>,           // DataType::Bool
    std::vector<std::int8_t>,    // DataType::Int8
    std::vector<std::int16_t>,   // DataType::Int16
    std::vector<std::int32_t>,   // DataType::Int32
    std::vector<std::int64_t>,   // DataType::Int64
    std::vector<std::uint8_t>,   // DataType::UInt8
    std::vector<std::uint16_t>,  // DataType::UInt16
    std::vector<std::uint32_t>,  // DataType::UInt32
    std::vector<std::uint64_t>,  // DataType::UInt64
    std::vector<float>,          // DataType::Float
    std::vector<double>          // DataType::Double
>;

/// Number of samples held by a `NumericPayload`. O(1).
[[nodiscard]] std::size_t numericPayloadLen(NumericPayload const& p) noexcept;

/// Equivalent to `numericPayloadLen(p) == 0`.
[[nodiscard]] bool numericPayloadEmpty(NumericPayload const& p) noexcept;

/// Timestamped payload: `(timestampNs, value)` pairs per supported
/// data type. `string`, `binary`, and `gpslocation` only occur here
/// (equidistant blocks are numeric-only per spec).
///
/// For `string` and `binary`, the null-terminator handling is
/// version-deterministic per spec rev 2026-05-24: OSF4 input has the
/// spec-mandated trailing `0x00` byte stripped by the reader before
/// it lands in the variant; OSF5 input is delivered verbatim, so a
/// trailing `0x00` is part of the payload.
using TimestampedPayload = std::variant<
    std::vector<std::pair<std::int64_t, bool>>,
    std::vector<std::pair<std::int64_t, std::int8_t>>,
    std::vector<std::pair<std::int64_t, std::int16_t>>,
    std::vector<std::pair<std::int64_t, std::int32_t>>,
    std::vector<std::pair<std::int64_t, std::int64_t>>,
    std::vector<std::pair<std::int64_t, std::uint8_t>>,
    std::vector<std::pair<std::int64_t, std::uint16_t>>,
    std::vector<std::pair<std::int64_t, std::uint32_t>>,
    std::vector<std::pair<std::int64_t, std::uint64_t>>,
    std::vector<std::pair<std::int64_t, float>>,
    std::vector<std::pair<std::int64_t, double>>,
    std::vector<std::pair<std::int64_t, std::string>>,
    std::vector<std::pair<std::int64_t, std::vector<std::uint8_t>>>,
    std::vector<std::pair<std::int64_t, GpsLocation>>
>;

/// Number of samples held by a `TimestampedPayload`.
[[nodiscard]] std::size_t timestampedPayloadLen(TimestampedPayload const& p) noexcept;

/// Equivalent to `timestampedPayloadLen(p) == 0`.
[[nodiscard]] bool timestampedPayloadEmpty(TimestampedPayload const& p) noexcept;

/// Relative-timestamped payload (OSF4-only `bcContinuedRelStampData`):
/// `(deltaNs, value)` pairs where `deltaNs` is the unsigned 32-bit
/// offset from the previous sample of the same channel. Numeric data
/// types only per spec.
using RelTimestampedPayload = std::variant<
    std::vector<std::pair<std::uint32_t, bool>>,
    std::vector<std::pair<std::uint32_t, std::int8_t>>,
    std::vector<std::pair<std::uint32_t, std::int16_t>>,
    std::vector<std::pair<std::uint32_t, std::int32_t>>,
    std::vector<std::pair<std::uint32_t, std::int64_t>>,
    std::vector<std::pair<std::uint32_t, std::uint8_t>>,
    std::vector<std::pair<std::uint32_t, std::uint16_t>>,
    std::vector<std::pair<std::uint32_t, std::uint32_t>>,
    std::vector<std::pair<std::uint32_t, std::uint64_t>>,
    std::vector<std::pair<std::uint32_t, float>>,
    std::vector<std::pair<std::uint32_t, double>>
>;

/// Number of samples held by a `RelTimestampedPayload`.
[[nodiscard]] std::size_t relTimestampedPayloadLen(
    RelTimestampedPayload const& p) noexcept;

/// Equivalent to `relTimestampedPayloadLen(p) == 0`.
[[nodiscard]] bool relTimestampedPayloadEmpty(
    RelTimestampedPayload const& p) noexcept;

// ---------------------------------------------------------------------
// Skip reasons
// ---------------------------------------------------------------------

/// Why a block ended up as `BlockKind::Skipped`.
struct SkipReason {
    enum class Kind {
        /// The channel's `dataType` is `DataType::Unsupported` —
        /// either a future-spec spelling or one this build does not
        /// implement.
        UnsupportedDataType,
        /// The channel's `channelType` is `ChannelType::Unsupported`.
        UnsupportedChannelType,
        /// Deprecated control byte that newer writers no longer emit
        /// but readers must tolerate (1 = `bcTrustedTimestamp`,
        /// 3 = `bcStatusEvent`, 4 = `bcMessageEvent`). `rawByte`
        /// carries the value.
        DeprecatedBlockType,
        /// Reserved control byte (0 = `bcReserved`,
        /// 2 = `bcTimebaseRealign`) or any value the spec does not
        /// currently define. `rawByte` carries the value.
        ReservedBlockType,
        /// The block's frame CRC (integrity profile level `crc`) did not
        /// match the recomputed CRC32C. The block is dropped best-effort so
        /// the rest of the file stays readable.
        CrcFailed,
        /// An integrity signature block (`bcIntegritySignature = 9` on the
        /// reserved channel `0xFFFE`). This build reads level `crc` but does
        /// not verify signatures, so the block is skipped via its length
        /// field.
        SignatureBlock,
        /// The block's length field read `0`. A conforming block always
        /// carries at least its control byte, so this is a non-conforming
        /// writer artefact (OSF-UP3, DECISIONS §25). The frame is nothing but
        /// the channel index and the length field — both already consumed —
        /// so the reader counts it and keeps scanning. `rawByte` is zero and
        /// carries no meaning here.
        ZeroLengthBlock,
    };

    Kind kind = Kind::ReservedBlockType;
    /// Raw control-byte value (low 7 bits). Only meaningful for
    /// `DeprecatedBlockType` / `ReservedBlockType`; zero for every other
    /// `Kind`, including `ZeroLengthBlock` (no control byte is ever read).
    std::uint8_t rawByte = 0;

    friend bool operator==(SkipReason const& a, SkipReason const& b) noexcept {
        return a.kind == b.kind && a.rawByte == b.rawByte;
    }
    friend bool operator!=(SkipReason const& a, SkipReason const& b) noexcept {
        return !(a == b);
    }
};

// ---------------------------------------------------------------------
// BlockKind discriminated union
// ---------------------------------------------------------------------

/// `bcStartData`: opens a new equidistant segment with absolute start
/// timestamp and sample rate; carries the first samples. Allowed only
/// for numeric data types per spec.
struct StartData {
    /// Absolute start timestamp in nanoseconds since the Unix epoch
    /// (UTC).
    std::int64_t startTimestampNs = 0;
    /// Sample rate in Hz, valid until the next `bcStartData` of the
    /// same channel.
    double sampleRateHz = 0.0;
    /// Sample values.
    NumericPayload samples;
};

/// `bcContinuedData`: continuation of the current equidistant segment
/// of the same channel. Time per sample is `1 / sampleRateHz` from
/// the most recent `StartData`. Numeric only.
struct ContinuedData {
    /// Sample values, picking up where the previous block ended.
    NumericPayload samples;
};

/// `bcAbsTimeStampData`: each sample carries its own absolute
/// timestamp. Supports all data types including `string` and `binary`.
struct AbsTimestampData {
    /// `(timestampNs, value)` pairs in stream order.
    TimestampedPayload samples;
};

/// `bcContinuedRelStampData`: OSF4-only. Each sample carries a
/// relative time delta in nanoseconds (`uint32`) plus a value. Reader
/// supports it; writers never produce it.
struct ContinuedRelStampData {
    /// `(deltaNs, value)` pairs in stream order.
    RelTimestampedPayload samples;
};

/// Block kept for stream-position purposes after the reader chose not
/// to interpret it: known control byte but channel marked
/// `Unsupported`, or block type intentionally skipped.
struct Skipped {
    /// Why the reader skipped this block.
    SkipReason reason;
    /// Number of payload bytes the reader had to consume from the
    /// stream (control byte + payload). Always ≥ 0.
    std::uint64_t bytesSkipped = 0;
    /// Captured payload bytes after the control byte. Default is
    /// `std::nullopt` (bytes are dropped without allocation). Opt in
    /// with `BlockReader::withCaptureSkippedPayload(true)`.
    std::optional<std::vector<std::uint8_t>> payload;
};

/// Discriminated union of OSF block payloads. The active alternative
/// matches the decoded control byte:
/// - `StartData` for control byte 6.
/// - `ContinuedData` for control byte 5.
/// - `AbsTimestampData` for control byte 8.
/// - `ContinuedRelStampData` for control byte 7.
/// - `Skipped` for everything else (deprecated, reserved, unknown).
using BlockKind = std::variant<StartData, ContinuedData, AbsTimestampData,
                               ContinuedRelStampData, Skipped>;

/// A decoded data block from an OSF stream.
struct Block {
    /// Channel this block belongs to. `0xFFFF` (TRAILER_CHANNEL_INDEX)
    /// is reserved for the optional OSF4 info-data block and is not
    /// produced as a regular `Block` by the reader.
    std::uint16_t channelIndex = 0;
    /// Decoded payload.
    BlockKind kind;
};

// ---------------------------------------------------------------------
// Control byte decoding (used by the reader implementation; exposed for
// unit testing).
// ---------------------------------------------------------------------

/// Block-type discriminator extracted from the lower 7 bits of the
/// control byte.
enum class ControlKind {
    /// 0 — `bcReserved`. Originally `bcMetaData`, never used.
    Reserved,
    /// 1 — `bcTrustedTimestamp`. Deprecated; readers skip.
    TrustedTimestamp,
    /// 2 — `bcTimebaseRealign`. Deprecated; readers skip.
    TimebaseRealign,
    /// 3 — `bcStatusEvent`. Deprecated; readers skip.
    StatusEvent,
    /// 4 — `bcMessageEvent`. Deprecated; readers skip.
    MessageEvent,
    /// 5 — `bcContinuedData`. Equidistant continuation block.
    ContinuedData,
    /// 6 — `bcStartData`. Equidistant start block with sample rate.
    StartData,
    /// 7 — `bcContinuedRelStampData`. OSF4-only on read.
    ContinuedRelStampData,
    /// 8 — `bcAbsTimeStampData`. Per-sample absolute timestamps.
    AbsTimeStampData,
    /// Anything else (bits 0–6 ≥ 9). The raw value can be recovered
    /// from the original byte by `byte & 0x7F`.
    Unknown,
};

/// Decoded form of the 1-byte control byte.
struct ControlByte {
    ControlKind kind = ControlKind::Reserved;
    /// Low 7 bits of the original byte. Useful when `kind == Unknown`
    /// so diagnostics can quote the raw value.
    std::uint8_t raw = 0;
    /// Bit 7 of the control byte. When set, the payload begins with
    /// a `uint32` sample count `N`; when clear, exactly one sample
    /// follows. One exception: `bcAbsTimeStampData` for `string` and
    /// `binary` always sets bit 7 per spec.
    bool multiSample = false;
};

/// Decode the 1-byte control byte that follows the length field.
[[nodiscard]] ControlByte decodeControlByte(std::uint8_t byte) noexcept;

}  // namespace osf
