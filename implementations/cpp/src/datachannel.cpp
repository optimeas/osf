// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <osf/datachannel.h>

#include <sstream>
#include <utility>

namespace osf {

// =====================================================================
// NumericValues helpers
// =====================================================================

std::size_t numericValuesLen(NumericValues const& v) noexcept {
    return std::visit(
        [](auto const& vec) noexcept { return vec.size(); }, v);
}

bool numericValuesEmpty(NumericValues const& v) noexcept {
    return numericValuesLen(v) == 0;
}

DataType numericValuesDataType(NumericValues const& v) noexcept {
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

std::optional<NumericValues> numericValuesEmptyFor(DataType dt) noexcept {
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
    if (seg.sampleRateHz > 0.0 && i > 0) {
        double const offset = static_cast<double>(i) * 1.0e9 /
                              seg.sampleRateHz;
        return seg.startTimestampNs +
               static_cast<std::int64_t>(offset);
    }
    return seg.startTimestampNs;
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
// EquidistantChannel::samplesVector
// =====================================================================

std::vector<Sample<NumericValueRef>>
EquidistantChannel::samplesVector() const {
    std::vector<Sample<NumericValueRef>> out;
    out.reserve(numericValuesLen(samples));
    for (auto const& seg : segments) {
        for (std::size_t i = 0; i < seg.sampleCount; ++i) {
            Sample<NumericValueRef> s;
            s.timestampNs = segment_timestamp(seg, i);
            s.value = numeric_value_ref_at(samples, seg.startIndex + i);
            out.push_back(std::move(s));
        }
    }
    return out;
}

// =====================================================================
// TimestampedChannel::samplesVector
// =====================================================================

std::vector<Sample<NumericValueRef>>
TimestampedChannel::samplesVector() const {
    std::vector<Sample<NumericValueRef>> out;
    out.reserve(timestampsNs.size());
    for (std::size_t i = 0; i < timestampsNs.size(); ++i) {
        Sample<NumericValueRef> s;
        s.timestampNs = timestampsNs[i];
        s.value = numeric_value_ref_at(values, i);
        out.push_back(std::move(s));
    }
    return out;
}

// =====================================================================
// VariableChannel — accessors + samplesVector
// =====================================================================

Result<std::vector<std::string> const*>
VariableChannel::asStrings() const {
    if (stringValues) {
        return &*stringValues;
    }
    return tl::make_unexpected(access_mismatch(index, DataType::String,
                                              dataType));
}

Result<std::vector<std::vector<std::uint8_t>> const*>
VariableChannel::asBinaries() const {
    if (binaryValues) {
        return &*binaryValues;
    }
    return tl::make_unexpected(access_mismatch(index, DataType::Binary,
                                              dataType));
}

std::vector<Sample<VariableValueRef>>
VariableChannel::samplesVector() const {
    std::vector<Sample<VariableValueRef>> out;
    out.reserve(timestampsNs.size());
    for (std::size_t i = 0; i < timestampsNs.size(); ++i) {
        Sample<VariableValueRef> s;
        s.timestampNs = timestampsNs[i];
        if (stringValues) {
            s.value.kind = VariableValueRef::Kind::String;
            s.value.stringValue = (*stringValues)[i];
        } else if (binaryValues) {
            s.value.kind = VariableValueRef::Kind::Binary;
            s.value.binaryValue = (*binaryValues)[i];
        }
        out.push_back(std::move(s));
    }
    return out;
}

// =====================================================================
// Channel common accessors
// =====================================================================

std::uint16_t channelIndex(DataChannel const& c) noexcept {
    return std::visit([](auto const& ch) noexcept { return ch.index; }, c);
}

std::string const& channelName(DataChannel const& c) noexcept {
    return std::visit([](auto const& ch) noexcept -> std::string const& {
        return ch.name;
    }, c);
}

DataType channelDataType(DataChannel const& c) noexcept {
    return std::visit([](auto const& ch) noexcept { return ch.dataType; }, c);
}

std::optional<std::string> channelPhysicalUnit(DataChannel const& c) {
    return std::visit([](auto const& ch) -> std::optional<std::string> {
        return ch.physicalUnit;
    }, c);
}

std::optional<std::string> channelDisplayName(DataChannel const& c) {
    return std::visit([](auto const& ch) -> std::optional<std::string> {
        return ch.displayName;
    }, c);
}

std::size_t channelSampleCount(DataChannel const& c) noexcept {
    return std::visit([](auto const& ch) noexcept -> std::size_t {
        using T = std::decay_t<decltype(ch)>;
        if constexpr (std::is_same_v<T, EquidistantChannel>) {
            return numericValuesLen(ch.samples);
        } else if constexpr (std::is_same_v<T, TimestampedChannel>) {
            return numericValuesLen(ch.values);
        } else {
            if (ch.stringValues) return ch.stringValues->size();
            if (ch.binaryValues) return ch.binaryValues->size();
            return 0;
        }
    }, c);
}

bool channelIsEmpty(DataChannel const& c) noexcept {
    return channelSampleCount(c) == 0;
}

ChannelMeta const& channelMeta(DataChannel const& c) noexcept {
    return std::visit([](auto const& ch) noexcept -> ChannelMeta const& {
        return ch.channelDef;
    }, c);
}

// =====================================================================
// Flat-access helpers
// =====================================================================

#define OSF_DEFINE_FLAT_ACCESSORS(SUFFIX, TYPE, DT)                          \
    Result<std::vector<TYPE>>                                                \
        as##SUFFIX##Flat(EquidistantChannel const& c) {                      \
        if (auto const* v = std::get_if<std::vector<TYPE>>(&c.samples)) {    \
            return *v;                                                       \
        }                                                                    \
        return tl::make_unexpected(access_mismatch(                          \
            c.index, DT, numericValuesDataType(c.samples)));              \
    }                                                                        \
    Result<std::vector<std::pair<std::int64_t, TYPE>>>                       \
        as##SUFFIX##Flat(TimestampedChannel const& c) {                      \
        if (auto const* v = std::get_if<std::vector<TYPE>>(&c.values)) {     \
            std::vector<std::pair<std::int64_t, TYPE>> out;                  \
            out.reserve(v->size());                                          \
            for (std::size_t i = 0; i < v->size(); ++i) {                    \
                out.emplace_back(c.timestampsNs[i], (*v)[i]);               \
            }                                                                \
            return out;                                                      \
        }                                                                    \
        return tl::make_unexpected(access_mismatch(                          \
            c.index, DT, numericValuesDataType(c.values)));               \
    }

OSF_DEFINE_FLAT_ACCESSORS(Bools,   bool,             DataType::Bool)
OSF_DEFINE_FLAT_ACCESSORS(Int8,    std::int8_t,      DataType::Int8)
OSF_DEFINE_FLAT_ACCESSORS(Int16,   std::int16_t,     DataType::Int16)
OSF_DEFINE_FLAT_ACCESSORS(Int32,   std::int32_t,     DataType::Int32)
OSF_DEFINE_FLAT_ACCESSORS(Int64,   std::int64_t,     DataType::Int64)
OSF_DEFINE_FLAT_ACCESSORS(Uint8,   std::uint8_t,     DataType::UInt8)
OSF_DEFINE_FLAT_ACCESSORS(Uint16,  std::uint16_t,    DataType::UInt16)
OSF_DEFINE_FLAT_ACCESSORS(Uint32,  std::uint32_t,    DataType::UInt32)
OSF_DEFINE_FLAT_ACCESSORS(Uint64,  std::uint64_t,    DataType::UInt64)
OSF_DEFINE_FLAT_ACCESSORS(Floats,  float,            DataType::Float)
OSF_DEFINE_FLAT_ACCESSORS(Doubles, double,           DataType::Double)
OSF_DEFINE_FLAT_ACCESSORS(Gps,     GpsLocation,      DataType::GpsLocation)

#undef OSF_DEFINE_FLAT_ACCESSORS

}  // namespace osf
