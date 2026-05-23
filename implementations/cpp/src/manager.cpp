// SPDX-License-Identifier: MIT

#include <osf/manager.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <osf/block.hpp>
#include <osf/header.hpp>
#include <osf/reader.hpp>
#include <osf/types.hpp>

namespace osf {

// =====================================================================
// File-local helpers
// =====================================================================

namespace {

Error invalid_block(std::string msg) {
    return Error{Error::Code::InvalidBlock, std::move(msg)};
}

Error data_type_mismatch(std::uint16_t channel, DataType expected,
                         DataType got) {
    std::ostringstream oss;
    oss << "channel " << channel << " data type mismatch: expected "
        << static_cast<int>(expected) << ", got block payload "
        << static_cast<int>(got);
    return Error{Error::Code::DataTypeMismatch, oss.str()};
}

Error channel_mixed(std::uint16_t channel) {
    std::ostringstream oss;
    oss << "channel " << channel
        << " mixes equidistant and timestamped blocks";
    return Error{Error::Code::ChannelMixedBlockTypes, oss.str()};
}

Error continued_without_start(std::uint16_t channel) {
    std::ostringstream oss;
    oss << "channel " << channel
        << " produced bcContinuedData without a preceding bcStartData";
    return Error{Error::Code::ContinuedDataWithoutStart, oss.str()};
}

Error rel_stamp_without_anchor(std::uint16_t channel) {
    std::ostringstream oss;
    oss << "channel " << channel
        << " produced bcContinuedRelStampData without an absolute anchor";
    return Error{Error::Code::RelStampWithoutAnchor, oss.str()};
}

DataType numeric_payload_data_type(NumericPayload const& p) noexcept {
    return std::visit([](auto const& vec) noexcept -> DataType {
        using V = std::decay_t<decltype(vec)>;
        if constexpr (std::is_same_v<V, std::vector<bool>>)
            return DataType::Bool;
        else if constexpr (std::is_same_v<V, std::vector<std::int8_t>>)
            return DataType::Int8;
        else if constexpr (std::is_same_v<V, std::vector<std::int16_t>>)
            return DataType::Int16;
        else if constexpr (std::is_same_v<V, std::vector<std::int32_t>>)
            return DataType::Int32;
        else if constexpr (std::is_same_v<V, std::vector<std::int64_t>>)
            return DataType::Int64;
        else if constexpr (std::is_same_v<V, std::vector<std::uint8_t>>)
            return DataType::UInt8;
        else if constexpr (std::is_same_v<V, std::vector<std::uint16_t>>)
            return DataType::UInt16;
        else if constexpr (std::is_same_v<V, std::vector<std::uint32_t>>)
            return DataType::UInt32;
        else if constexpr (std::is_same_v<V, std::vector<std::uint64_t>>)
            return DataType::UInt64;
        else if constexpr (std::is_same_v<V, std::vector<float>>)
            return DataType::Float;
        else
            return DataType::Double;
    }, p);
}

DataType timestamped_payload_data_type(TimestampedPayload const& p) noexcept {
    return std::visit([](auto const& vec) noexcept -> DataType {
        using V = std::decay_t<decltype(vec)>;
        using Pair = typename V::value_type;
        using T = typename Pair::second_type;
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
        else if constexpr (std::is_same_v<T, std::string>)   return DataType::String;
        else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>)
            return DataType::Binary;
        else                                                 return DataType::GpsLocation;
    }, p);
}

DataType rel_timestamped_payload_data_type(
    RelTimestampedPayload const& p) noexcept {
    return std::visit([](auto const& vec) noexcept -> DataType {
        using V = std::decay_t<decltype(vec)>;
        using T = typename V::value_type::second_type;
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
        else                                                 return DataType::Double;
    }, p);
}

// Append a NumericPayload onto an existing NumericValues whose
// alternative type matches. Returns the number of samples appended.
Result<std::size_t> extend_numeric(NumericValues& target,
                                   NumericPayload payload,
                                   std::uint16_t channel) {
    DataType const target_dt  = numeric_values_data_type(target);
    DataType const payload_dt = numeric_payload_data_type(payload);
    if (payload_dt != target_dt) {
        return tl::make_unexpected(
            data_type_mismatch(channel, target_dt, payload_dt));
    }
    std::size_t appended = 0;
    std::visit([&](auto&& src_vec) {
        using V = std::decay_t<decltype(src_vec)>;
        auto* dst = std::get_if<V>(&target);
        if (!dst) return;  // unreachable: type-check above guarantees match
        appended = src_vec.size();
        dst->insert(dst->end(),
                    std::make_move_iterator(src_vec.begin()),
                    std::make_move_iterator(src_vec.end()));
    }, std::move(payload));
    return appended;
}

// Compute the last absolute timestamp produced by a segment with
// `(start_timestamp_ns, sample_rate_hz, sample_count)`. Mirrors the
// Rust update_last_ts_from_segment helper.
std::int64_t segment_last_timestamp(std::int64_t start_ts, double rate,
                                    std::size_t sample_count) noexcept {
    if (sample_count == 0) return start_ts;
    if (rate > 0.0) {
        double const offset =
            (static_cast<double>(sample_count - 1) / rate) * 1.0e9;
        return start_ts + static_cast<std::int64_t>(offset);
    }
    return start_ts;
}

// ---------------------------------------------------------------------
// ChannelBuilder — accumulator per channel.
// ---------------------------------------------------------------------

struct ChannelBuilder {
    enum class State {
        Pending,        ///< Numeric channel, no typed block seen yet
        Equidistant,    ///< Equidistant numeric
        Timestamped,    ///< Timestamped numeric (or GPS)
        Variable,       ///< Timestamped string / binary
        Unsupported,    ///< Dropped from final output
    };

