// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/block_writer.hpp"

#include "block_encode.hpp"           // osf::detail::encode_start_data, encode_continued_data
#include "writer_common.hpp"          // osf::detail chunking helpers + FileInfoDraft + build_metablock
#include "osf/data_channel.hpp"       // NumericValues, numeric_values_len
#include "osf/manager.hpp"            // DataManager (from_manager)
#include "osf/metablock.hpp"          // serialize_metablock_json

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
        std::int64_t  start_timestamp_ns = 0;
        double        sample_rate_hz = 0.0;
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
// Precondition: the caller (add_timestamped_samples_impl) has already
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

void BlockWriter::set_creator(std::string v)       { file_info_.creator       = std::move(v); }
void BlockWriter::set_tag(std::string v)           { file_info_.tag           = std::move(v); }
void BlockWriter::set_reason(std::string v)        { file_info_.reason        = std::move(v); }
void BlockWriter::set_namespace_sep(std::string v) { file_info_.namespace_sep = std::move(v); }
void BlockWriter::set_comment(std::string v)       { file_info_.comment       = std::move(v); }

void BlockWriter::set_location(double lat, double lon, double alt) {
    file_info_.created_at_latitude  = lat;
    file_info_.created_at_longitude = lon;
    file_info_.created_at_altitude  = alt;
}

// ── add_channel ──────────────────────────────────────────────────────

Result<std::uint16_t> BlockWriter::add_channel(ChannelDef def) {
    if (def.size_of_length_value != 2 && def.size_of_length_value != 4) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_channel: size_of_length_value must be 2 or 4"));
    }
    if (def.data_type == DataType::Unsupported) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_channel: data_type Unsupported is not writeable"));
    }
    if (def.channel_type == ChannelType::Unsupported) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_channel: channel_type Unsupported is not writeable"));
    }
    if (channels_.size() >= 0xFFFF) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_channel: too many channels (max 65535)"));
    }

    auto const idx = static_cast<std::uint16_t>(channels_.size());
    ChannelData cd;
    cd.datatype_lock = def.data_type;
    name_to_index_.emplace(def.name, idx);
    channels_.push_back(std::move(def));
    channel_data_.push_back(std::move(cd));
    return idx;
}

// ── channel_count / channel_index ───────────────────────────────────

std::size_t BlockWriter::channel_count() const noexcept {
    return channels_.size();
}

std::optional<std::uint16_t>
BlockWriter::channel_index(std::string_view name) const {
    auto it = name_to_index_.find(std::string{name});
    if (it == name_to_index_.end()) return std::nullopt;
    return it->second;
}

// ── add_equidistant_segment_impl<T> ─────────────────────────────────

template <typename T>
Result<void> BlockWriter::add_equidistant_segment_impl(
        std::uint16_t channel, std::int64_t start_ts_ns, double rate_hz,
        T const* samples, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_equidistant_segment: count must be > 0"));
    }
    if (!(rate_hz > 0.0) || !std::isfinite(rate_hz)) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_equidistant_segment: sample_rate_hz must be a "
            "positive finite double"));
    }
    if (channel >= channels_.size()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_equidistant_segment: channel index out of range"));
    }

    auto& cd = channel_data_[channel];

    // Kind-lock: once a channel has an equidistant segment it stays equidistant.
    if (cd.kind != ChannelData::Kind::Empty &&
        cd.kind != ChannelData::Kind::Equidistant) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidBlock,
            "channel " + std::to_string(channel) + ": mixed block types"));
    }

    // Datatype must match what was declared at add_channel time AND be
    // Float/Double. The Float/Double restriction is a compile-time
    // invariant: this template is only instantiated for float/double (the
    // public add_equidistant_segment overloads), so the runtime branch
    // would be dead code — a static_assert documents it without tripping
    // MSVC C4127 (constant conditional) under /WX.
    constexpr DataType expected = data_type_for<T>();
    static_assert(expected == DataType::Float || expected == DataType::Double,
                  "add_equidistant_segment_impl is only valid for "
                  "Float and Double");
    if (cd.datatype_lock != expected) {
        return tl::make_unexpected(make_error(
            Error::Code::DataTypeMismatch,
            "channel " + std::to_string(channel) + ": datatype mismatch"));
    }

    cd.kind = ChannelData::Kind::Equidistant;
    ChannelData::EqSegment seg;
    seg.start_timestamp_ns = start_ts_ns;
    seg.sample_rate_hz     = rate_hz;
    seg.values             = NumericValues{std::vector<T>(samples, samples + count)};
    cd.eq_segments.push_back(std::move(seg));
    return {};
}

