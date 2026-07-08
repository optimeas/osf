// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <osf/manager.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <osf/block.h>
#include <osf/compression.h>
#include <osf/header.h>
#include <osf/reader.h>
#include <osf/types.h>

#include "crc32c_p.h"

namespace osf {

// =====================================================================
// File-local helpers
// =====================================================================

namespace {

Error invalidBlock(std::string msg) {
    return Error{Error::Code::InvalidBlock, std::move(msg)};
}

Error dataTypeMismatch(std::uint16_t channel, DataType expected,
                         DataType got) {
    std::ostringstream oss;
    oss << "channel " << channel << " data type mismatch: expected "
        << static_cast<int>(expected) << ", got block payload "
        << static_cast<int>(got);
    return Error{Error::Code::DataTypeMismatch, oss.str()};
}

Error channelMixed(std::uint16_t channel) {
    std::ostringstream oss;
    oss << "channel " << channel
        << " mixes equidistant and timestamped blocks";
    return Error{Error::Code::ChannelMixedBlockTypes, oss.str()};
}

Error continuedWithoutStart(std::uint16_t channel) {
    std::ostringstream oss;
    oss << "channel " << channel
        << " produced bcContinuedData without a preceding bcStartData";
    return Error{Error::Code::ContinuedDataWithoutStart, oss.str()};
}

Error relStampWithoutAnchor(std::uint16_t channel) {
    std::ostringstream oss;
    oss << "channel " << channel
        << " produced bcContinuedRelStampData without an absolute anchor";
    return Error{Error::Code::RelStampWithoutAnchor, oss.str()};
}

DataType numericPayloadDataType(NumericPayload const& p) noexcept {
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

DataType timestampedPayloadDataType(TimestampedPayload const& p) noexcept {
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

DataType relTimestampedPayloadDataType(
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
Result<std::size_t> extendNumeric(NumericValues& target,
                                   NumericPayload payload,
                                   std::uint16_t channel) {
    DataType const targetDt  = numericValuesDataType(target);
    DataType const payloadDt = numericPayloadDataType(payload);
    if (payloadDt != targetDt) {
        return tl::make_unexpected(
            dataTypeMismatch(channel, targetDt, payloadDt));
    }
    std::size_t appended = 0;
    std::visit([&](auto&& srcVec) {
        using V = std::decay_t<decltype(srcVec)>;
        auto* dst = std::get_if<V>(&target);
        if (!dst) return;  // unreachable: type-check above guarantees match
        appended = srcVec.size();
        dst->insert(dst->end(),
                    std::make_move_iterator(srcVec.begin()),
                    std::make_move_iterator(srcVec.end()));
    }, std::move(payload));
    return appended;
}

// Compute the last absolute timestamp produced by a segment with
// `(startTimestampNs, sampleRateHz, sampleCount)`. Mirrors the
// Rust update_last_ts_from_segment helper.
std::int64_t segmentLastTimestamp(std::int64_t startTs, double rate,
                                    std::size_t sampleCount) noexcept {
    if (sampleCount == 0) return startTs;
    if (rate > 0.0) {
        double const offset =
            (static_cast<double>(sampleCount - 1) / rate) * 1.0e9;
        return startTs + static_cast<std::int64_t>(offset);
    }
    return startTs;
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
    DataType dataType = DataType::Unsupported;
    std::optional<std::string> physicalUnit;
    std::optional<std::string> displayName;
    std::optional<std::string> mimeType;
    ChannelMeta channelDef;

    // Anchor for bcContinuedRelStampData (carries deltas).
    std::optional<std::int64_t> lastTimestampNs;

    // Equidistant storage.
    NumericValues eqSamples{std::vector<double>{}};
    std::vector<Segment> eqSegments;

    // Timestamped storage.
    std::vector<std::int64_t> tsTimestampsNs;
    NumericValues tsValues{std::vector<double>{}};

    // Variable storage.
    std::optional<std::vector<std::string>> varStrings;
    std::optional<std::vector<std::vector<std::uint8_t>>> varBinaries;
};

void seedInitialState(ChannelBuilder& b) {
    if (b.dataType == DataType::Unsupported ||
        b.channelDef.channelType == ChannelType::Unsupported) {
        b.state = ChannelBuilder::State::Unsupported;
        return;
    }
    if (b.dataType == DataType::String) {
        b.state = ChannelBuilder::State::Variable;
        b.varStrings.emplace();
        return;
    }
    if (b.dataType == DataType::Binary || b.dataType == DataType::ByteArray) {
        b.state = ChannelBuilder::State::Variable;
        b.varBinaries.emplace();
        return;
    }
    b.state = ChannelBuilder::State::Pending;
}

Result<void> applyStart(ChannelBuilder& b, StartData payload) {
    DataType const payloadDt = numericPayloadDataType(payload.samples);
    if (payloadDt != b.dataType) {
        return tl::make_unexpected(
            dataTypeMismatch(b.index, b.dataType, payloadDt));
    }

    switch (b.state) {
        case ChannelBuilder::State::Pending: {
            auto empty = numericValuesEmptyFor(b.dataType);
            if (!empty) {
                return tl::make_unexpected(invalidBlock(
                    "channel cannot hold equidistant samples"));
            }
            b.eqSamples = std::move(*empty);
            auto appended = extendNumeric(b.eqSamples,
                                           std::move(payload.samples), b.index);
            if (!appended) return tl::make_unexpected(std::move(appended).error());
            Segment seg;
            seg.startTimestampNs = payload.startTimestampNs;
            seg.sampleRateHz     = payload.sampleRateHz;
            seg.startIndex        = 0;
            seg.sampleCount       = *appended;
            b.eqSegments.clear();
            b.eqSegments.push_back(seg);
            b.state = ChannelBuilder::State::Equidistant;
            b.lastTimestampNs = segmentLastTimestamp(
                payload.startTimestampNs, payload.sampleRateHz, *appended);
            return {};
        }
        case ChannelBuilder::State::Equidistant: {
            std::size_t const startIndex = numericValuesLen(b.eqSamples);
            auto appended = extendNumeric(b.eqSamples,
                                           std::move(payload.samples), b.index);
            if (!appended) return tl::make_unexpected(std::move(appended).error());
            Segment seg;
            seg.startTimestampNs = payload.startTimestampNs;
            seg.sampleRateHz     = payload.sampleRateHz;
            seg.startIndex        = startIndex;
            seg.sampleCount       = *appended;
            b.eqSegments.push_back(seg);
            b.lastTimestampNs = segmentLastTimestamp(
                payload.startTimestampNs, payload.sampleRateHz, *appended);
            return {};
        }
        case ChannelBuilder::State::Timestamped:
        case ChannelBuilder::State::Variable:
            return tl::make_unexpected(channelMixed(b.index));
        case ChannelBuilder::State::Unsupported:
            return {};
    }
    return {};
}

Result<void> applyContinued(ChannelBuilder& b, ContinuedData payload) {
    DataType const payloadDt = numericPayloadDataType(payload.samples);
    if (payloadDt != b.dataType) {
        return tl::make_unexpected(
            dataTypeMismatch(b.index, b.dataType, payloadDt));
    }
    switch (b.state) {
        case ChannelBuilder::State::Pending:
            return tl::make_unexpected(continuedWithoutStart(b.index));
        case ChannelBuilder::State::Equidistant: {
            if (b.eqSegments.empty()) {
                return tl::make_unexpected(continuedWithoutStart(b.index));
            }
            auto appended = extendNumeric(b.eqSamples,
                                           std::move(payload.samples), b.index);
            if (!appended) return tl::make_unexpected(std::move(appended).error());
            auto& last = b.eqSegments.back();
            last.sampleCount += *appended;
            b.lastTimestampNs = segmentLastTimestamp(
                last.startTimestampNs, last.sampleRateHz, last.sampleCount);
            return {};
        }
        case ChannelBuilder::State::Timestamped:
        case ChannelBuilder::State::Variable:
            return tl::make_unexpected(channelMixed(b.index));
        case ChannelBuilder::State::Unsupported:
            return {};
    }
    return {};
}

// Routes an AbsTimestampData payload into the Timestamped (numeric or
// GPS) or Variable (string / binary) storage as appropriate.
Result<void> applyAbsTimestamped(ChannelBuilder& b,
                                   AbsTimestampData payload) {
    DataType const payloadDt = timestampedPayloadDataType(payload.samples);
    if (payloadDt != b.dataType) {
        return tl::make_unexpected(
            dataTypeMismatch(b.index, b.dataType, payloadDt));
    }

    switch (b.state) {
        case ChannelBuilder::State::Pending: {
            // Lock to Timestamped (numeric / GPS). String / binary
            // would have been seeded into the Variable state by
            // seedInitialState, so reaching Pending here implies
            // the payload is one of the numeric / GPS variants.
            auto empty = numericValuesEmptyFor(b.dataType);
            if (!empty) {
                return tl::make_unexpected(invalidBlock(
                    "unexpected variable-length payload in numeric "
                    "timestamped init"));
            }
            b.tsValues = std::move(*empty);
            b.tsTimestampsNs.clear();
            // Fallthrough into the Timestamped-extend block below.
            b.state = ChannelBuilder::State::Timestamped;
            [[fallthrough]];
        }
        case ChannelBuilder::State::Timestamped: {
            DataType const targetDt = numericValuesDataType(b.tsValues);
            if (payloadDt != targetDt) {
                return tl::make_unexpected(
                    dataTypeMismatch(b.index, targetDt, payloadDt));
            }
            std::size_t appended = 0;
            std::visit([&](auto&& srcVec) {
                using V = std::decay_t<decltype(srcVec)>;
                using Pair = typename V::value_type;
                using T = typename Pair::second_type;
                if constexpr (std::is_same_v<T, std::string> ||
                              std::is_same_v<T, std::vector<std::uint8_t>>) {
                    // Variable types should never reach the Timestamped
                    // branch — the data-type check above guarantees it.
                    return;
                } else {
                    auto* dst =
                        std::get_if<std::vector<T>>(&b.tsValues);
                    if (!dst) return;
                    appended = srcVec.size();
                    for (auto& [ts, value] : srcVec) {
                        b.tsTimestampsNs.push_back(ts);
                        dst->push_back(std::move(value));
                    }
                }
            }, std::move(payload.samples));
            if (appended > 0) {
                b.lastTimestampNs = b.tsTimestampsNs.back();
            }
            return {};
        }
        case ChannelBuilder::State::Variable: {
            // String XOR binary based on the channel's declared
            // dataType — payload variant must match.
            std::size_t appended = 0;
            bool mismatched = false;
            std::visit([&](auto&& srcVec) {
                using V = std::decay_t<decltype(srcVec)>;
                using Pair = typename V::value_type;
                using T = typename Pair::second_type;
                if constexpr (std::is_same_v<T, std::string>) {
                    if (b.dataType != DataType::String || !b.varStrings) {
                        mismatched = true;
                        return;
                    }
                    appended = srcVec.size();
                    for (auto& [ts, value] : srcVec) {
                        b.tsTimestampsNs.push_back(ts);
                        b.varStrings->push_back(std::move(value));
                    }
                } else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) {
                    bool const ok = (b.dataType == DataType::Binary ||
                                      b.dataType == DataType::ByteArray) &&
                                     b.varBinaries;
                    if (!ok) {
                        mismatched = true;
                        return;
                    }
                    appended = srcVec.size();
                    for (auto& [ts, value] : srcVec) {
                        b.tsTimestampsNs.push_back(ts);
                        b.varBinaries->push_back(std::move(value));
                    }
                } else {
                    // Numeric / GPS on a Variable channel — mismatch.
                    mismatched = true;
                }
            }, std::move(payload.samples));
            if (mismatched) {
                return tl::make_unexpected(
                    dataTypeMismatch(b.index, b.dataType, payloadDt));
            }
            if (appended > 0) {
                b.lastTimestampNs = b.tsTimestampsNs.back();
            }
            return {};
        }
        case ChannelBuilder::State::Equidistant:
            return tl::make_unexpected(channelMixed(b.index));
        case ChannelBuilder::State::Unsupported:
            return {};
    }
    return {};
}

Result<void> applyRelTimestamped(ChannelBuilder& b,
                                   ContinuedRelStampData payload) {
    DataType const payloadDt = relTimestampedPayloadDataType(payload.samples);
    if (payloadDt != b.dataType) {
        return tl::make_unexpected(
            dataTypeMismatch(b.index, b.dataType, payloadDt));
    }
    if (!b.lastTimestampNs) {
        return tl::make_unexpected(relStampWithoutAnchor(b.index));
    }

    if (b.state != ChannelBuilder::State::Timestamped) {
        if (b.state == ChannelBuilder::State::Unsupported) return {};
        return tl::make_unexpected(channelMixed(b.index));
    }

    DataType const targetDt = numericValuesDataType(b.tsValues);
    if (payloadDt != targetDt) {
        return tl::make_unexpected(
            dataTypeMismatch(b.index, targetDt, payloadDt));
    }

    std::int64_t anchor = *b.lastTimestampNs;
    std::visit([&](auto&& srcVec) {
        using V = std::decay_t<decltype(srcVec)>;
        using Pair = typename V::value_type;
        using T = typename Pair::second_type;
        auto* dst = std::get_if<std::vector<T>>(&b.tsValues);
        if (!dst) return;
        for (auto& [delta, value] : srcVec) {
            anchor += static_cast<std::int64_t>(delta);
            b.tsTimestampsNs.push_back(anchor);
            dst->push_back(std::move(value));
        }
    }, std::move(payload.samples));
    b.lastTimestampNs = anchor;
    return {};
}

Result<void> applyBlockKind(ChannelBuilder& b, BlockKind kind) {
    return std::visit([&](auto&& specific) -> Result<void> {
        using K = std::decay_t<decltype(specific)>;
        if constexpr (std::is_same_v<K, StartData>) {
            return applyStart(b, std::move(specific));
        } else if constexpr (std::is_same_v<K, ContinuedData>) {
            return applyContinued(b, std::move(specific));
        } else if constexpr (std::is_same_v<K, AbsTimestampData>) {
            return applyAbsTimestamped(b, std::move(specific));
        } else if constexpr (std::is_same_v<K, ContinuedRelStampData>) {
            return applyRelTimestamped(b, std::move(specific));
        } else {
            // Skipped — nothing to apply.
            (void) specific;
            return {};
        }
    }, std::move(kind));
}

// Finalize a builder into a typed DataChannel. Returns std::nullopt
// for Unsupported (channel is dropped from the final list).
std::optional<DataChannel> finalizeBuilder(ChannelBuilder&& b) {
    switch (b.state) {
        case ChannelBuilder::State::Unsupported:
            return std::nullopt;
        case ChannelBuilder::State::Pending: {
            // Channel declared but no typed block arrived — emit an
            // empty channel matching the metablock's data-type group.
            if (b.dataType == DataType::String) {
                VariableChannel v;
                v.index = b.index;
                v.name = std::move(b.name);
                v.dataType = b.dataType;
                v.physicalUnit = std::move(b.physicalUnit);
                v.displayName = std::move(b.displayName);
                v.mimeType = std::move(b.mimeType);
                v.channelDef = std::move(b.channelDef);
                v.stringValues.emplace();
                return DataChannel{std::move(v)};
            }
            if (b.dataType == DataType::Binary ||
                b.dataType == DataType::ByteArray) {
                VariableChannel v;
                v.index = b.index;
                v.name = std::move(b.name);
                v.dataType = b.dataType;
                v.physicalUnit = std::move(b.physicalUnit);
                v.displayName = std::move(b.displayName);
                v.mimeType = std::move(b.mimeType);
                v.channelDef = std::move(b.channelDef);
                v.binaryValues.emplace();
                return DataChannel{std::move(v)};
            }
            auto empty = numericValuesEmptyFor(b.dataType);
            if (!empty) return std::nullopt;
            EquidistantChannel e;
            e.index = b.index;
            e.name = std::move(b.name);
            e.dataType = b.dataType;
            e.physicalUnit = std::move(b.physicalUnit);
            e.displayName = std::move(b.displayName);
            e.channelDef = std::move(b.channelDef);
            e.samples = std::move(*empty);
            return DataChannel{std::move(e)};
        }
        case ChannelBuilder::State::Equidistant: {
            EquidistantChannel e;
            e.index = b.index;
            e.name = std::move(b.name);
            e.dataType = b.dataType;
            e.physicalUnit = std::move(b.physicalUnit);
            e.displayName = std::move(b.displayName);
            e.channelDef = std::move(b.channelDef);
            e.samples = std::move(b.eqSamples);
            e.segments = std::move(b.eqSegments);
            return DataChannel{std::move(e)};
        }
        case ChannelBuilder::State::Timestamped: {
            TimestampedChannel t;
            t.index = b.index;
            t.name = std::move(b.name);
            t.dataType = b.dataType;
            t.physicalUnit = std::move(b.physicalUnit);
            t.displayName = std::move(b.displayName);
            t.channelDef = std::move(b.channelDef);
            t.timestampsNs = std::move(b.tsTimestampsNs);
            t.values = std::move(b.tsValues);
            return DataChannel{std::move(t)};
        }
        case ChannelBuilder::State::Variable: {
            VariableChannel v;
            v.index = b.index;
            v.name = std::move(b.name);
            v.dataType = b.dataType;
            v.physicalUnit = std::move(b.physicalUnit);
            v.displayName = std::move(b.displayName);
            v.mimeType = std::move(b.mimeType);
            v.channelDef = std::move(b.channelDef);
            v.timestampsNs = std::move(b.tsTimestampsNs);
            v.stringValues = std::move(b.varStrings);
            v.binaryValues = std::move(b.varBinaries);
            return DataChannel{std::move(v)};
        }
    }
    return std::nullopt;
}

// Read the magic header line, then exactly `metablockLen` bytes for
// the metablock body. Returns the parsed metablock plus the
// bytes-consumed totals (header line length, metablock body length).
struct HeaderAndMetablock {
    MetaBlock meta;
    std::uint64_t headerLineBytes = 0;
    std::uint64_t metablockBytes = 0;
    IntegrityProfile integrity = IntegrityProfile::None;
};

Result<HeaderAndMetablock> parseHeaderAndMetablock(std::istream& in) {
    // OSFZ (gzip / zlib) decompression is applied transparently before
    // this point (buildFromStreamImpl wraps the source in a
    // DecompressingIStream), so `in` is always a plain OSF byte stream
    // here.
    auto hdr = parseMagicHeader(in);
    if (!hdr) return tl::make_unexpected(std::move(hdr).error());

    // parseMagicHeader (istream overload) leaves the cursor right
    // after the terminating newline. The header-line byte count is
    // identifier + " " + length + "\n"; recompute it from the parsed
    // version + length value so the stats are populated correctly.
    auto identifierLen = (hdr->version == OsfVersion::Osf4) ? 4 : 4;
    std::ostringstream oss;
    oss << hdr->metablockLen;
    std::uint64_t const headerLineBytes =
        static_cast<std::uint64_t>(identifierLen) + 1 +
        oss.str().size() + 1;

    std::vector<std::uint8_t> body(hdr->metablockLen);
    if (hdr->metablockLen > 0) {
        in.read(reinterpret_cast<char*>(body.data()),
                static_cast<std::streamsize>(body.size()));
        if (static_cast<std::size_t>(in.gcount()) != body.size()) {
            return tl::make_unexpected(Error{
                Error::Code::IoError,
                "short read on metablock body"});
        }
    }

    // Metablock CRC (integrity level crc): verify the raw metablock bytes
    // against the crc32c header token before parsing. A mismatch rejects the
    // file — nothing after the metablock is interpretable without it.
    if (hdr->metablockCrc.has_value()) {
        std::uint32_t const actual = osf::detail::crc32c(body.data(), body.size());
        if (actual != *hdr->metablockCrc) {
            return tl::make_unexpected(Error{
                Error::Code::MetablockCrcMismatch,
                "metablock CRC mismatch: the crc32c header token does not match "
                "the metablock bytes"});
        }
    }

    auto meta = (hdr->version == OsfVersion::Osf5)
        ? parseMetablockJson(body.data(), body.size())
        : parseMetablockXml(body.data(), body.size());
    if (!meta) return tl::make_unexpected(std::move(meta).error());

    HeaderAndMetablock out;
    out.meta = std::move(*meta);
    out.headerLineBytes = headerLineBytes;
    out.metablockBytes   = hdr->metablockLen;
    out.integrity        = hdr->integrity;
    return out;
}

}  // anonymous namespace

// =====================================================================
// DataManager builder
// =====================================================================

Result<DataManager> buildFromStreamImpl(std::istream& stream,
                                           std::uint64_t fileSize,
                                           bool haveFileSize) {
    // Transparent OSFZ (gzip / zlib) decompression: detection is by the
    // leading two bytes; a plain OSF stream passes through verbatim. The
    // rest of the read stack consumes the decompressed bytes unchanged.
    DecompressingIStream input(stream);

    auto hm = parseHeaderAndMetablock(input);
    if (!hm) return tl::make_unexpected(std::move(hm).error());

    BlockReader reader(input, hm->meta);
    reader.withIntegrity(hm->integrity);
    // fileSize is telemetry only (BlockReader does not use it); the
    // value is the source file size, which for OSFZ is the compressed
    // size — still the meaningful "file size" to report.
    if (haveFileSize) reader.withFileSize(fileSize);

    // Seed builders.
    std::vector<ChannelBuilder> builders;
    builders.reserve(hm->meta.channels.size());
    std::unordered_map<std::uint16_t, std::size_t> builderByIndex;
    for (auto const& ch : hm->meta.channels) {
        ChannelBuilder b;
        b.index            = ch.index;
        b.name             = ch.name;
        b.dataType        = ch.dataType;
        b.physicalUnit    = ch.physicalUnit;
        b.displayName     = ch.displayName;
        b.mimeType        = ch.mimeType;
        b.channelDef.channelType         = ch.channelType;
        b.channelDef.sizeOfLengthValue = ch.sizeOfLengthValue;
        b.channelDef.timeIncrementNs    = ch.timeIncrementNs;
        b.channelDef.reference            = ch.reference;
        b.channelDef.physicalDimension   = ch.physicalDimension;
        b.channelDef.comment              = ch.comment;
        b.channelDef.spectrumType        = ch.spectrumType;
        seedInitialState(b);
        builderByIndex[ch.index] = builders.size();
        builders.push_back(std::move(b));
    }

    // Drive the reader to completion.
    for (auto& blkR : reader) {
        if (!blkR.has_value()) {
            return tl::make_unexpected(blkR.error());
        }
        Block const& blk = *blkR;
        auto it = builderByIndex.find(blk.channelIndex);
        if (it == builderByIndex.end()) {
            std::ostringstream oss;
            oss << "block references unknown channel index " << blk.channelIndex;
            return tl::make_unexpected(Error{
                Error::Code::UnknownChannelIndex, oss.str()});
        }
        auto r = applyBlockKind(builders[it->second], blk.kind);
        if (!r) return tl::make_unexpected(std::move(r).error());
    }

    // Finalize.
    DataManager mgr;
    mgr.meta = std::move(hm->meta);
    mgr.stats = reader.stats();
    mgr.stats.headerSizeBytes    = hm->headerLineBytes;
    mgr.stats.metablockSizeBytes = hm->metablockBytes;
    mgr.stats.compressed           = input.isCompressed();
    mgr.stats.compressionFormat   = input.format();

    mgr.m_channels.reserve(builders.size());
    for (auto& b : builders) {
        std::uint16_t const idx = b.index;
        std::string         name = b.name;  // copy for the lookup key
        auto chan = finalizeBuilder(std::move(b));
        if (!chan) continue;  // Unsupported — dropped
        std::size_t const slot = mgr.m_channels.size();
        mgr.m_byName.emplace(std::move(name), slot);
        mgr.m_byIndex.emplace(idx, slot);
        mgr.m_channels.push_back(std::move(*chan));
    }

    return mgr;
}

Result<DataManager> DataManager::loadFromFile(
    std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::ostringstream oss;
        oss << "failed to open " << path;
        return tl::make_unexpected(Error{Error::Code::IoError, oss.str()});
    }
    std::uint64_t fileSize = 0;
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    bool haveFileSize = !ec;
    if (haveFileSize) fileSize = sz;
    return buildFromStreamImpl(in, fileSize, haveFileSize);
}

Result<DataManager> DataManager::loadFromStream(std::istream& stream) {
    return buildFromStreamImpl(stream, 0, false);
}

DataChannel const* DataManager::channel(std::string_view name) const {
    auto it = m_byName.find(std::string{name});
    if (it == m_byName.end()) return nullptr;
    return &m_channels[it->second];
}

DataChannel const* DataManager::channelByIndex(std::uint16_t index) const {
    auto it = m_byIndex.find(index);
    if (it == m_byIndex.end()) return nullptr;
    return &m_channels[it->second];
}

}  // namespace osf
