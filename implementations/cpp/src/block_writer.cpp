// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/block_writer.hpp"

#include "block_encode.hpp"           // osf::detail::encode_start_data, encode_continued_data
#include "writer_common.hpp"          // osf::detail chunking helpers + FileInfoDraft + build_metablock
#include "osf/data_channel.hpp"       // NumericValues, numeric_values_len
#include "osf/metablock.hpp"          // serialize_metablock_json

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <type_traits>
#include <utility>

namespace osf {

// ── ChannelData — real typed storage (Task 4) ─────────────────────────

struct BlockWriter::ChannelData {
    enum class Kind { Empty, Equidistant, Timestamped, Variable } kind = Kind::Empty;
    DataType datatype_lock = DataType::Unsupported;

    struct EqSegment {
        std::int64_t  start_timestamp_ns = 0;
        double        sample_rate_hz = 0.0;
        NumericValues values;   // Float or Double only for equidistant
    };
    std::vector<EqSegment> eq_segments;

    // Timestamped (Task 5/6) + Variable (Task 6) storage added later.
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

    // Datatype must match what was declared at add_channel time AND be Float/Double.
    constexpr DataType expected = data_type_for<T>();
    if (cd.datatype_lock != expected) {
        return tl::make_unexpected(make_error(
            Error::Code::DataTypeMismatch,
            "channel " + std::to_string(channel) + ": datatype mismatch"));
    }
    if (expected != DataType::Float && expected != DataType::Double) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_equidistant_segment: equidistant channels support "
            "Float and Double only"));
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

    // Local copy of defs for auto-bump (Variable auto-bump lands in Task 6).
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
