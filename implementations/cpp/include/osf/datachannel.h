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
/// `samples_vector()` is called, using the per-segment
/// `(start_timestamp_ns, sample_rate_hz)`. Spec rev 2026-05-04 makes
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
[[nodiscard]] std::size_t numeric_values_len(NumericValues const& v) noexcept;

/// Equivalent to `numeric_values_len(v) == 0`.
[[nodiscard]] bool numeric_values_empty(NumericValues const& v) noexcept;

/// `DataType` enum matching the active alternative.
[[nodiscard]] DataType numeric_values_data_type(NumericValues const& v) noexcept;

/// Build an empty `NumericValues` whose active alternative matches
/// `dt`. Returns `std::nullopt` for variable-length types (`String`,
/// `Binary`, `ByteArray`) and for `Unsupported`.
[[nodiscard]] std::optional<NumericValues> numeric_values_empty_for(
    DataType dt) noexcept;

// =====================================================================
// Segment + ChannelMeta
// =====================================================================

/// One equidistant segment within an `EquidistantChannel`. Every
/// `bcStartData` block opens a new one with its own absolute start
/// time and sample rate.
struct Segment {
    /// Absolute start timestamp of this segment in nanoseconds.
    std::int64_t start_timestamp_ns = 0;
    /// Sample rate in Hz, valid until the next segment of this
    /// channel.
    double sample_rate_hz = 0.0;
    /// First sample of this segment in the channel's flat
    /// `NumericValues` vector.
    std::size_t start_index = 0;
    /// Number of samples belonging to this segment.
    std::size_t sample_count = 0;
};

/// Secondary channel-definition fields preserved from the metablock
/// so downstream code can introspect them without keeping a
/// back-reference to the `MetaBlock`.
struct ChannelMeta {
    /// Original on-disk channel-type spelling (`scalar`,
    /// `equidistant`, `timestamped`).
    ChannelType channel_type = ChannelType::Scalar;
    /// Length-prefix width on disk (2 or 4 bytes per spec).
    std::uint8_t size_of_length_value = 0;
    /// Sample period in nanoseconds, if the metablock declared one.
    std::optional<std::int64_t> time_increment_ns;
    /// Free-form reference identifier.
    std::optional<std::string> reference;
    /// Physical dimension (e.g. `temperature`).
    std::optional<std::string> physical_dimension;
    /// Free-form comment.
    std::optional<std::string> comment;
    /// Spectrum subtype, if the channel carries spectral data.
    std::optional<SpectrumType> spectrum_type;
};

// =====================================================================
// Sample iteration types — value classes returned by samples_vector.
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
    std::string string_value;
    std::vector<std::uint8_t> binary_value;
};

/// Generic `(timestamp_ns, value)` pair.
template <typename T>
struct Sample {
    std::int64_t timestamp_ns = 0;
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
    DataType data_type = DataType::Unsupported;
    /// Optional physical unit string (e.g. `°C`).
    std::optional<std::string> physical_unit;
    /// Optional display name.
    std::optional<std::string> display_name;
    /// Secondary channel-definition fields preserved from the
    /// metablock.
    ChannelMeta channel_def;

    /// Flat sample storage: every segment's samples appended
    /// head-to-tail.
    NumericValues samples{std::vector<double>{}};
    /// One entry per `bcStartData`; `start_index..start_index +
    /// sample_count` indexes into `samples`.
    std::vector<Segment> segments;

    /// Reconstruct `(timestamp, value)` pairs for every sample. Per
    /// the spec, sample `i` within a segment lands at
    /// `segment.start + i * (1e9 / sample_rate_hz)`. Time gaps
    /// between consecutive segments are NOT interpolated.
    [[nodiscard]] std::vector<Sample<NumericValueRef>> samples_vector() const;
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
    DataType data_type = DataType::Unsupported;
    /// Optional physical unit string.
    std::optional<std::string> physical_unit;
    /// Optional display name.
    std::optional<std::string> display_name;
    /// Secondary channel-definition fields preserved from the
    /// metablock.
    ChannelMeta channel_def;

    /// Absolute timestamps in nanoseconds since the Unix epoch (UTC),
    /// in stream order — same length as `values`.
    std::vector<std::int64_t> timestamps_ns;
    /// Sample values, parallel to `timestamps_ns`.
    NumericValues values{std::vector<double>{}};

    /// Pair every timestamp with its sample value.
    [[nodiscard]] std::vector<Sample<NumericValueRef>> samples_vector() const;
};

// =====================================================================
// VariableChannel — string and binary samples.
// =====================================================================

