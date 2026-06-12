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
/// owning `Channel` (`Channel::data_type_raw`,
/// `Channel::channel_type_raw`) so callers can still produce useful
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
    /// the on-disk spelling lives on the owning `Channel::data_type_raw`.
    Unsupported,
};

/// Whether a channel stores values at a fixed sample rate
/// (`equidistant`/`scalar` with a non-zero `timeincrement`) or with
/// an explicit timestamp per sample (`timestamped`/`scalar` with
/// `timeincrement` 0 or absent).
///
/// The on-disk strings `scalar`, `timestamped`, and `equidistant` all
/// occur in the wild; the parser treats `scalar` as the canonical
/// spelling for both equidistant and timestamped channels and uses
/// `time_increment_ns` on the channel to disambiguate.
enum class ChannelType {
    /// Default channel type used by the OSFGenerator and most field
    /// devices. The actual layout (equidistant vs. timestamped) is
    /// derived from `Channel::time_increment_ns`.
    Scalar,
    /// Channel with a fixed sample rate; timestamps are reconstructed
    /// from `bcStartData` segments and the sample index.
    Equidistant,
    /// Channel with an absolute timestamp per sample.
    Timestamped,
    /// Forward-compatibility sentinel. The on-disk spelling lives on
    /// `Channel::channel_type_raw`.
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
[[nodiscard]] Result<DataType> parse_data_type(std::string_view raw);

/// Resolve a wire-format channel-type string to a `ChannelType`
/// variant. Unknown spellings produce `ChannelType::Unsupported`.
/// Currently never returns an error — the `Result` return type is
/// reserved for future spec revisions that retire a value.
[[nodiscard]] Result<ChannelType> parse_channel_type(std::string_view raw);

/// Resolve a wire-format spectrum-type string to a `SpectrumType`
/// variant. Unknown spellings resolve to `Amplitude` (the spec
/// default for missing or unrecognised spectrum-type metadata).
[[nodiscard]] SpectrumType parse_spectrum_type(std::string_view raw) noexcept;

}  // namespace osf
