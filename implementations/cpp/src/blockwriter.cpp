// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/blockwriter.h"

#include "blockencode_p.h"           // osf::detail::encode_start_data, encode_continued_data
#include "writercommon_p.h"          // osf::detail chunking helpers + FileInfoDraft + build_metablock
#include "osf/datachannel.h"       // NumericValues, numericValuesLen
#include "osf/manager.h"            // DataManager (fromManager)
#include "osf/metablock.h"          // serializeMetablockJson

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <type_traits>
#include <utility>

namespace osf {

// ── ChannelData — per-channel accumulated samples ─────────────────────

struct BlockWriter::ChannelData {
    enum class Kind { Empty, Equidistant, Timestamped, Variable } kind = Kind::Empty;
    DataType datatype_lock = DataType::Unsupported;

    struct EqSegment {
        std::int64_t  startTimestampNs = 0;
        double        sampleRateHz = 0.0;
        NumericValues values;   // Float or Double only for equidistant
    };
    std::vector<EqSegment> eq_segments;

    // Timestamped numeric + GPS storage (ts_ns + ts_values) and variable
    // string/binary storage. Only the fields for this channel's locked
    // Kind are populated.
    std::vector<std::int64_t> ts_ns;
    NumericValues             ts_values;
    std::vector<std::string>  strings;
    std::vector<std::vector<std::uint8_t>> binaries;
};

// ── Special members (defined out-of-line so vector<ChannelData> can
//    instantiate against the now-complete ChannelData type) ────────────

BlockWriter::BlockWriter()                                     = default;
BlockWriter::~BlockWriter()                                    = default;
BlockWriter::BlockWriter(BlockWriter const&)                   = default;
BlockWriter& BlockWriter::operator=(BlockWriter const&)        = default;
BlockWriter::BlockWriter(BlockWriter&&) noexcept               = default;
BlockWriter& BlockWriter::operator=(BlockWriter&&) noexcept    = default;

// ── Internal helpers ─────────────────────────────────────────────────

namespace {

Error make_error(Error::Code code, std::string msg) {
    return Error{code, std::move(msg)};
}

// Map a supported template T to its DataType enum. Compile-time dispatch.
template <typename T>
constexpr DataType data_type_for() noexcept {
    if constexpr (std::is_same_v<T, bool>)               return DataType::Bool;
    else if constexpr (std::is_same_v<T, std::int8_t>)   return DataType::Int8;
    else if constexpr (std::is_same_v<T, std::int16_t>)  return DataType::Int16;
    else if constexpr (std::is_same_v<T, std::int32_t>)  return DataType::Int32;
    else if constexpr (std::is_same_v<T, std::int64_t>)  return DataType::Int64;
    else if constexpr (std::is_same_v<T, std::uint8_t>)  return DataType::UInt8;
    else if constexpr (std::is_same_v<T, std::uint16_t>) return DataType::UInt16;
    else if constexpr (std::is_same_v<T, std::uint32_t>) return DataType::UInt32;
    else if constexpr (std::is_same_v<T, std::uint64_t>) return DataType::UInt64;
    else if constexpr (std::is_same_v<T, float>)         return DataType::Float;
    else if constexpr (std::is_same_v<T, double>)        return DataType::Double;
    else { static_assert(sizeof(T) == 0, "unsupported T"); return DataType::Unsupported; }
}

// Returns the per-sample byte size for the active NumericValues alternative.
// Returns sizeof(T) for all numeric alternatives and GpsLocation (== 24).
std::size_t numeric_value_size(NumericValues const& v) noexcept {
    return std::visit([](auto const& vec) -> std::size_t {
        using T = typename std::decay_t<decltype(vec)>::value_type;
        return sizeof(T);
    }, v);
}

// Dispatch encode_start_data<T> from a NumericValues slice [offset, offset+count).
Result<void> encode_start_from_values(
        std::vector<std::uint8_t>& buf,
        std::uint16_t ci, std::uint8_t sov,
        std::int64_t start_ts, double rate,
        NumericValues const& v, std::size_t offset, std::size_t count) {
    if (auto const* fv = std::get_if<std::vector<float>>(&v)) {
        return osf::detail::encode_start_data<float>(
            buf, ci, sov, start_ts, rate, fv->data() + offset, count);
    } else if (auto const* dv = std::get_if<std::vector<double>>(&v)) {
        return osf::detail::encode_start_data<double>(
            buf, ci, sov, start_ts, rate, dv->data() + offset, count);
    }
    // Defensive: equidistant channels only store float/double
    return tl::make_unexpected(
        Error{Error::Code::InvalidBlock, "encode_start_from_values: non-float/double NumericValues"});
}

// Dispatch encode_continued_data<T> from a NumericValues slice [offset, offset+count).
Result<void> encode_continued_from_values(
        std::vector<std::uint8_t>& buf,
        std::uint16_t ci, std::uint8_t sov,
        NumericValues const& v, std::size_t offset, std::size_t count) {
    if (auto const* fv = std::get_if<std::vector<float>>(&v)) {
        return osf::detail::encode_continued_data<float>(
            buf, ci, sov, fv->data() + offset, count);
    } else if (auto const* dv = std::get_if<std::vector<double>>(&v)) {
        return osf::detail::encode_continued_data<double>(
            buf, ci, sov, dv->data() + offset, count);
    }
    // Defensive: equidistant channels only store float/double
    return tl::make_unexpected(
        Error{Error::Code::InvalidBlock, "encode_continued_from_values: non-float/double NumericValues"});
}

// Append T values into the correct NumericValues alternative — creating it
// if still default-constructed (holds std::vector<double> by default).
//
// Precondition: the caller (addTimestampedSamplesImpl) has already
// verified datatype_lock == data_type_for<T>(), so the variant either
// is empty/default (first append → create) or already holds vector<T>.
//
// Note: std::vector<bool> does not support pointer-range insert from
// `bool const*` directly in all implementations; we push_back element by
// element for that case.
template <typename T>
void append_timestamped_values(NumericValues& nv, T const* values, std::size_t count) {
    if (auto* vec = std::get_if<std::vector<T>>(&nv)) {
        if constexpr (std::is_same_v<T, bool>) {
            vec->reserve(vec->size() + count);
            for (std::size_t i = 0; i < count; ++i) vec->push_back(values[i]);
        } else {
            vec->insert(vec->end(), values, values + count);
        }
    } else {
        // Replace default (or empty-wrong-type) with the correct alternative.
        if constexpr (std::is_same_v<T, bool>) {
            std::vector<bool> tmp;
            tmp.reserve(count);
            for (std::size_t i = 0; i < count; ++i) tmp.push_back(values[i]);
            nv = std::move(tmp);
        } else {
            nv = std::vector<T>(values, values + count);
        }
    }
}

// Append GpsLocation values into the GpsLocation alternative of NumericValues.
// Switches from default (vector<double>) to vector<GpsLocation> on first call.
void append_gps_values(NumericValues& nv, GpsLocation const* values, std::size_t count) {
    if (auto* vec = std::get_if<std::vector<GpsLocation>>(&nv)) {
        vec->insert(vec->end(), values, values + count);
    } else {
        // Replace default or wrong-alternative with vector<GpsLocation>.
        nv = std::vector<GpsLocation>(values, values + count);
    }
}

// Dispatch encode_abs_timestamp_data<T> from a NumericValues slice
// [offset, offset+count).
//
// Special case: std::vector<bool> has no .data() member (proxy-reference
// specialisation). We materialise a genuine bool[] so the glvalue type
// matches the object type — no strict-aliasing violation.
Result<void> encode_abs_ts_from_values(
        std::vector<std::uint8_t>& buf,
        std::uint16_t ci, std::uint8_t sov,
        std::int64_t const* ts,
        NumericValues const& v, std::size_t offset, std::size_t count) {
    // Handle std::vector<bool> before the generic visit (no .data()).
    if (auto const* bv = std::get_if<std::vector<bool>>(&v)) {
        // std::vector<bool> has no .data() (proxy-reference specialisation);
        // materialise a genuine bool[] so encode_abs_timestamp_data<bool>
        // reads bool objects (no strict-aliasing violation).
        std::unique_ptr<bool[]> tmp(new bool[count]);
        for (std::size_t i = 0; i < count; ++i)
            tmp[i] = (*bv)[offset + i];
        return osf::detail::encode_abs_timestamp_data<bool>(
            buf, ci, sov, ts, tmp.get(), count);
    }
    return std::visit([&](auto const& vec) -> Result<void> {
        using T = typename std::decay_t<decltype(vec)>::value_type;
        if constexpr (std::is_same_v<T, GpsLocation>) {
            return osf::detail::encode_abs_timestamp_data_gps(
                buf, ci, sov, ts, vec.data() + offset, count);
        } else if constexpr (std::is_same_v<T, bool>) {
            // Handled above; this branch is unreachable but needed for
            // the constexpr-else chain.
            return tl::make_unexpected(
                Error{Error::Code::InvalidBlock,
                      "encode_abs_ts_from_values: bool fallthrough (unreachable)"});
        } else {
            return osf::detail::encode_abs_timestamp_data<T>(
                buf, ci, sov, ts, vec.data() + offset, count);
        }
    }, v);
}

}  // namespace

// ── File-info setters ────────────────────────────────────────────────

void BlockWriter::setCreator(std::string v)       { m_fileInfo.creator       = std::move(v); }
void BlockWriter::setTag(std::string v)           { m_fileInfo.tag           = std::move(v); }
void BlockWriter::setReason(std::string v)        { m_fileInfo.reason        = std::move(v); }
void BlockWriter::setNamespaceSep(std::string v) { m_fileInfo.namespaceSep = std::move(v); }
void BlockWriter::setComment(std::string v)       { m_fileInfo.comment       = std::move(v); }

void BlockWriter::setLocation(double lat, double lon, double alt) {
    m_fileInfo.createdAtLatitude  = lat;
    m_fileInfo.createdAtLongitude = lon;
    m_fileInfo.createdAtAltitude  = alt;
}

// ── addChannel ──────────────────────────────────────────────────────

Result<std::uint16_t> BlockWriter::addChannel(ChannelDef def) {
    if (def.sizeOfLengthValue != 2 && def.sizeOfLengthValue != 4) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addChannel: sizeOfLengthValue must be 2 or 4"));
    }
    if (def.dataType == DataType::Unsupported) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addChannel: dataType Unsupported is not writeable"));
    }
    if (def.channelType == ChannelType::Unsupported) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addChannel: channelType Unsupported is not writeable"));
    }
    if (m_channels.size() >= 0xFFFF) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addChannel: too many channels (max 65535)"));
    }

    auto const idx = static_cast<std::uint16_t>(m_channels.size());
    ChannelData cd;
    cd.datatype_lock = def.dataType;
    m_nameToIndex.emplace(def.name, idx);
    m_channels.push_back(std::move(def));
    m_channelData.push_back(std::move(cd));
    return idx;
}