    State state = State::Pending;

    std::uint16_t index = 0;
    std::string name;
    DataType data_type = DataType::Unsupported;
    std::optional<std::string> physical_unit;
    std::optional<std::string> display_name;
    std::optional<std::string> mime_type;
    ChannelMeta channel_def;

    // Anchor for bcContinuedRelStampData (carries deltas).
    std::optional<std::int64_t> last_timestamp_ns;

    // Equidistant storage.
    NumericValues eq_samples{std::vector<double>{}};
    std::vector<Segment> eq_segments;

    // Timestamped storage.
    std::vector<std::int64_t> ts_timestamps_ns;
    NumericValues ts_values{std::vector<double>{}};

    // Variable storage.
    std::optional<std::vector<std::string>> var_strings;
    std::optional<std::vector<std::vector<std::uint8_t>>> var_binaries;
};

void seed_initial_state(ChannelBuilder& b) {
    if (b.data_type == DataType::Unsupported ||
        b.channel_def.channel_type == ChannelType::Unsupported) {
        b.state = ChannelBuilder::State::Unsupported;
        return;
    }
    if (b.data_type == DataType::String) {
        b.state = ChannelBuilder::State::Variable;
        b.var_strings.emplace();
        return;
    }
    if (b.data_type == DataType::Binary || b.data_type == DataType::ByteArray) {
        b.state = ChannelBuilder::State::Variable;
        b.var_binaries.emplace();
        return;
    }
    b.state = ChannelBuilder::State::Pending;
}

Result<void> apply_start(ChannelBuilder& b, StartData payload) {
    DataType const payload_dt = numeric_payload_data_type(payload.samples);
    if (payload_dt != b.data_type) {
        return tl::make_unexpected(
            data_type_mismatch(b.index, b.data_type, payload_dt));
    }

    switch (b.state) {
        case ChannelBuilder::State::Pending: {
            auto empty = numeric_values_empty_for(b.data_type);
            if (!empty) {
                return tl::make_unexpected(invalid_block(
                    "channel cannot hold equidistant samples"));
            }
            b.eq_samples = std::move(*empty);
            auto appended = extend_numeric(b.eq_samples,
                                           std::move(payload.samples), b.index);
            if (!appended) return tl::make_unexpected(std::move(appended).error());
            Segment seg;
            seg.start_timestamp_ns = payload.start_timestamp_ns;
            seg.sample_rate_hz     = payload.sample_rate_hz;
            seg.start_index        = 0;
            seg.sample_count       = *appended;
            b.eq_segments.clear();
            b.eq_segments.push_back(seg);
            b.state = ChannelBuilder::State::Equidistant;
            b.last_timestamp_ns = segment_last_timestamp(
                payload.start_timestamp_ns, payload.sample_rate_hz, *appended);
            return {};
        }
        case ChannelBuilder::State::Equidistant: {
            std::size_t const start_index = numeric_values_len(b.eq_samples);
            auto appended = extend_numeric(b.eq_samples,
                                           std::move(payload.samples), b.index);
            if (!appended) return tl::make_unexpected(std::move(appended).error());
            Segment seg;
            seg.start_timestamp_ns = payload.start_timestamp_ns;
            seg.sample_rate_hz     = payload.sample_rate_hz;
            seg.start_index        = start_index;
            seg.sample_count       = *appended;
            b.eq_segments.push_back(seg);
            b.last_timestamp_ns = segment_last_timestamp(
                payload.start_timestamp_ns, payload.sample_rate_hz, *appended);
            return {};
        }
        case ChannelBuilder::State::Timestamped:
        case ChannelBuilder::State::Variable:
            return tl::make_unexpected(channel_mixed(b.index));
        case ChannelBuilder::State::Unsupported:
            return {};
    }
    return {};
}

Result<void> apply_continued(ChannelBuilder& b, ContinuedData payload) {
    DataType const payload_dt = numeric_payload_data_type(payload.samples);
    if (payload_dt != b.data_type) {
        return tl::make_unexpected(
            data_type_mismatch(b.index, b.data_type, payload_dt));
    }
    switch (b.state) {
        case ChannelBuilder::State::Pending:
            return tl::make_unexpected(continued_without_start(b.index));
        case ChannelBuilder::State::Equidistant: {
            if (b.eq_segments.empty()) {
                return tl::make_unexpected(continued_without_start(b.index));
            }
            auto appended = extend_numeric(b.eq_samples,
                                           std::move(payload.samples), b.index);
            if (!appended) return tl::make_unexpected(std::move(appended).error());
            auto& last = b.eq_segments.back();
            last.sample_count += *appended;
            b.last_timestamp_ns = segment_last_timestamp(
                last.start_timestamp_ns, last.sample_rate_hz, last.sample_count);
            return {};
        }
        case ChannelBuilder::State::Timestamped:
        case ChannelBuilder::State::Variable:
            return tl::make_unexpected(channel_mixed(b.index));
        case ChannelBuilder::State::Unsupported:
            return {};
    }
    return {};
}

// Routes an AbsTimestampData payload into the Timestamped (numeric or
// GPS) or Variable (string / binary) storage as appropriate.
Result<void> apply_abs_timestamped(ChannelBuilder& b,
                                   AbsTimestampData payload) {
    DataType const payload_dt = timestamped_payload_data_type(payload.samples);
    if (payload_dt != b.data_type) {
        return tl::make_unexpected(
            data_type_mismatch(b.index, b.data_type, payload_dt));
    }

    switch (b.state) {
        case ChannelBuilder::State::Pending: {
            // Lock to Timestamped (numeric / GPS). String / binary
            // would have been seeded into the Variable state by
            // seed_initial_state, so reaching Pending here implies
            // the payload is one of the numeric / GPS variants.
            auto empty = numeric_values_empty_for(b.data_type);
            if (!empty) {
                return tl::make_unexpected(invalid_block(
                    "unexpected variable-length payload in numeric "
                    "timestamped init"));
            }
            b.ts_values = std::move(*empty);
            b.ts_timestamps_ns.clear();
            // Fallthrough into the Timestamped-extend block below.
            b.state = ChannelBuilder::State::Timestamped;
            [[fallthrough]];
        }
        case ChannelBuilder::State::Timestamped: {
            DataType const target_dt = numeric_values_data_type(b.ts_values);
            if (payload_dt != target_dt) {
                return tl::make_unexpected(
                    data_type_mismatch(b.index, target_dt, payload_dt));
            }
            std::size_t appended = 0;
            std::visit([&](auto&& src_vec) {
                using V = std::decay_t<decltype(src_vec)>;
                using Pair = typename V::value_type;
                using T = typename Pair::second_type;
                if constexpr (std::is_same_v<T, std::string> ||
                              std::is_same_v<T, std::vector<std::uint8_t>>) {
                    // Variable types should never reach the Timestamped
                    // branch — the data-type check above guarantees it.
                    return;
                } else {
                    auto* dst =
                        std::get_if<std::vector<T>>(&b.ts_values);
                    if (!dst) return;
                    appended = src_vec.size();
                    for (auto& [ts, value] : src_vec) {
                        b.ts_timestamps_ns.push_back(ts);
                        dst->push_back(std::move(value));
                    }
                }
            }, std::move(payload.samples));
            if (appended > 0) {
                b.last_timestamp_ns = b.ts_timestamps_ns.back();
            }
            return {};
        }
        case ChannelBuilder::State::Variable: {
            // String XOR binary based on the channel's declared
            // data_type — payload variant must match.
            std::size_t appended = 0;
            bool mismatched = false;
            std::visit([&](auto&& src_vec) {
                using V = std::decay_t<decltype(src_vec)>;
                using Pair = typename V::value_type;
                using T = typename Pair::second_type;
                if constexpr (std::is_same_v<T, std::string>) {
                    if (b.data_type != DataType::String || !b.var_strings) {
                        mismatched = true;
                        return;
                    }
                    appended = src_vec.size();
                    for (auto& [ts, value] : src_vec) {
                        b.ts_timestamps_ns.push_back(ts);
                        b.var_strings->push_back(std::move(value));
                    }
                } else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) {
                    bool const ok = (b.data_type == DataType::Binary ||
                                      b.data_type == DataType::ByteArray) &&
                                     b.var_binaries;
                    if (!ok) {
                        mismatched = true;
                        return;
                    }
                    appended = src_vec.size();
                    for (auto& [ts, value] : src_vec) {
                        b.ts_timestamps_ns.push_back(ts);
                        b.var_binaries->push_back(std::move(value));
                    }
                } else {
                    // Numeric / GPS on a Variable channel — mismatch.
                    mismatched = true;
                }
            }, std::move(payload.samples));
            if (mismatched) {
                return tl::make_unexpected(
                    data_type_mismatch(b.index, b.data_type, payload_dt));
            }
            if (appended > 0) {
                b.last_timestamp_ns = b.ts_timestamps_ns.back();
            }
            return {};
        }
        case ChannelBuilder::State::Equidistant:
            return tl::make_unexpected(channel_mixed(b.index));
        case ChannelBuilder::State::Unsupported:
            return {};
    }
    return {};
}

