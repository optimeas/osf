// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// \file datachannel.h
/// Typed in-memory channel model.
///
/// Where `osf::Block` is the per-block raw view from `osf::BlockReader`,
/// this header is the per-channel aggregated view: equidistant samples
/// grouped by segment, timestamped samples in parallel
/// timestamp + value vectors, and string / binary samples in their own
/// variant. `osf::DataManager` builds these structs from a block stream
/// so applications can ignore the on-disk block boundaries.
///
/// Three channel layouts because the storage genuinely differs:
///
/// | Variant         | Storage                                       |
/// |-----------------|-----------------------------------------------|
/// | `Equidistant`   | flat `NumericValues` + `std::vector<Segment>` |
/// | `Timestamped`   | parallel `std::vector<i64>` + `NumericValues` |
/// | `Variable`      | timestamps + string XOR binary vectors        |
///
/// Equidistant channels reconstruct sample timestamps lazily when
/// `samplesVector()` is called, using the per-segment
/// `(startTimestampNs, sampleRateHz)`. Spec rev 2026-05-04 makes
/// multiple segments per channel explicit; every `bcStartData` opens
/// a new one.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <osf/block.h>
#include <osf/error.h>
#include <osf/metablock.h>
#include <osf/types.h>

namespace osf {

// =====================================================================
// NumericValues — per-channel sample storage for the numeric + GPS
// datatypes.
// =====================================================================

/// One sample storage vector per supported numeric data type plus
/// `GpsLocation`. `Equidistant` channels never carry the GPS variant
/// per spec, but the type is shared with timestamped channels which
/// do.
using NumericValues = std::variant<
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
    std::vector<double>,         // DataType::Double
    std::vector<GpsLocation>     // DataType::GpsLocation
>;

/// Number of samples held by a `NumericValues`. O(1).
[[nodiscard]] std::size_t numericValuesLen(NumericValues const& v) noexcept;

/// Equivalent to `numericValuesLen(v) == 0`.
[[nodiscard]] bool numericValuesEmpty(NumericValues const& v) noexcept;

/// `DataType` enum matching the active alternative.
[[nodiscard]] DataType numericValuesDataType(NumericValues const& v) noexcept;

/// Build an empty `NumericValues` whose active alternative matches
/// `dt`. Returns `std::nullopt` for variable-length types (`String`,
/// `Binary`, `ByteArray`) and for `Unsupported`.
[[nodiscard]] std::optional<NumericValues> numericValuesEmptyFor(
    DataType dt) noexcept;

// =====================================================================
// Segment + ChannelMeta
// =====================================================================

/// One equidistant segment within an `EquidistantChannel`. Every
/// `bcStartData` block opens a new one with its own absolute start
/// time and sample rate.
struct Segment {
    /// Absolute start timestamp of this segment in nanoseconds.
    std::int64_t startTimestampNs = 0;
    /// Sample rate in Hz, valid until the next segment of this
    /// channel.
    double sampleRateHz = 0.0;
    /// First sample of this segment in the channel's flat
    /// `NumericValues` vector.
    std::size_t startIndex = 0;
    /// Number of samples belonging to this segment.
    std::size_t sampleCount = 0;
};

/// Secondary channel-definition fields preserved from the metablock
/// so downstream code can introspect them without keeping a
/// back-reference to the `MetaBlock`.
struct ChannelMeta {
    /// Original on-disk channel-type spelling (`scalar`,
    /// `equidistant`, `timestamped`).
    ChannelType channelType = ChannelType::Scalar;
    /// Length-prefix width on disk (2 or 4 bytes per spec).
    std::uint8_t sizeOfLengthValue = 0;
    /// Sample period in nanoseconds, if the metablock declared one.
    std::optional<std::int64_t> timeIncrementNs;
    /// Free-form reference identifier.
    std::optional<std::string> reference;
    /// Physical dimension (e.g. `temperature`).
    std::optional<std::string> physicalDimension;
    /// Free-form comment.
    std::optional<std::string> comment;
    /// Spectrum subtype, if the channel carries spectral data.
    std::optional<SpectrumType> spectrumType;
};

// =====================================================================
// Sample iteration types — value classes returned by samplesVector.
// =====================================================================

/// Single numeric (or GPS) sample value, discriminated by data type.
/// GpsLocation is held by value (24 bytes — trivial to copy).
using NumericValueRef = std::variant<
    bool,
    std::int8_t,
    std::int16_t,
    std::int32_t,
    std::int64_t,
    std::uint8_t,
    std::uint16_t,
    std::uint32_t,
    std::uint64_t,
    float,
    double,
    GpsLocation
>;

/// Single string-or-binary sample value, discriminated by data type.
/// Held by value so the returned `Sample` is self-contained even after
/// the channel goes out of scope.
struct VariableValueRef {
    enum class Kind { String, Binary };
    Kind kind = Kind::String;
    std::string stringValue;
    std::vector<std::uint8_t> binaryValue;
};

/// Generic `(timestampNs, value)` pair.
template <typename T>
struct Sample {
    std::int64_t timestampNs = 0;
    T value;
};

// =====================================================================
// EquidistantChannel
// =====================================================================

/// Equidistant numeric channel — flat sample storage plus a list of
/// segments. `bcStartData` opens a new segment; `bcContinuedData`
/// extends the most recent one.
struct EquidistantChannel {
    /// Channel index from the metablock.
    std::uint16_t index = 0;
    /// Fully qualified channel name.
    std::string name;
    /// Datatype of the samples — every numeric type plus
    /// `GpsLocation` is possible (though equidistant GPS is rare).
    DataType dataType = DataType::Unsupported;
    /// Optional physical unit string (e.g. `°C`).
    std::optional<std::string> physicalUnit;
    /// Optional display name.
    std::optional<std::string> displayName;
    /// Secondary channel-definition fields preserved from the
    /// metablock.
    ChannelMeta channelDef;