// ── channelCount / channelIndex ───────────────────────────────────

std::size_t BlockWriter::channelCount() const noexcept {
    return m_channels.size();
}

std::optional<std::uint16_t>
BlockWriter::channelIndex(std::string_view name) const {
    auto it = m_nameToIndex.find(std::string{name});
    if (it == m_nameToIndex.end()) return std::nullopt;
    return it->second;
}

// ── addEquidistantSegmentImpl<T> ─────────────────────────────────

template <typename T>
Result<void> BlockWriter::addEquidistantSegmentImpl(
        std::uint16_t channel, std::int64_t startTsNs, double rateHz,
        T const* samples, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addEquidistantSegment: count must be > 0"));
    }
    if (!(rateHz > 0.0) || !std::isfinite(rateHz)) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addEquidistantSegment: sampleRateHz must be a "
            "positive finite double"));
    }
    if (channel >= m_channels.size()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addEquidistantSegment: channel index out of range"));
    }

    auto& cd = m_channelData[channel];

    // Kind-lock: once a channel has an equidistant segment it stays equidistant.
    if (cd.kind != ChannelData::Kind::Empty &&
        cd.kind != ChannelData::Kind::Equidistant) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidBlock,
            "channel " + std::to_string(channel) + ": mixed block types"));
    }

    // Datatype must match what was declared at addChannel time AND be
    // Float/Double. The Float/Double restriction is a compile-time
    // invariant: this template is only instantiated for float/double (the
    // public addEquidistantSegment overloads), so the runtime branch
    // would be dead code — a static_assert documents it without tripping
    // MSVC C4127 (constant conditional) under /WX.
    constexpr DataType expected = data_type_for<T>();
    static_assert(expected == DataType::Float || expected == DataType::Double,
                  "addEquidistantSegmentImpl is only valid for "
                  "Float and Double");
    if (cd.datatype_lock != expected) {
        return tl::make_unexpected(make_error(
            Error::Code::DataTypeMismatch,
            "channel " + std::to_string(channel) + ": datatype mismatch"));
    }

    cd.kind = ChannelData::Kind::Equidistant;
    ChannelData::EqSegment seg;
    seg.startTimestampNs = startTsNs;
    seg.sampleRateHz     = rateHz;
    seg.values             = NumericValues{std::vector<T>(samples, samples + count)};
    cd.eq_segments.push_back(std::move(seg));
    return {};
}