Result<void> apply_rel_timestamped(ChannelBuilder& b,
                                   ContinuedRelStampData payload) {
    DataType const payload_dt = rel_timestamped_payload_data_type(payload.samples);
    if (payload_dt != b.data_type) {
        return tl::make_unexpected(
            data_type_mismatch(b.index, b.data_type, payload_dt));
    }
    if (!b.last_timestamp_ns) {
        return tl::make_unexpected(rel_stamp_without_anchor(b.index));
    }

    if (b.state != ChannelBuilder::State::Timestamped) {
        if (b.state == ChannelBuilder::State::Unsupported) return {};
        return tl::make_unexpected(channel_mixed(b.index));
    }

    DataType const target_dt = numeric_values_data_type(b.ts_values);
    if (payload_dt != target_dt) {
        return tl::make_unexpected(
            data_type_mismatch(b.index, target_dt, payload_dt));
    }

    std::int64_t anchor = *b.last_timestamp_ns;
    std::visit([&](auto&& src_vec) {
        using V = std::decay_t<decltype(src_vec)>;
        using Pair = typename V::value_type;
        using T = typename Pair::second_type;
        auto* dst = std::get_if<std::vector<T>>(&b.ts_values);
        if (!dst) return;
        for (auto& [delta, value] : src_vec) {
            anchor += static_cast<std::int64_t>(delta);
            b.ts_timestamps_ns.push_back(anchor);
            dst->push_back(std::move(value));
        }
    }, std::move(payload.samples));
    b.last_timestamp_ns = anchor;
    return {};
}