/// Timestamped channel for string and binary data. Variable-length
/// payloads land in either `string_values` (when `data_type` is
/// `String`) or `binary_values` (when `data_type` is `Binary`); the
/// other field stays `std::nullopt`.
struct VariableChannel {
    /// Channel index from the metablock.
    std::uint16_t index = 0;
    /// Fully qualified channel name.
    std::string name;
    /// Datatype — exactly one of `DataType::String` or
    /// `DataType::Binary`.
    DataType data_type = DataType::String;
    /// Optional physical unit string (rare for string / binary).
    std::optional<std::string> physical_unit;
    /// Optional display name.
    std::optional<std::string> display_name;
    /// MIME type for binary channels (e.g. `image/jpeg`).
    std::optional<std::string> mime_type;
    /// Secondary channel-definition fields preserved from the
    /// metablock.
    ChannelMeta channel_def;

    /// Absolute timestamps in nanoseconds since the Unix epoch (UTC).
    std::vector<std::int64_t> timestamps_ns;
    /// String samples; set for `data_type == String`.
    std::optional<std::vector<std::string>> string_values;
    /// Binary samples; set for `data_type == Binary`.
    std::optional<std::vector<std::vector<std::uint8_t>>> binary_values;

    /// Read the channel's string samples.
    ///
    /// \returns `DataTypeAccessMismatch` when the channel holds
    /// binary data instead.
    [[nodiscard]] Result<std::vector<std::string> const*> as_strings() const;

    /// Read the channel's binary samples.
    ///
    /// \returns `DataTypeAccessMismatch` when the channel holds
    /// string data instead.
    [[nodiscard]] Result<std::vector<std::vector<std::uint8_t>> const*>
        as_binaries() const;

    /// Pair every timestamp with its value (string or binary).
    [[nodiscard]] std::vector<Sample<VariableValueRef>> samples_vector() const;
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
[[nodiscard]] std::uint16_t channel_index(DataChannel const& c) noexcept;

/// Common accessor: channel name.
[[nodiscard]] std::string const& channel_name(DataChannel const& c) noexcept;

/// Common accessor: data type of the samples.
[[nodiscard]] DataType channel_data_type(DataChannel const& c) noexcept;

/// Common accessor: physical-unit string (if set in the metablock).
[[nodiscard]] std::optional<std::string> channel_physical_unit(
    DataChannel const& c);

/// Common accessor: display name (if set in the metablock).
[[nodiscard]] std::optional<std::string> channel_display_name(
    DataChannel const& c);

/// Total sample count (sum across all segments for equidistant
/// channels).
[[nodiscard]] std::size_t channel_sample_count(DataChannel const& c) noexcept;

/// Convenience predicate for `channel_sample_count == 0`.
[[nodiscard]] bool channel_is_empty(DataChannel const& c) noexcept;

/// Read-only view of the secondary channel-definition fields.
[[nodiscard]] ChannelMeta const& channel_meta(DataChannel const& c) noexcept;

// =====================================================================
// Flat-access helpers — clone the typed vector when the active
// alternative matches.
// =====================================================================

/// Clone an `EquidistantChannel`'s samples as `std::vector<T>`.
/// Returns `DataTypeAccessMismatch` when the stored data type does
/// not match. One overload per supported numeric type plus GPS.

#define OSF_DECL_FLAT_ACCESSORS(SUFFIX, TYPE)                              \
    [[nodiscard]] Result<std::vector<TYPE>>                                \
        as_##SUFFIX##_flat(EquidistantChannel const& c);                   \
    [[nodiscard]] Result<std::vector<std::pair<std::int64_t, TYPE>>>       \
        as_##SUFFIX##_flat(TimestampedChannel const& c);

OSF_DECL_FLAT_ACCESSORS(bools,   bool)
OSF_DECL_FLAT_ACCESSORS(int8,    std::int8_t)
OSF_DECL_FLAT_ACCESSORS(int16,   std::int16_t)
OSF_DECL_FLAT_ACCESSORS(int32,   std::int32_t)
OSF_DECL_FLAT_ACCESSORS(int64,   std::int64_t)
OSF_DECL_FLAT_ACCESSORS(uint8,   std::uint8_t)
OSF_DECL_FLAT_ACCESSORS(uint16,  std::uint16_t)
OSF_DECL_FLAT_ACCESSORS(uint32,  std::uint32_t)
OSF_DECL_FLAT_ACCESSORS(uint64,  std::uint64_t)
OSF_DECL_FLAT_ACCESSORS(floats,  float)
OSF_DECL_FLAT_ACCESSORS(doubles, double)
OSF_DECL_FLAT_ACCESSORS(gps,     GpsLocation)

#undef OSF_DECL_FLAT_ACCESSORS

}  // namespace osf