// ── addTimestampedSamplesImpl<T> ─────────────────────────────────

template <typename T>
Result<void> BlockWriter::addTimestampedSamplesImpl(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        T const* values, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addTimestampedSamples: count must be > 0"));
    }
    if (channel >= m_channels.size()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addTimestampedSamples: channel index out of range"));
    }

    auto& cd = m_channelData[channel];

    // Kind-lock: once a channel has timestamped data it stays timestamped.
    if (cd.kind != ChannelData::Kind::Empty &&
        cd.kind != ChannelData::Kind::Timestamped) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidBlock,
            "channel " + std::to_string(channel) + ": mixed block types"));
    }

    // Datatype must match what was declared at addChannel time.
    constexpr DataType expected = data_type_for<T>();
    if (cd.datatype_lock != expected) {
        return tl::make_unexpected(make_error(
            Error::Code::DataTypeMismatch,
            "channel " + std::to_string(channel) + ": datatype mismatch"));
    }

    cd.kind = ChannelData::Kind::Timestamped;
    cd.ts_ns.insert(cd.ts_ns.end(), timestampsNs, timestampsNs + count);
    append_timestamped_values<T>(cd.ts_values, values, count);
    return {};
}

// ── Public addEquidistantSegment overloads ─────────────────────────

Result<void> BlockWriter::addEquidistantSegment(
        std::uint16_t channel, std::int64_t startTsNs, double rateHz,
        float const* samples, std::size_t count) {
    return addEquidistantSegmentImpl<float>(channel, startTsNs, rateHz, samples, count);
}