Result<void> apply_block_kind(ChannelBuilder& b, BlockKind kind) {
    return std::visit([&](auto&& specific) -> Result<void> {
        using K = std::decay_t<decltype(specific)>;
        if constexpr (std::is_same_v<K, StartData>) {
            return apply_start(b, std::move(specific));
        } else if constexpr (std::is_same_v<K, ContinuedData>) {
            return apply_continued(b, std::move(specific));
        } else if constexpr (std::is_same_v<K, AbsTimestampData>) {
            return apply_abs_timestamped(b, std::move(specific));
        } else if constexpr (std::is_same_v<K, ContinuedRelStampData>) {
            return apply_rel_timestamped(b, std::move(specific));
        } else {
            // Skipped — nothing to apply.
            (void) specific;
            return {};
        }
    }, std::move(kind));
}

// Finalize a builder into a typed DataChannel. Returns std::nullopt
// for Unsupported (channel is dropped from the final list).
std::optional<DataChannel> finalize_builder(ChannelBuilder&& b) {
    switch (b.state) {
        case ChannelBuilder::State::Unsupported:
            return std::nullopt;
        case ChannelBuilder::State::Pending: {
            // Channel declared but no typed block arrived — emit an
            // empty channel matching the metablock's data-type group.
            if (b.data_type == DataType::String) {
                VariableChannel v;
                v.index = b.index;
                v.name = std::move(b.name);
                v.data_type = b.data_type;
                v.physical_unit = std::move(b.physical_unit);
                v.display_name = std::move(b.display_name);
                v.mime_type = std::move(b.mime_type);
                v.channel_def = std::move(b.channel_def);
                v.string_values.emplace();
                return DataChannel{std::move(v)};
            }
            if (b.data_type == DataType::Binary ||
                b.data_type == DataType::ByteArray) {
                VariableChannel v;
                v.index = b.index;
                v.name = std::move(b.name);
                v.data_type = b.data_type;
                v.physical_unit = std::move(b.physical_unit);
                v.display_name = std::move(b.display_name);
                v.mime_type = std::move(b.mime_type);
                v.channel_def = std::move(b.channel_def);
                v.binary_values.emplace();
                return DataChannel{std::move(v)};
            }
            auto empty = numeric_values_empty_for(b.data_type);
            if (!empty) return std::nullopt;
            EquidistantChannel e;
            e.index = b.index;
            e.name = std::move(b.name);
            e.data_type = b.data_type;
            e.physical_unit = std::move(b.physical_unit);
            e.display_name = std::move(b.display_name);
            e.channel_def = std::move(b.channel_def);
            e.samples = std::move(*empty);
            return DataChannel{std::move(e)};
        }
        case ChannelBuilder::State::Equidistant: {
            EquidistantChannel e;
            e.index = b.index;
            e.name = std::move(b.name);
            e.data_type = b.data_type;
            e.physical_unit = std::move(b.physical_unit);
            e.display_name = std::move(b.display_name);
            e.channel_def = std::move(b.channel_def);
            e.samples = std::move(b.eq_samples);
            e.segments = std::move(b.eq_segments);
            return DataChannel{std::move(e)};
        }
        case ChannelBuilder::State::Timestamped: {
            TimestampedChannel t;
            t.index = b.index;
            t.name = std::move(b.name);
            t.data_type = b.data_type;
            t.physical_unit = std::move(b.physical_unit);
            t.display_name = std::move(b.display_name);
            t.channel_def = std::move(b.channel_def);
            t.timestamps_ns = std::move(b.ts_timestamps_ns);
            t.values = std::move(b.ts_values);
            return DataChannel{std::move(t)};
        }
        case ChannelBuilder::State::Variable: {
            VariableChannel v;
            v.index = b.index;
            v.name = std::move(b.name);
            v.data_type = b.data_type;
            v.physical_unit = std::move(b.physical_unit);
            v.display_name = std::move(b.display_name);
            v.mime_type = std::move(b.mime_type);
            v.channel_def = std::move(b.channel_def);
            v.timestamps_ns = std::move(b.ts_timestamps_ns);
            v.string_values = std::move(b.var_strings);
            v.binary_values = std::move(b.var_binaries);
            return DataChannel{std::move(v)};
        }
    }
    return std::nullopt;
}