// ── add_timestamped_samples_impl<T> ─────────────────────────────────

template <typename T>
Result<void> BlockWriter::add_timestamped_samples_impl(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        T const* values, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_timestamped_samples: count must be > 0"));
    }
    if (channel >= channels_.size()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_timestamped_samples: channel index out of range"));
    }

    auto& cd = channel_data_[channel];

    // Kind-lock: once a channel has timestamped data it stays timestamped.
    if (cd.kind != ChannelData::Kind::Empty &&
        cd.kind != ChannelData::Kind::Timestamped) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidBlock,
            "channel " + std::to_string(channel) + ": mixed block types"));
    }

    // Datatype must match what was declared at add_channel time.
    constexpr DataType expected = data_type_for<T>();
    if (cd.datatype_lock != expected) {
        return tl::make_unexpected(make_error(
            Error::Code::DataTypeMismatch,
            "channel " + std::to_string(channel) + ": datatype mismatch"));
    }

    cd.kind = ChannelData::Kind::Timestamped;
    cd.ts_ns.insert(cd.ts_ns.end(), timestamps_ns, timestamps_ns + count);
    append_timestamped_values<T>(cd.ts_values, values, count);
    return {};
}

// ── Public add_equidistant_segment overloads ─────────────────────────

Result<void> BlockWriter::add_equidistant_segment(
        std::uint16_t channel, std::int64_t start_ts_ns, double rate_hz,
        float const* samples, std::size_t count) {
    return add_equidistant_segment_impl<float>(channel, start_ts_ns, rate_hz, samples, count);
}

Result<void> BlockWriter::add_equidistant_segment(
        std::uint16_t channel, std::int64_t start_ts_ns, double rate_hz,
        double const* samples, std::size_t count) {
    return add_equidistant_segment_impl<double>(channel, start_ts_ns, rate_hz, samples, count);
}

// ── add_timestamped_gps_samples / _sample ───────────────────────────

Result<void> BlockWriter::add_timestamped_gps_samples(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        GpsLocation const* values, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_timestamped_gps_samples: count must be > 0"));
    }
    if (channel >= channels_.size()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_timestamped_gps_samples: channel index out of range"));
    }

    auto& cd = channel_data_[channel];

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
    cd.ts_ns.insert(cd.ts_ns.end(), timestamps_ns, timestamps_ns + count);
    append_gps_values(cd.ts_values, values, count);
    return {};
}

Result<void> BlockWriter::add_timestamped_gps_sample(
        std::uint16_t channel, std::int64_t timestamp_ns, GpsLocation value) {
    return add_timestamped_gps_samples(channel, &timestamp_ns, &value, 1);
}

// ── add_string_samples / _sample ────────────────────────────────────

Result<void> BlockWriter::add_string_samples(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        std::string_view const* values, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_string_samples: count must be > 0"));
    }
    if (channel >= channels_.size()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_string_samples: channel index out of range"));
    }

    auto& cd = channel_data_[channel];

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
    cd.ts_ns.insert(cd.ts_ns.end(), timestamps_ns, timestamps_ns + count);
    for (std::size_t i = 0; i < count; ++i) {
        cd.strings.emplace_back(values[i]);
    }
    return {};
}

Result<void> BlockWriter::add_string_sample(
        std::uint16_t channel, std::int64_t timestamp_ns, std::string_view value) {
    return add_string_samples(channel, &timestamp_ns, &value, 1);
}

// ── add_binary_samples / _sample ────────────────────────────────────

Result<void> BlockWriter::add_binary_samples(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        BinarySample const* values, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_binary_samples: count must be > 0"));
    }
    if (channel >= channels_.size()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_binary_samples: channel index out of range"));
    }

    auto& cd = channel_data_[channel];

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
    cd.ts_ns.insert(cd.ts_ns.end(), timestamps_ns, timestamps_ns + count);
    for (std::size_t i = 0; i < count; ++i) {
        cd.binaries.emplace_back(values[i].data, values[i].data + values[i].size);
    }
    return {};
}