Result<void> BlockWriter::addEquidistantSegment(
        std::uint16_t channel, std::int64_t startTsNs, double rateHz,
        double const* samples, std::size_t count) {
    return addEquidistantSegmentImpl<double>(channel, startTsNs, rateHz, samples, count);
}

// ── addTimestampedGpsSamples / _sample ───────────────────────────

Result<void> BlockWriter::addTimestampedGpsSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        GpsLocation const* values, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addTimestampedGpsSamples: count must be > 0"));
    }
    if (channel >= m_channels.size()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addTimestampedGpsSamples: channel index out of range"));
    }

    auto& cd = m_channelData[channel];

    // Kind-lock: once a channel has timestamped data it stays timestamped.
    if (cd.kind != ChannelData::Kind::Empty &&
        cd.kind != ChannelData::Kind::Timestamped) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidBlock,
            "channel " + std::to_string(channel) + ": mixed block types"));
    }
    // Datatype must be GpsLocation.
    if (cd.datatype_lock != DataType::GpsLocation) {
        return tl::make_unexpected(make_error(
            Error::Code::DataTypeMismatch,
            "channel " + std::to_string(channel) + ": datatype mismatch"));
    }

    cd.kind = ChannelData::Kind::Timestamped;
    cd.ts_ns.insert(cd.ts_ns.end(), timestampsNs, timestampsNs + count);
    append_gps_values(cd.ts_values, values, count);
    return {};
}

Result<void> BlockWriter::addTimestampedGpsSample(
        std::uint16_t channel, std::int64_t timestampNs, GpsLocation value) {
    return addTimestampedGpsSamples(channel, &timestampNs, &value, 1);
}

// ── addStringSamples / _sample ────────────────────────────────────

Result<void> BlockWriter::addStringSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        std::string_view const* values, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addStringSamples: count must be > 0"));
    }
    if (channel >= m_channels.size()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addStringSamples: channel index out of range"));
    }

    auto& cd = m_channelData[channel];

    // Kind-lock: once a channel has variable data it stays variable.
    if (cd.kind != ChannelData::Kind::Empty &&
        cd.kind != ChannelData::Kind::Variable) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidBlock,
            "channel " + std::to_string(channel) + ": mixed block types"));
    }
    // Datatype must be String.
    if (cd.datatype_lock != DataType::String) {
        return tl::make_unexpected(make_error(
            Error::Code::DataTypeMismatch,
            "channel " + std::to_string(channel) + ": datatype mismatch"));
    }

    cd.kind = ChannelData::Kind::Variable;
    cd.ts_ns.insert(cd.ts_ns.end(), timestampsNs, timestampsNs + count);
    for (std::size_t i = 0; i < count; ++i) {
        cd.strings.emplace_back(values[i]);
    }
    return {};
}

