// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <osf/datachannel.h>

#include <sstream>
#include <utility>

namespace osf {

// =====================================================================
// NumericValues helpers
// =====================================================================

std::size_t numeric_values_len(NumericValues const& v) noexcept {
    return std::visit(
        [](auto const& vec) noexcept { return vec.size(); }, v);
}

bool numeric_values_empty(NumericValues const& v) noexcept {
    return numeric_values_len(v) == 0;
}

DataType numeric_values_data_type(NumericValues const& v) noexcept {
    return std::visit([](auto const& vec) noexcept -> DataType {
        using Vec = std::decay_t<decltype(vec)>;
        if constexpr (std::is_same_v<Vec, std::vector<bool>>)
            return DataType::Bool;
        else if constexpr (std::is_same_v<Vec, std::vector<std::int8_t>>)
            return DataType::Int8;
        else if constexpr (std::is_same_v<Vec, std::vector<std::int16_t>>)
            return DataType::Int16;
        else if constexpr (std::is_same_v<Vec, std::vector<std::int32_t>>)
            return DataType::Int32;
        else if constexpr (std::is_same_v<Vec, std::vector<std::int64_t>>)
            return DataType::Int64;
        else if constexpr (std::is_same_v<Vec, std::vector<std::uint8_t>>)
            return DataType::UInt8;
        else if constexpr (std::is_same_v<Vec, std::vector<std::uint16_t>>)
            return DataType::UInt16;
        else if constexpr (std::is_same_v<Vec, std::vector<std::uint32_t>>)
            return DataType::UInt32;
        else if constexpr (std::is_same_v<Vec, std::vector<std::uint64_t>>)
            return DataType::UInt64;
        else if constexpr (std::is_same_v<Vec, std::vector<float>>)
            return DataType::Float;
        else if constexpr (std::is_same_v<Vec, std::vector<double>>)
            return DataType::Double;
        else
            return DataType::GpsLocation;
    }, v);
}

std::optional<NumericValues> numeric_values_empty_for(DataType dt) noexcept {
    switch (dt) {
        case DataType::Bool:        return NumericValues{std::vector<bool>{}};
        case DataType::Int8:        return NumericValues{std::vector<std::int8_t>{}};
        case DataType::Int16:       return NumericValues{std::vector<std::int16_t>{}};
        case DataType::Int32:       return NumericValues{std::vector<std::int32_t>{}};
        case DataType::Int64:       return NumericValues{std::vector<std::int64_t>{}};
        case DataType::UInt8:       return NumericValues{std::vector<std::uint8_t>{}};
        case DataType::UInt16:      return NumericValues{std::vector<std::uint16_t>{}};
        case DataType::UInt32:      return NumericValues{std::vector<std::uint32_t>{}};
        case DataType::UInt64:      return NumericValues{std::vector<std::uint64_t>{}};
        case DataType::Float:       return NumericValues{std::vector<float>{}};
        case DataType::Double:      return NumericValues{std::vector<double>{}};
        case DataType::GpsLocation: return NumericValues{std::vector<GpsLocation>{}};
        case DataType::String:
        case DataType::Binary:
        case DataType::ByteArray:
        case DataType::Unsupported:
            return std::nullopt;
    }
    return std::nullopt;
}

// =====================================================================
// Sample-projection helpers (file-local)
// =====================================================================

namespace {

NumericValueRef numeric_value_ref_at(NumericValues const& v, std::size_t idx) {
    return std::visit([idx](auto const& vec) -> NumericValueRef {
        return NumericValueRef{vec[idx]};
    }, v);
}

// Compute the timestamp of sample `i` within a segment given its
// start time and sample rate. Returns the segment start when the
// rate is non-positive (defensive — would only happen on a
// malformed file).
std::int64_t segment_timestamp(Segment const& seg, std::size_t i) noexcept {
    if (seg.sample_rate_hz > 0.0 && i > 0) {
        double const offset = static_cast<double>(i) * 1.0e9 /
                              seg.sample_rate_hz;
        return seg.start_timestamp_ns +
               static_cast<std::int64_t>(offset);
    }
    return seg.start_timestamp_ns;
}

Error access_mismatch(std::uint16_t channel, DataType requested,
                      DataType actual) {
    std::ostringstream oss;
    oss << "channel " << channel
        << " flat-access mismatch: requested " << static_cast<int>(requested)
        << ", channel holds "                   << static_cast<int>(actual);
    return Error{Error::Code::DataTypeMismatch, oss.str()};
}

}  // anonymous namespace

// =====================================================================
// EquidistantChannel::samples_vector
// =====================================================================

std::vector<Sample<NumericValueRef>>
EquidistantChannel::samples_vector() const {
    std::vector<Sample<NumericValueRef>> out;
    out.reserve(numeric_values_len(samples));
    for (auto const& seg : segments) {
        for (std::size_t i = 0; i < seg.sample_count; ++i) {
            Sample<NumericValueRef> s;
            s.timestamp_ns = segment_timestamp(seg, i);
            s.value = numeric_value_ref_at(samples, seg.start_index + i);
            out.push_back(std::move(s));
        }
    }
    return out;
}

// =====================================================================
// TimestampedChannel::samples_vector
// =====================================================================

std::vector<Sample<NumericValueRef>>
TimestampedChannel::samples_vector() const {
    std::vector<Sample<NumericValueRef>> out;
    out.reserve(timestamps_ns.size());
    for (std::size_t i = 0; i < timestamps_ns.size(); ++i) {
        Sample<NumericValueRef> s;
        s.timestamp_ns = timestamps_ns[i];
        s.value = numeric_value_ref_at(values, i);
        out.push_back(std::move(s));
    }
    return out;
}

// =====================================================================
// VariableChannel — accessors + samples_vector
// =====================================================================

Result<std::vector<std::string> const*>
VariableChannel::as_strings() const {
    if (string_values) {
        return &*string_values;
    }
    return tl::make_unexpected(access_mismatch(index, DataType::String,
                                              data_type));
}

Result<std::vector<std::vector<std::uint8_t>> const*>
VariableChannel::as_binaries() const {
    if (binary_values) {
        return &*binary_values;
    }
    return tl::make_unexpected(access_mismatch(index, DataType::Binary,
                                              data_type));
}

std::vector<Sample<VariableValueRef>>
VariableChannel::samples_vector() const {
    std::vector<Sample<VariableValueRef>> out;
    out.reserve(timestamps_ns.size());
    for (std::size_t i = 0; i < timestamps_ns.size(); ++i) {
        Sample<VariableValueRef> s;
        s.timestamp_ns = timestamps_ns[i];
        if (string_values) {
            s.value.kind = VariableValueRef::Kind::String;
            s.value.string_value = (*string_values)[i];
        } else if (binary_values) {
            s.value.kind = VariableValueRef::Kind::Binary;
            s.value.binary_value = (*binary_values)[i];
        }
        out.push_back(std::move(s));
    }
    return out;
}

// =====================================================================
// Channel common accessors
// =====================================================================

std::uint16_t channel_index(DataChannel const& c) noexcept {
    return std::visit([](auto const& ch) noexcept { return ch.index; }, c);
}

std::string const& channel_name(DataChannel const& c) noexcept {
    return std::visit([](auto const& ch) noexcept -> std::string const& {
        return ch.name;
    }, c);
}

DataType channel_data_type(DataChannel const& c) noexcept {
    return std::visit([](auto const& ch) noexcept { return ch.data_type; }, c);
}

std::optional<std::string> channel_physical_unit(DataChannel const& c) {
    return std::visit([](auto const& ch) -> std::optional<std::string> {
        return ch.physical_unit;
    }, c);
}

std::optional<std::string> channel_display_name(DataChannel const& c) {
    return std::visit([](auto const& ch) -> std::optional<std::string> {
        return ch.display_name;
    }, c);
}

std::size_t channel_sample_count(DataChannel const& c) noexcept {
    return std::visit([](auto const& ch) noexcept -> std::size_t {
        using T = std::decay_t<decltype(ch)>;
        if constexpr (std::is_same_v<T, EquidistantChannel>) {
            return numeric_values_len(ch.samples);
        } else if constexpr (std::is_same_v<T, TimestampedChannel>) {
            return numeric_values_len(ch.values);
        } else {
            if (ch.string_values) return ch.string_values->size();
            if (ch.binary_values) return ch.binary_values->size();
            return 0;
        }
    }, c);
}

bool channel_is_empty(DataChannel const& c) noexcept {
    return channel_sample_count(c) == 0;
}

ChannelMeta const& channel_meta(DataChannel const& c) noexcept {
    return std::visit([](auto const& ch) noexcept -> ChannelMeta const& {
        return ch.channel_def;
    }, c);
}

// =====================================================================
// Flat-access helpers
// =====================================================================

#define OSF_DEFINE_FLAT_ACCESSORS(SUFFIX, TYPE, DT)                          \
    Result<std::vector<TYPE>>                                                \
        as_##SUFFIX##_flat(EquidistantChannel const& c) {                    \
        if (auto const* v = std::get_if<std::vector<TYPE>>(&c.samples)) {    \
            return *v;                                                       \
        }                                                                    \
        return tl::make_unexpected(access_mismatch(                          \
            c.index, DT, numeric_values_data_type(c.samples)));              \
    }                                                                        \
    Result<std::vector<std::pair<std::int64_t, TYPE>>>                       \
        as_##SUFFIX##_flat(TimestampedChannel const& c) {                    \
        if (auto const* v = std::get_if<std::vector<TYPE>>(&c.values)) {     \
            std::vector<std::pair<std::int64_t, TYPE>> out;                  \
            out.reserve(v->size());                                          \
            for (std::size_t i = 0; i < v->size(); ++i) {                    \
                out.emplace_back(c.timestamps_ns[i], (*v)[i]);               \
            }                                                                \
            return out;                                                      \
        }                                                                    \
        return tl::make_unexpected(access_mismatch(                          \
            c.index, DT, numeric_values_data_type(c.values)));               \
    }