    /// Flat sample storage: every segment's samples appended
    /// head-to-tail.
    NumericValues samples{std::vector<double>{}};
    /// One entry per `bcStartData`; `startIndex..startIndex +
    /// sampleCount` indexes into `samples`.
    std::vector<Segment> segments;

    /// Reconstruct `(timestamp, value)` pairs for every sample. Per
    /// the spec, sample `i` within a segment lands at
    /// `segment.start + i * (1e9 / sampleRateHz)`. Time gaps
    /// between consecutive segments are NOT interpolated.
    [[nodiscard]] std::vector<Sample<NumericValueRef>> samplesVector() const;
};

// =====================================================================
// TimestampedChannel
// =====================================================================

/// Timestamped numeric channel. Every sample carries an absolute
/// timestamp; `bcAbsTimeStampData` blocks append directly,
/// `bcContinuedRelStampData` deltas are converted to absolute time on
/// read.
struct TimestampedChannel {
    /// Channel index from the metablock.
    std::uint16_t index = 0;
    /// Fully qualified channel name.
    std::string name;
    /// Datatype of the samples.
    DataType dataType = DataType::Unsupported;
    /// Optional physical unit string.
    std::optional<std::string> physicalUnit;
    /// Optional display name.
    std::optional<std::string> displayName;
    /// Secondary channel-definition fields preserved from the
    /// metablock.
    ChannelMeta channelDef;

    /// Absolute timestamps in nanoseconds since the Unix epoch (UTC),
    /// in stream order — same length as `values`.
    std::vector<std::int64_t> timestampsNs;
    /// Sample values, parallel to `timestampsNs`.
    NumericValues values{std::vector<double>{}};

    /// Pair every timestamp with its sample value.
    [[nodiscard]] std::vector<Sample<NumericValueRef>> samplesVector() const;
};

// =====================================================================
// VariableChannel — string and binary samples.
// =====================================================================

/// Timestamped channel for string and binary data. Variable-length
/// payloads land in either `stringValues` (when `dataType` is
/// `String`) or `binaryValues` (when `dataType` is `Binary`); the
/// other field stays `std::nullopt`.
struct VariableChannel {
    /// Channel index from the metablock.
    std::uint16_t index = 0;
    /// Fully qualified channel name.
    std::string name;
    /// Datatype — exactly one of `DataType::String` or
    /// `DataType::Binary`.
    DataType dataType = DataType::String;
    /// Optional physical unit string (rare for string / binary).
    std::optional<std::string> physicalUnit;
    /// Optional display name.
    std::optional<std::string> displayName;
    /// MIME type for binary channels (e.g. `image/jpeg`).
    std::optional<std::string> mimeType;
    /// Secondary channel-definition fields preserved from the
    /// metablock.
    ChannelMeta channelDef;