Result<void> BlockWriter::addStringSample(
        std::uint16_t channel, std::int64_t timestampNs, std::string_view value) {
    return addStringSamples(channel, &timestampNs, &value, 1);
}

// ── addBinarySamples / _sample ────────────────────────────────────

Result<void> BlockWriter::addBinarySamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        BinarySample const* values, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addBinarySamples: count must be > 0"));
    }
    if (channel >= m_channels.size()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addBinarySamples: channel index out of range"));
    }

    auto& cd = m_channelData[channel];

    // Kind-lock: once a channel has variable data it stays variable.
    if (cd.kind != ChannelData::Kind::Empty &&
        cd.kind != ChannelData::Kind::Variable) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidBlock,
            "channel " + std::to_string(channel) + ": mixed block types"));
    }
    // Datatype must be Binary.
    if (cd.datatype_lock != DataType::Binary) {
        return tl::make_unexpected(make_error(
            Error::Code::DataTypeMismatch,
            "channel " + std::to_string(channel) + ": datatype mismatch"));
    }

    cd.kind = ChannelData::Kind::Variable;
    cd.ts_ns.insert(cd.ts_ns.end(), timestampsNs, timestampsNs + count);
    for (std::size_t i = 0; i < count; ++i) {
        cd.binaries.emplace_back(values[i].data, values[i].data + values[i].size);
    }
    return {};
}

Result<void> BlockWriter::addBinarySample(
        std::uint16_t channel, std::int64_t timestampNs, BinarySample value) {
    return addBinarySamples(channel, &timestampNs, &value, 1);
}

// ── autobumpSizeOfLengthValue ───────────────────────────────────

void BlockWriter::autobumpSizeOfLengthValue(std::vector<ChannelDef>& defs) const {
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].sizeOfLengthValue == 4) continue;
        ChannelData const& cd = m_channelData[i];
        if (cd.kind != ChannelData::Kind::Variable) continue;
        std::size_t max_sample = 0;
        for (auto const& s : cd.strings)  max_sample = std::max(max_sample, s.size());
        for (auto const& b : cd.binaries) max_sample = std::max(max_sample, b.size());
        if (osf::detail::VARIABLE_BLOCK_OVERHEAD_BYTES + max_sample
                > osf::detail::max_payload_for_sov(2)) {
            defs[i].sizeOfLengthValue = 4;
        }
    }
}

// ── writeBlockBytes ────────────────────────────────────────────────