OSF_DEFINE_FLAT_ACCESSORS(bools,   bool,             DataType::Bool)
OSF_DEFINE_FLAT_ACCESSORS(int8,    std::int8_t,      DataType::Int8)
OSF_DEFINE_FLAT_ACCESSORS(int16,   std::int16_t,     DataType::Int16)
OSF_DEFINE_FLAT_ACCESSORS(int32,   std::int32_t,     DataType::Int32)
OSF_DEFINE_FLAT_ACCESSORS(int64,   std::int64_t,     DataType::Int64)
OSF_DEFINE_FLAT_ACCESSORS(uint8,   std::uint8_t,     DataType::UInt8)
OSF_DEFINE_FLAT_ACCESSORS(uint16,  std::uint16_t,    DataType::UInt16)
OSF_DEFINE_FLAT_ACCESSORS(uint32,  std::uint32_t,    DataType::UInt32)
OSF_DEFINE_FLAT_ACCESSORS(uint64,  std::uint64_t,    DataType::UInt64)
OSF_DEFINE_FLAT_ACCESSORS(floats,  float,            DataType::Float)
OSF_DEFINE_FLAT_ACCESSORS(doubles, double,           DataType::Double)
OSF_DEFINE_FLAT_ACCESSORS(gps,     GpsLocation,      DataType::GpsLocation)

#undef OSF_DEFINE_FLAT_ACCESSORS

}  // namespace osf