Result<void> BlockWriter::add_binary_sample(
        std::uint16_t channel, std::int64_t timestamp_ns, BinarySample value) {
    return add_binary_samples(channel, &timestamp_ns, &value, 1);
}

// ── autobump_size_of_length_value ───────────────────────────────────

void BlockWriter::autobump_size_of_length_value(std::vector<ChannelDef>& defs) const {
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].size_of_length_value == 4) continue;
        ChannelData const& cd = channel_data_[i];
        if (cd.kind != ChannelData::Kind::Variable) continue;
        std::size_t max_sample = 0;
        for (auto const& s : cd.strings)  max_sample = std::max(max_sample, s.size());
        for (auto const& b : cd.binaries) max_sample = std::max(max_sample, b.size());
        if (osf::detail::VARIABLE_BLOCK_OVERHEAD_BYTES + max_sample
                > osf::detail::max_payload_for_sov(2)) {
            defs[i].size_of_length_value = 4;
        }
    }
}

// ── write_block_bytes ────────────────────────────────────────────────

Result<void> BlockWriter::write_block_bytes(std::ostream& out,
        std::vector<std::uint8_t> const& buf) const {
    out.write(reinterpret_cast<char const*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
    if (!out) {
        return tl::make_unexpected(make_error(
            Error::Code::IoError, "write_block_bytes: stream error"));
    }
    return {};
}

// ── emit_channel ─────────────────────────────────────────────────────

Result<void> BlockWriter::emit_channel(std::ostream& out,
        std::vector<std::uint8_t>& buf,
        std::uint16_t ci, std::uint8_t sov,
        ChannelData const& cd) const {
    if (cd.kind == ChannelData::Kind::Equidistant) {
        for (auto const& seg : cd.eq_segments) {
            std::size_t const value_size = numeric_value_size(seg.values);
            std::size_t const total      = numeric_values_len(seg.values);
            std::size_t const max_first  =
                osf::detail::max_samples_per_start_block(value_size, sov);
            std::size_t const max_cont   =
                osf::detail::max_samples_per_continued_block(value_size, sov);
            std::size_t const first = std::min(total, max_first);

            buf.clear();
            if (auto e = encode_start_from_values(buf, ci, sov,
                    seg.start_timestamp_ns, seg.sample_rate_hz,
                    seg.values, 0, first); !e) {
                return e;
            }
            if (auto w = write_block_bytes(out, buf); !w) return w;

            std::size_t written = first;
            while (written < total) {
                std::size_t const chunk = std::min(total - written, max_cont);
                buf.clear();
                if (auto e = encode_continued_from_values(buf, ci, sov,
                        seg.values, written, chunk); !e) {
                    return e;
                }
                if (auto w = write_block_bytes(out, buf); !w) return w;
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
            if (auto w = write_block_bytes(out, buf); !w) return w;
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
            if (auto w = write_block_bytes(out, buf); !w) return w;
        }
        return {};
    }

    // Empty: declared channel with no samples — emit no blocks.
    return {};
}

// ── write_to / write_to_file ─────────────────────────────────────────

Result<void> BlockWriter::write_to(std::ostream& out) const {
    if (channels_.empty()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "write_to: no channels declared"));
    }

    // Local copy of defs — autobump_size_of_length_value may promote Variable channels to sov=4.
    std::vector<ChannelDef> defs = channels_;
    autobump_size_of_length_value(defs);

    // Translate header-local file-info into detail::FileInfoDraft.
    detail::FileInfoDraft fi;
    fi.creator               = file_info_.creator;
    fi.tag                   = file_info_.tag;
    fi.reason                = file_info_.reason;
    fi.created_at_latitude   = file_info_.created_at_latitude;
    fi.created_at_longitude  = file_info_.created_at_longitude;
    fi.created_at_altitude   = file_info_.created_at_altitude;
    fi.namespace_sep         = file_info_.namespace_sep;
    fi.comment               = file_info_.comment;

    MetaBlock meta = detail::build_metablock(fi, defs);
    std::string const json  = serialize_metablock_json(meta);
    std::string const magic = "OSF5 " + std::to_string(json.size()) + "\n";

    out.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    out.write(json.data(),  static_cast<std::streamsize>(json.size()));
    if (!out) {
        return tl::make_unexpected(make_error(
            Error::Code::IoError,
            "write_to: stream error writing header/metablock"));
    }

    std::vector<std::uint8_t> buf;
    for (std::size_t i = 0; i < channels_.size(); ++i) {
        auto const ci  = static_cast<std::uint16_t>(i);
        std::uint8_t const sov = defs[i].size_of_length_value;
        if (auto r = emit_channel(out, buf, ci, sov, channel_data_[i]); !r) return r;
    }

    out.flush();
    if (!out) {
        return tl::make_unexpected(make_error(
            Error::Code::IoError,
            "write_to: stream error on final flush"));
    }
    return {};
}