Result<void> BlockWriter::writeBlockBytes(std::ostream& out,
        std::vector<std::uint8_t> const& buf) const {
    out.write(reinterpret_cast<char const*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
    if (!out) {
        return tl::make_unexpected(make_error(
            Error::Code::IoError, "writeBlockBytes: stream error"));
    }
    return {};
}

// ── emitChannel ─────────────────────────────────────────────────────

Result<void> BlockWriter::emitChannel(std::ostream& out,
        std::vector<std::uint8_t>& buf,
        std::uint16_t ci, std::uint8_t sov,
        ChannelData const& cd) const {
    if (cd.kind == ChannelData::Kind::Equidistant) {
        for (auto const& seg : cd.eq_segments) {
            std::size_t const value_size = numeric_value_size(seg.values);
            std::size_t const total      = numericValuesLen(seg.values);
            std::size_t const max_first  =
                osf::detail::max_samples_per_start_block(value_size, sov);
            std::size_t const max_cont   =
                osf::detail::max_samples_per_continued_block(value_size, sov);
            std::size_t const first = std::min(total, max_first);

            buf.clear();
            if (auto e = encode_start_from_values(buf, ci, sov,
                    seg.startTimestampNs, seg.sampleRateHz,
                    seg.values, 0, first); !e) {
                return e;
            }
            if (auto w = writeBlockBytes(out, buf); !w) return w;

            std::size_t written = first;
            while (written < total) {
                std::size_t const chunk = std::min(total - written, max_cont);
                buf.clear();
                if (auto e = encode_continued_from_values(buf, ci, sov,
                        seg.values, written, chunk); !e) {
                    return e;
                }
                if (auto w = writeBlockBytes(out, buf); !w) return w;
                written += chunk;
            }
        }
        return {};
    }

    if (cd.kind == ChannelData::Kind::Timestamped) {
        std::size_t const value_size = numeric_value_size(cd.ts_values);
        std::size_t const total      = cd.ts_ns.size();
        std::size_t const max_per    =
            osf::detail::max_samples_per_timestamped_block(value_size, sov);
        std::size_t written = 0;
        while (written < total) {
            std::size_t const chunk = std::min(total - written, max_per);
            buf.clear();
            if (auto e = encode_abs_ts_from_values(buf, ci, sov,
                    cd.ts_ns.data() + written,
                    cd.ts_values, written, chunk); !e) {
                return e;
            }
            if (auto w = writeBlockBytes(out, buf); !w) return w;
            written += chunk;
        }
        return {};
    }

    if (cd.kind == ChannelData::Kind::Variable) {
        // Variable: one block per sample (no chunking — spec).
        std::size_t const capacity = osf::detail::variable_sample_capacity(sov);
        for (std::size_t i = 0; i < cd.ts_ns.size(); ++i) {
            buf.clear();
            // strings is non-empty iff datatype_lock == String; a Binary channel never populates strings (datatype-lock enforced at accumulation).
            if (!cd.strings.empty()) {
                std::string_view sv = cd.strings[i];
                if (sv.size() > capacity) {
                    return tl::make_unexpected(make_error(
                        Error::Code::InvalidBlock,
                        "channel " + std::to_string(ci) +
                        ": variable string sample size " + std::to_string(sv.size()) +
                        " exceeds capacity " + std::to_string(capacity) +
                        " for sizeoflengthvalue=" + std::to_string(sov)));
                }
                if (auto e = osf::detail::encode_abs_timestamp_data(
                        buf, ci, sov, cd.ts_ns[i], sv); !e) {
                    return e;
                }
            } else {
                BinarySample bs{cd.binaries[i].data(), cd.binaries[i].size()};
                if (bs.size > capacity) {
                    return tl::make_unexpected(make_error(
                        Error::Code::InvalidBlock,
                        "channel " + std::to_string(ci) +
                        ": variable binary sample size " + std::to_string(bs.size) +
                        " exceeds capacity " + std::to_string(capacity) +
                        " for sizeoflengthvalue=" + std::to_string(sov)));
                }
                if (auto e = osf::detail::encode_abs_timestamp_data(
                        buf, ci, sov, cd.ts_ns[i], bs); !e) {
                    return e;
                }
            }
            if (auto w = writeBlockBytes(out, buf); !w) return w;
        }
        return {};
    }

    // Empty: declared channel with no samples — emit no blocks.
    return {};
}

// ── writeTo / writeToFile ─────────────────────────────────────────

Result<void> BlockWriter::writeTo(std::ostream& out) const {
    if (m_channels.empty()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "writeTo: no channels declared"));
    }

    // Local copy of defs — autobumpSizeOfLengthValue may promote Variable channels to sov=4.
    std::vector<ChannelDef> defs = m_channels;
    autobumpSizeOfLengthValue(defs);

    // Translate header-local file-info into detail::FileInfoDraft.
    detail::FileInfoDraft fi;
    fi.creator               = m_fileInfo.creator;
    fi.tag                   = m_fileInfo.tag;
    fi.reason                = m_fileInfo.reason;
    fi.createdAtLatitude   = m_fileInfo.createdAtLatitude;
    fi.createdAtLongitude  = m_fileInfo.createdAtLongitude;
    fi.createdAtAltitude   = m_fileInfo.createdAtAltitude;
    fi.namespaceSep         = m_fileInfo.namespaceSep;
    fi.comment               = m_fileInfo.comment;

    MetaBlock meta = detail::build_metablock(fi, defs);
    std::string const json  = serializeMetablockJson(meta);
    std::string const magic = "OSF5 " + std::to_string(json.size()) + "\n";

    out.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    out.write(json.data(),  static_cast<std::streamsize>(json.size()));
    if (!out) {
        return tl::make_unexpected(make_error(
            Error::Code::IoError,
            "writeTo: stream error writing header/metablock"));
    }

    std::vector<std::uint8_t> buf;
    for (std::size_t i = 0; i < m_channels.size(); ++i) {
        auto const ci  = static_cast<std::uint16_t>(i);
        std::uint8_t const sov = defs[i].sizeOfLengthValue;
        if (auto r = emitChannel(out, buf, ci, sov, m_channelData[i]); !r) return r;
    }

    out.flush();
    if (!out) {
        return tl::make_unexpected(make_error(
            Error::Code::IoError,
            "writeTo: stream error on final flush"));
    }
    return {};
}