    /// Absolute timestamps in nanoseconds since the Unix epoch (UTC).
    std::vector<std::int64_t> timestampsNs;
    /// String samples; set for `dataType == String`.
    std::optional<std::vector<std::string>> stringValues;
    /// Binary samples; set for `dataType == Binary`.
    std::optional<std::vector<std::vector<std::uint8_t>>> binaryValues;

    /// Read the channel's string samples.
    ///
    /// \returns `DataTypeAccessMismatch` when the channel holds
    /// binary data instead.
    [[nodiscard]] Result<std::vector<std::string> const*> asStrings() const;

    /// Read the channel's binary samples.
    ///
    /// \returns `DataTypeAccessMismatch` when the channel holds
    /// string data instead.
    [[nodiscard]] Result<std::vector<std::vector<std::uint8_t>> const*>
        asBinaries() const;

    /// Pair every timestamp with its value (string or binary).
    [[nodiscard]] std::vector<Sample<VariableValueRef>> samplesVector() const;
};

// =====================================================================
// Channel — top-level variant.
// =====================================================================

/// One of the three storage layouts. Distinct from `osf::Channel`
/// (which is the metablock-level channel *definition*); this variant
/// represents the actual *samples* assembled by `DataManager`.
using DataChannel = std::variant<EquidistantChannel, TimestampedChannel,
                                 VariableChannel>;

/// Common accessor: channel index from the metablock.
[[nodiscard]] std::uint16_t channelIndex(DataChannel const& c) noexcept;

/// Common accessor: channel name.
[[nodiscard]] std::string const& channelName(DataChannel const& c) noexcept;

/// Common accessor: data type of the samples.
[[nodiscard]] DataType channelDataType(DataChannel const& c) noexcept;

/// Common accessor: physical-unit string (if set in the metablock).
[[nodiscard]] std::optional<std::string> channelPhysicalUnit(
    DataChannel const& c);

/// Common accessor: display name (if set in the metablock).
[[nodiscard]] std::optional<std::string> channelDisplayName(
    DataChannel const& c);

/// Total sample count (sum across all segments for equidistant
/// channels).
[[nodiscard]] std::size_t channelSampleCount(DataChannel const& c) noexcept;

/// Convenience predicate for `channelSampleCount == 0`.
[[nodiscard]] bool channelIsEmpty(DataChannel const& c) noexcept;

/// Read-only view of the secondary channel-definition fields.
[[nodiscard]] ChannelMeta const& channelMeta(DataChannel const& c) noexcept;

// =====================================================================
// Flat-access helpers — clone the typed vector when the active
// alternative matches.
// =====================================================================

/// Clone an `EquidistantChannel`'s samples as `std::vector<T>`.
/// Returns `DataTypeAccessMismatch` when the stored data type does
/// not match. One overload per supported numeric type plus GPS.

#define OSF_DECL_FLAT_ACCESSORS(SUFFIX, TYPE)                              \
    [[nodiscard]] Result<std::vector<TYPE>>                                \
        as##SUFFIX##Flat(EquidistantChannel const& c);                     \
    [[nodiscard]] Result<std::vector<std::pair<std::int64_t, TYPE>>>       \
        as##SUFFIX##Flat(TimestampedChannel const& c);

OSF_DECL_FLAT_ACCESSORS(Bools,   bool)
OSF_DECL_FLAT_ACCESSORS(Int8,    std::int8_t)
OSF_DECL_FLAT_ACCESSORS(Int16,   std::int16_t)
OSF_DECL_FLAT_ACCESSORS(Int32,   std::int32_t)
OSF_DECL_FLAT_ACCESSORS(Int64,   std::int64_t)
OSF_DECL_FLAT_ACCESSORS(Uint8,   std::uint8_t)
OSF_DECL_FLAT_ACCESSORS(Uint16,  std::uint16_t)
OSF_DECL_FLAT_ACCESSORS(Uint32,  std::uint32_t)
OSF_DECL_FLAT_ACCESSORS(Uint64,  std::uint64_t)
OSF_DECL_FLAT_ACCESSORS(Floats,  float)
OSF_DECL_FLAT_ACCESSORS(Doubles, double)
OSF_DECL_FLAT_ACCESSORS(Gps,     GpsLocation)

#undef OSF_DECL_FLAT_ACCESSORS

}  // namespace osf