// Read the magic header line, then exactly `metablock_len` bytes for
// the metablock body. Returns the parsed metablock plus the
// bytes-consumed totals (header line length, metablock body length).
struct HeaderAndMetablock {
    MetaBlock meta;
    std::uint64_t header_line_bytes = 0;
    std::uint64_t metablock_bytes = 0;
};

Result<HeaderAndMetablock> parse_header_and_metablock(std::istream& in) {
    // Peek at the first two bytes to detect OSFZ. OSFZ support
    // arrives in Phase 8; until then we surface a clear error
    // rather than running the magic-header parser on compressed
    // bytes.
    int const b0 = in.peek();
    if (b0 != EOF) {
        in.get();
        int const b1 = in.peek();
        in.unget();
        std::uint8_t const u0 = static_cast<std::uint8_t>(b0);
        std::uint8_t const u1 = (b1 == EOF) ? 0 : static_cast<std::uint8_t>(b1);
        bool const is_gzip = (u0 == 0x1F && u1 == 0x8B);
        bool const is_zlib = (u0 == 0x78 &&
                              (u1 == 0x01 || u1 == 0x5E ||
                               u1 == 0x9C || u1 == 0xDA));
        if (is_gzip || is_zlib) {
            return tl::make_unexpected(Error{
                Error::Code::IoError,
                std::string{"OSFZ-compressed input detected ("} +
                    (is_gzip ? "gzip" : "zlib") +
                    "); transparent decompression arrives in Phase 8. "
                    "Decompress the file with `gunzip` or pipe through "
                    "zlib before opening."});
        }
    }

    auto hdr = parse_magic_header(in);
    if (!hdr) return tl::make_unexpected(std::move(hdr).error());

    // parse_magic_header (istream overload) leaves the cursor right
    // after the terminating newline. The header-line byte count is
    // identifier + " " + length + "\n"; recompute it from the parsed
    // version + length value so the stats are populated correctly.
    auto identifier_len = (hdr->version == OsfVersion::Osf4) ? 4 : 4;
    std::ostringstream oss;
    oss << hdr->metablock_len;
    std::uint64_t const header_line_bytes =
        static_cast<std::uint64_t>(identifier_len) + 1 +
        oss.str().size() + 1;

    std::vector<std::uint8_t> body(hdr->metablock_len);
    if (hdr->metablock_len > 0) {
        in.read(reinterpret_cast<char*>(body.data()),
                static_cast<std::streamsize>(body.size()));
        if (static_cast<std::size_t>(in.gcount()) != body.size()) {
            return tl::make_unexpected(Error{
                Error::Code::IoError,
                "short read on metablock body"});
        }
    }

    auto meta = (hdr->version == OsfVersion::Osf5)
        ? parse_metablock_json(body.data(), body.size())
        : parse_metablock_xml(body.data(), body.size());
    if (!meta) return tl::make_unexpected(std::move(meta).error());

    HeaderAndMetablock out;
    out.meta = std::move(*meta);
    out.header_line_bytes = header_line_bytes;
    out.metablock_bytes   = hdr->metablock_len;
    return out;
}

}  // anonymous namespace