Result<void> BlockWriter::writeToFile(std::filesystem::path path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return tl::make_unexpected(make_error(
            Error::Code::IoError,
            "writeToFile: cannot open " + path.string()));
    }
    return writeTo(f);
}

// ── fromManager helpers (anonymous namespace) ───────────────────────

namespace {

/// Build a ChannelDef from a typed DataChannel read by the DataManager.
/// Mirrors channel_def_from_manager_channel in the Rust writer.rs reference.
osf::ChannelDef channel_def_from_dc(osf::DataChannel const& dc) {
    osf::ChannelMeta const& meta = osf::channelMeta(dc);
    osf::ChannelDef def;
    def.name                   = osf::channelName(dc);
    def.dataType              = osf::channelDataType(dc);
    def.channelType           = meta.channelType;
    def.sizeOfLengthValue   = meta.sizeOfLengthValue;
    def.physicalUnit          = osf::channelPhysicalUnit(dc);
    def.physicalDimension     = meta.physicalDimension;
    def.displayName           = osf::channelDisplayName(dc);
    def.reference              = meta.reference;
    def.comment                = meta.comment;
    def.timeIncrementNs      = meta.timeIncrementNs;
    // mimeType is only carried by VariableChannel — mirror the Rust special-case.
    if (auto const* var = std::get_if<osf::VariableChannel>(&dc)) {
        def.mimeType = var->mimeType;
    }
    return def;
}

/// Copy all samples from a typed DataChannel into the builder.
/// Mirrors copy_channel_data in the Rust writer.rs reference.
osf::Result<void> copy_dc_data(osf::BlockWriter& b,
                                osf::DataChannel const& dc,
                                std::uint16_t target_idx) {
    if (auto const* eq = std::get_if<osf::EquidistantChannel>(&dc)) {
        // Equidistant: one addEquidistantSegment per segment; slice the
        // flat NumericValues by [startIndex, startIndex+sampleCount).
        for (auto const& seg : eq->segments) {
            if (seg.sampleCount == 0) continue;
            if (auto const* fv = std::get_if<std::vector<float>>(&eq->samples)) {
                if (auto r = b.addEquidistantSegment(target_idx,
                        seg.startTimestampNs, seg.sampleRateHz,
                        fv->data() + seg.startIndex, seg.sampleCount); !r)
                    return r;
            } else if (auto const* dv = std::get_if<std::vector<double>>(&eq->samples)) {
                if (auto r = b.addEquidistantSegment(target_idx,
                        seg.startTimestampNs, seg.sampleRateHz,
                        dv->data() + seg.startIndex, seg.sampleCount); !r)
                    return r;
            } else {
                return tl::make_unexpected(osf::Error{
                    osf::Error::Code::InvalidBlock,
                    "copy_dc_data: equidistant channel '" + eq->name +
                    "' has non-float/double samples"});
            }
        }
        return {};
    }

    if (auto const* ts = std::get_if<osf::TimestampedChannel>(&dc)) {
        // Timestamped numeric: dispatch on each NumericValues alternative.
        std::size_t const n = ts->timestampsNs.size();
        if (n == 0) return {};
        auto const* ts_ptr = ts->timestampsNs.data();

        return std::visit([&](auto const& vec) -> osf::Result<void> {
            using T = typename std::decay_t<decltype(vec)>::value_type;
            if constexpr (std::is_same_v<T, osf::GpsLocation>) {
                return b.addTimestampedGpsSamples(target_idx, ts_ptr, vec.data(), n);
            } else if constexpr (std::is_same_v<T, bool>) {
                // std::vector<bool> has no .data() (proxy-reference specialisation);
                // materialise a genuine bool[] so addTimestampedSamples<bool>
                // reads bool objects without UB.
                std::unique_ptr<bool[]> tmp(new bool[n]);
                for (std::size_t i = 0; i < n; ++i) tmp[i] = vec[i];
                return b.addTimestampedSamples<bool>(target_idx, ts_ptr, tmp.get(), n);
            } else {
                return b.addTimestampedSamples<T>(target_idx, ts_ptr, vec.data(), n);
            }
        }, ts->values);
    }

    if (auto const* var = std::get_if<osf::VariableChannel>(&dc)) {
        std::size_t const n = var->timestampsNs.size();
        if (n == 0) return {};
        auto const* ts_ptr = var->timestampsNs.data();

        if (var->dataType == osf::DataType::String) {
            // Build a std::string_view array pointing into the stored strings.
            auto strs_r = var->asStrings();
            if (!strs_r) return tl::make_unexpected(strs_r.error());
            std::vector<std::string_view> svs;
            svs.reserve(n);
            for (auto const& s : **strs_r) svs.emplace_back(s);
            return b.addStringSamples(target_idx, ts_ptr, svs.data(), n);
        } else {
            // Binary channel: build BinarySample views into the stored vectors.
            auto bins_r = var->asBinaries();
            if (!bins_r) return tl::make_unexpected(bins_r.error());
            std::vector<osf::BinarySample> bsv;
            bsv.reserve(n);
            for (auto const& bv : **bins_r)
                bsv.push_back(osf::BinarySample{bv.data(), bv.size()});
            return b.addBinarySamples(target_idx, ts_ptr, bsv.data(), n);
        }
    }

    // Unknown variant — defensive; DataChannel is a closed variant set.
    return tl::make_unexpected(osf::Error{
        osf::Error::Code::InvalidBlock,
        "copy_dc_data: unknown DataChannel variant"});
}

}  // namespace (fromManager helpers)

