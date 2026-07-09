// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// \file types.h
/// Core OSF type enumerations.
///
/// These mirror the spec revision **2026-05-04** datatype set. Removed
/// datatypes (`pair`, `triple`, `candata`, `gpsdata`) are intentionally
/// absent — readers must reject those legacy strings rather than silently
/// mapping them to a current type.
///
/// Forward-compatible variants `DataType::Unsupported` and
/// `ChannelType::Unsupported` exist so a file using a future-spec
/// spelling still parses channel-by-channel without aborting the whole
/// metablock. The on-disk spelling is preserved alongside on the
/// owning `Channel` (`Channel::dataTypeRaw`,
/// `Channel::channelTypeRaw`) so callers can still produce useful
/// diagnostics. Block reads against a channel whose type is
/// `Unsupported` will fail explicitly when later attempted.

#pragma once

#include <string_view>

#include <osf/error.h>

namespace osf {

/// Data type carried by an OSF channel. Matches the on-disk `datatype`
/// attribute / JSON field exactly (lowercase, ASCII).
enum class DataType {
    /// 1-byte boolean (`0x00` = false, anything else = true).
    Bool,
    /// Signed 8-bit integer.
    Int8,
    /// Signed 16-bit integer (little-endian on disk).
    Int16,
    /// Signed 32-bit integer (little-endian on disk).
    Int32,
    /// Signed 64-bit integer (little-endian on disk).
    Int64,
    /// Unsigned 8-bit integer. Added in spec revision 2026-05-04.
    UInt8,
    /// Unsigned 16-bit integer. Added in spec revision 2026-05-04.
    UInt16,
    /// Unsigned 32-bit integer. Added in spec revision 2026-05-04.
    UInt32,
    /// Unsigned 64-bit integer. Added in spec revision 2026-05-04.
    UInt64,
    /// IEEE-754 single-precision floating point.
    Float,
    /// IEEE-754 double-precision floating point.
    Double,
    /// UTF-8 string. On-disk layout is version-deterministic per spec
    /// rev 2026-05-24: OSF4 writers MUST append a trailing `0x00`
    /// terminator and OSF4 readers MUST strip it; OSF5 writers MUST
    /// NOT append it and OSF5 readers MUST NOT strip it.
    String,
    /// Opaque byte payload. Same version-deterministic null-terminator
    /// rule as `String`: present in OSF4, absent in OSF5.
    Binary,
    /// Reserved spelling for the read-side alias `bytearray`. The
    /// metablock parser normalises `bytearray` directly to `Binary`
    /// on read, and the writer always emits `binary`. This enumerator
    /// is kept so downstream code can express "input was spelled
    /// `bytearray`" if it ever needs the distinction; the metablock
    /// parser does not produce it.
    ByteArray,
    /// 24-byte struct of `latitude`, `longitude`, `altitude` as
    /// little-endian `double`s. Renamed from `gpsdata` in spec
    /// revision 2026-05-04.
    GpsLocation,
    /// Forward-compatibility sentinel. Carries no data on its own;
    /// the on-disk spelling lives on the owning `Channel::dataTypeRaw`.
    Unsupported,
};

/// The logical **data shape** of a channel — the OSF `channeltype`
/// metablock attribute.
///
/// This is the channel's structure, NOT its storage mode: whether a channel
/// is equidistant or timestamped is derived at read time from the block
/// control byte (`bcStartData`/`bcContinuedData` ⇒ equidistant;
/// `bcAbsTimeStampData` ⇒ per-sample timestamps) together with
/// `timeIncrementNs` — never from `channeltype`. The wire strings
/// `equidistant`/`timestamped` are therefore NOT channeltypes and never
/// appear in a conformant file.
///
/// The spec value set is `scalar`, `vector`, `matrix`, `binary`
/// (`docs/de/osf_general.md` channel-field reference + "Kanaltypen";
/// `osf4.md`). A missing `channeltype` defaults to `Scalar`.
enum class ChannelType {
    /// One value per point in time — the most common shape (default when the
    /// `channeltype` attribute is absent).
    Scalar,
    /// A sequence of values per block (e.g. an FFT spectrum). Full vector
    /// payload decoding is a future feature; the channel is kept and read.
    Vector,
    /// A two-dimensional structure per timestamp (e.g. a rainflow matrix).
    Matrix,
    /// Arbitrary binary blocks — one blob per point in time (typically with a
    /// `mimetype`). Payload-equivalent to a `scalar` channel of `datatype`
    /// `binary`.
    Binary,
    /// Forward-compatibility sentinel. The on-disk spelling lives on
    /// `Channel::channelTypeRaw`.
    Unsupported,
};

/// Subtype for spectrum channels. Defaults to `Amplitude` when the
/// metablock omits the field or carries a spelling unknown to this
/// build.
enum class SpectrumType {
    /// Magnitude only (default).
    Amplitude,
    /// Real and imaginary parts.
    RealImag,
    /// Magnitude and phase in radians.
    AmpPhaseRad,
    /// Magnitude and phase in degrees.
    AmpPhaseDeg,
};

/// Resolve a wire-format datatype string to a `DataType` variant.
///
/// Behaviour:
/// - Known current spelling → corresponding enumerator.
/// - `bytearray` → `DataType::Binary` (read-side alias).
/// - Removed datatype (`pair`, `triple`, `candata`, `gpsdata`) → hard
///   error `Error::Code::RemovedInSpec`. Without a correct datatype the
///   binary blocks for this channel cannot be decoded.
/// - Anything else → `DataType::Unsupported`. The metablock as a whole
///   still parses; block reads against this channel will fail
///   explicitly when later attempted.
[[nodiscard]] Result<DataType> parseDataType(std::string_view raw);

/// Resolve a wire-format channel-type string to a `ChannelType`
/// variant. Unknown spellings produce `ChannelType::Unsupported`.
/// Currently never returns an error — the `Result` return type is
/// reserved for future spec revisions that retire a value.
[[nodiscard]] Result<ChannelType> parseChannelType(std::string_view raw);

/// Resolve a wire-format spectrum-type string to a `SpectrumType`
/// variant. Unknown spellings resolve to `Amplitude` (the spec
/// default for missing or unrecognised spectrum-type metadata).
[[nodiscard]] SpectrumType parseSpectrumType(std::string_view raw) noexcept;

}  // namespace osf