Result<void> BlockWriter::write_to_file(std::filesystem::path path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return tl::make_unexpected(make_error(
            Error::Code::IoError,
            "write_to_file: cannot open " + path.string()));
    }
    return write_to(f);
}

// ── from_manager helpers (anonymous namespace) ───────────────────────

namespace {

/// Build a ChannelDef from a typed DataChannel read by the DataManager.
/// Mirrors channel_def_from_manager_channel in the Rust writer.rs reference.
osf::ChannelDef channel_def_from_dc(osf::DataChannel const& dc) {
    osf::ChannelMeta const& meta = osf::channel_meta(dc);
    osf::ChannelDef def;
    def.name                   = osf::channel_name(dc);
    def.data_type              = osf::channel_data_type(dc);
    def.channel_type           = meta.channel_type;
    def.size_of_length_value   = meta.size_of_length_value;
    def.physical_unit          = osf::channel_physical_unit(dc);
    def.physical_dimension     = meta.physical_dimension;
    def.display_name           = osf::channel_display_name(dc);
    def.reference              = meta.reference;
    def.comment                = meta.comment;
    def.time_increment_ns      = meta.time_increment_ns;
    // mime_type is only carried by VariableChannel — mirror the Rust special-case.
    if (auto const* var = std::get_if<osf::VariableChannel>(&dc)) {
        def.mime_type = var->mime_type;
    }
    return def;
}

/// Copy all samples from a typed DataChannel into the builder.
/// Mirrors copy_channel_data in the Rust writer.rs reference.
osf::Result<void> copy_dc_data(osf::BlockWriter& b,
                                osf::DataChannel const& dc,
                                std::uint16_t target_idx) {
    if (auto const* eq = std::get_if<osf::EquidistantChannel>(&dc)) {
        // Equidistant: one add_equidistant_segment per segment; slice the
        // flat NumericValues by [start_index, start_index+sample_count).
        for (auto const& seg : eq->segments) {
            if (seg.sample_count == 0) continue;
            if (auto const* fv = std::get_if<std::vector<float>>(&eq->samples)) {
                if (auto r = b.add_equidistant_segment(target_idx,
                        seg.start_timestamp_ns, seg.sample_rate_hz,
                        fv->data() + seg.start_index, seg.sample_count); !r)
                    return r;
            } else if (auto const* dv = std::get_if<std::vector<double>>(&eq->samples)) {
                if (auto r = b.add_equidistant_segment(target_idx,
                        seg.start_timestamp_ns, seg.sample_rate_hz,
                        dv->data() + seg.start_index, seg.sample_count); !r)
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
        std::size_t const n = ts->timestamps_ns.size();
        if (n == 0) return {};
        auto const* ts_ptr = ts->timestamps_ns.data();

        return std::visit([&](auto const& vec) -> osf::Result<void> {
            using T = typename std::decay_t<decltype(vec)>::value_type;
            if constexpr (std::is_same_v<T, osf::GpsLocation>) {
                return b.add_timestamped_gps_samples(target_idx, ts_ptr, vec.data(), n);
            } else if constexpr (std::is_same_v<T, bool>) {
                // std::vector<bool> has no .data() (proxy-reference specialisation);
                // materialise a genuine bool[] so add_timestamped_samples<bool>
                // reads bool objects without UB.
                std::unique_ptr<bool[]> tmp(new bool[n]);
                for (std::size_t i = 0; i < n; ++i) tmp[i] = vec[i];
                return b.add_timestamped_samples<bool>(target_idx, ts_ptr, tmp.get(), n);
            } else {
                return b.add_timestamped_samples<T>(target_idx, ts_ptr, vec.data(), n);
            }
        }, ts->values);
    }

    if (auto const* var = std::get_if<osf::VariableChannel>(&dc)) {
        std::size_t const n = var->timestamps_ns.size();
        if (n == 0) return {};
        auto const* ts_ptr = var->timestamps_ns.data();

        if (var->data_type == osf::DataType::String) {
            // Build a std::string_view array pointing into the stored strings.
            auto strs_r = var->as_strings();
            if (!strs_r) return tl::make_unexpected(strs_r.error());
            std::vector<std::string_view> svs;
            svs.reserve(n);
            for (auto const& s : **strs_r) svs.emplace_back(s);
            return b.add_string_samples(target_idx, ts_ptr, svs.data(), n);
        } else {
            // Binary channel: build BinarySample views into the stored vectors.
            auto bins_r = var->as_binaries();
            if (!bins_r) return tl::make_unexpected(bins_r.error());
            std::vector<osf::BinarySample> bsv;
            bsv.reserve(n);
            for (auto const& bv : **bins_r)
                bsv.push_back(osf::BinarySample{bv.data(), bv.size()});
            return b.add_binary_samples(target_idx, ts_ptr, bsv.data(), n);
        }
    }

    // Unknown variant — defensive; DataChannel is a closed variant set.
    return tl::make_unexpected(osf::Error{
        osf::Error::Code::InvalidBlock,
        "copy_dc_data: unknown DataChannel variant"});
}

}  // namespace (from_manager helpers)

// ── BlockWriter::from_manager ────────────────────────────────────────

Result<BlockWriter> BlockWriter::from_manager(DataManager const& mgr) {
    BlockWriter b;

    // Copy writer-controllable file-info (NOT version/created_utc).
    b.file_info_.creator               = mgr.meta.file_info.creator;
    b.file_info_.tag                   = mgr.meta.file_info.tag;
    b.file_info_.reason                = mgr.meta.file_info.reason;
    b.file_info_.created_at_latitude   = mgr.meta.file_info.created_at_latitude;
    b.file_info_.created_at_longitude  = mgr.meta.file_info.created_at_longitude;
    b.file_info_.created_at_altitude   = mgr.meta.file_info.created_at_altitude;
    b.file_info_.namespace_sep         = mgr.meta.file_info.namespace_sep;
    b.file_info_.comment               = mgr.meta.file_info.comment;

    for (DataChannel const& dc : mgr.channels()) {
        ChannelDef def = channel_def_from_dc(dc);
        auto idx = b.add_channel(def);
        if (!idx) return tl::make_unexpected(idx.error());
        if (auto r = copy_dc_data(b, dc, *idx); !r)
            return tl::make_unexpected(r.error());
    }
    return b;
}

// ── Free convenience functions ───────────────────────────────────────

Result<void> write_to_file(DataManager const& mgr, std::filesystem::path path) {
    auto w = BlockWriter::from_manager(mgr);
    if (!w) return tl::make_unexpected(w.error());
    return w->write_to_file(std::move(path));
}

Result<void> write_to(DataManager const& mgr, std::ostream& out) {
    auto w = BlockWriter::from_manager(mgr);
    if (!w) return tl::make_unexpected(w.error());
    return w->write_to(out);
}

// ── Explicit instantiations ──────────────────────────────────────────
// Float + Double only per spec rev 2026-05-04 equidistant restriction.

template Result<void> BlockWriter::add_equidistant_segment_impl<float>(
    std::uint16_t, std::int64_t, double, float const*, std::size_t);
template Result<void> BlockWriter::add_equidistant_segment_impl<double>(
    std::uint16_t, std::int64_t, double, double const*, std::size_t);

// All 11 numeric types for timestamped.
#define OSF_INSTANTIATE_BW_TIMESTAMPED_IMPL(T)                               \
    template Result<void> BlockWriter::add_timestamped_samples_impl<T>(      \
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