// ── BlockWriter::fromManager ────────────────────────────────────────

Result<BlockWriter> BlockWriter::fromManager(DataManager const& mgr) {
    BlockWriter b;

    // Copy writer-controllable file-info (NOT version/createdUtc).
    b.m_fileInfo.creator               = mgr.meta.fileInfo.creator;
    b.m_fileInfo.tag                   = mgr.meta.fileInfo.tag;
    b.m_fileInfo.reason                = mgr.meta.fileInfo.reason;
    b.m_fileInfo.createdAtLatitude   = mgr.meta.fileInfo.createdAtLatitude;
    b.m_fileInfo.createdAtLongitude  = mgr.meta.fileInfo.createdAtLongitude;
    b.m_fileInfo.createdAtAltitude   = mgr.meta.fileInfo.createdAtAltitude;
    b.m_fileInfo.namespaceSep         = mgr.meta.fileInfo.namespaceSep;
    b.m_fileInfo.comment               = mgr.meta.fileInfo.comment;

    for (DataChannel const& dc : mgr.channels()) {
        ChannelDef def = channel_def_from_dc(dc);
        auto idx = b.addChannel(def);
        if (!idx) return tl::make_unexpected(idx.error());
        if (auto r = copy_dc_data(b, dc, *idx); !r)
            return tl::make_unexpected(r.error());
    }
    return b;
}

// ── Free convenience functions ───────────────────────────────────────

Result<void> writeToFile(DataManager const& mgr, std::filesystem::path path) {
    auto w = BlockWriter::fromManager(mgr);
    if (!w) return tl::make_unexpected(w.error());
    return w->writeToFile(std::move(path));
}

Result<void> writeTo(DataManager const& mgr, std::ostream& out) {
    auto w = BlockWriter::fromManager(mgr);
    if (!w) return tl::make_unexpected(w.error());
    return w->writeTo(out);
}

// ── Explicit instantiations ──────────────────────────────────────────
// Float + Double only per spec rev 2026-05-04 equidistant restriction.

template Result<void> BlockWriter::addEquidistantSegmentImpl<float>(
    std::uint16_t, std::int64_t, double, float const*, std::size_t);
template Result<void> BlockWriter::addEquidistantSegmentImpl<double>(
    std::uint16_t, std::int64_t, double, double const*, std::size_t);

// All 11 numeric types for timestamped.
#define OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(T)                               \
    template Result<void> BlockWriter::addTimestampedSamplesImpl<T>(      \
        std::uint16_t, std::int64_t const*, T const*, std::size_t)

OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(bool);
OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(std::int8_t);
OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(std::int16_t);
OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(std::int32_t);
OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(std::int64_t);
OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(std::uint8_t);
OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(std::uint16_t);
OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(std::uint32_t);
OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(std::uint64_t);
OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(float);
OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(double);

#undef OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL

}  // namespace osf