// =====================================================================
// DataManager builder
// =====================================================================

Result<DataManager> build_from_stream_impl(std::istream& stream,
                                           std::uint64_t file_size,
                                           bool have_file_size) {
    auto hm = parse_header_and_metablock(stream);
    if (!hm) return tl::make_unexpected(std::move(hm).error());

    BlockReader reader(stream, hm->meta);
    if (have_file_size) reader.with_file_size(file_size);

    // Seed builders.
    std::vector<ChannelBuilder> builders;
    builders.reserve(hm->meta.channels.size());
    std::unordered_map<std::uint16_t, std::size_t> builder_by_index;
    for (auto const& ch : hm->meta.channels) {
        ChannelBuilder b;
        b.index            = ch.index;
        b.name             = ch.name;
        b.data_type        = ch.data_type;
        b.physical_unit    = ch.physical_unit;
        b.display_name     = ch.display_name;
        b.mime_type        = ch.mime_type;
        b.channel_def.channel_type         = ch.channel_type;
        b.channel_def.size_of_length_value = ch.size_of_length_value;
        b.channel_def.time_increment_ns    = ch.time_increment_ns;
        b.channel_def.reference            = ch.reference;
        b.channel_def.physical_dimension   = ch.physical_dimension;
        b.channel_def.comment              = ch.comment;
        b.channel_def.spectrum_type        = ch.spectrum_type;
        seed_initial_state(b);
        builder_by_index[ch.index] = builders.size();
        builders.push_back(std::move(b));
    }

    // Drive the reader to completion.
    for (auto& blk_r : reader) {
        if (!blk_r.has_value()) {
            return tl::make_unexpected(blk_r.error());
        }
        Block const& blk = *blk_r;
        auto it = builder_by_index.find(blk.channel_index);
        if (it == builder_by_index.end()) {
            std::ostringstream oss;
            oss << "block references unknown channel index " << blk.channel_index;
            return tl::make_unexpected(Error{
                Error::Code::UnknownChannelIndex, oss.str()});
        }
        auto r = apply_block_kind(builders[it->second], blk.kind);
        if (!r) return tl::make_unexpected(std::move(r).error());
    }

    // Finalize.
    DataManager mgr;
    mgr.meta = std::move(hm->meta);
    mgr.stats = reader.stats();
    mgr.stats.header_size_bytes    = hm->header_line_bytes;
    mgr.stats.metablock_size_bytes = hm->metablock_bytes;

    mgr.channels_.reserve(builders.size());
    for (auto& b : builders) {
        std::uint16_t const idx = b.index;
        std::string         name = b.name;  // copy for the lookup key
        auto chan = finalize_builder(std::move(b));
        if (!chan) continue;  // Unsupported — dropped
        std::size_t const slot = mgr.channels_.size();
        mgr.by_name_.emplace(std::move(name), slot);
        mgr.by_index_.emplace(idx, slot);
        mgr.channels_.push_back(std::move(*chan));
    }

    return mgr;
}

Result<DataManager> DataManager::load_from_file(
    std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::ostringstream oss;
        oss << "failed to open " << path;
        return tl::make_unexpected(Error{Error::Code::IoError, oss.str()});
    }
    std::uint64_t file_size = 0;
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    bool have_file_size = !ec;
    if (have_file_size) file_size = sz;
    return build_from_stream_impl(in, file_size, have_file_size);
}

Result<DataManager> DataManager::load_from_stream(std::istream& stream) {
    return build_from_stream_impl(stream, 0, false);
}

DataChannel const* DataManager::channel(std::string_view name) const {
    auto it = by_name_.find(std::string{name});
    if (it == by_name_.end()) return nullptr;
    return &channels_[it->second];
}

DataChannel const* DataManager::channel_by_index(std::uint16_t index) const {
    auto it = by_index_.find(index);
    if (it == by_index_.end()) return nullptr;
    return &channels_[it->second];
}

}  // namespace osf
