// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <osf/reader.h>

#include "binaryio_p.h"
#include "crc32c_p.h"

#include <cstring>
#include <istream>
#include <sstream>
#include <string>
#include <utility>

namespace osf {

// =====================================================================
// File-local helpers
// =====================================================================

namespace {

Error invalidBlock(std::string msg) {
    return Error{Error::Code::InvalidBlock, std::move(msg)};
}

Error ioError(std::string msg) {
    return Error{Error::Code::IoError, std::move(msg)};
}

// Pull `len` bytes from the stream. Returns:
// - `Ok(true)`  — full `len` bytes consumed.
// - `Ok(false)` — stream truncated; reached EOF early.
// - `Err`       — non-EOF I/O failure.
Result<bool> streamReadN(std::istream& s, void* dst, std::streamsize len) {
    if (len == 0) return true;
    s.read(static_cast<char*>(dst), len);
    if (s.gcount() == len) return true;
    if (s.eof()) {
        s.clear();  // reset eofbit so subsequent calls behave predictably
        return false;
    }
    return tl::make_unexpected(ioError("istream::read failed before EOF"));
}

// ---------------------------------------------------------------------
// Cursor over an in-memory payload. Carries a position; every accessor
// either advances by the requested width and returns the value, or
// returns std::nullopt on overflow.
// ---------------------------------------------------------------------

class PayloadCursor {
public:
    PayloadCursor(std::uint8_t const* data, std::size_t size) noexcept
        : m_data(data), m_size(size) {}

    [[nodiscard]] std::size_t remaining() const noexcept { return m_size - m_pos; }
    [[nodiscard]] std::size_t pos() const noexcept { return m_pos; }
    [[nodiscard]] std::uint8_t const* tail() const noexcept { return m_data + m_pos; }

    std::optional<std::uint8_t> readU8() {
        if (remaining() < 1) return std::nullopt;
        return m_data[m_pos++];
    }
    std::optional<std::uint16_t> readU16() {
        if (remaining() < 2) return std::nullopt;
        auto v = osf::detail::readLeU16(m_data + m_pos); m_pos += 2; return v;
    }
    std::optional<std::uint32_t> readU32() {
        if (remaining() < 4) return std::nullopt;
        auto v = osf::detail::readLeU32(m_data + m_pos); m_pos += 4; return v;
    }
    std::optional<std::uint64_t> readU64() {
        if (remaining() < 8) return std::nullopt;
        auto v = osf::detail::readLeU64(m_data + m_pos); m_pos += 8; return v;
    }
    std::optional<std::int8_t>  readI8() {
        auto v = readU8();
        return v ? std::optional<std::int8_t>{static_cast<std::int8_t>(*v)}
                 : std::nullopt;
    }
    std::optional<std::int16_t> readI16() {
        auto v = readU16();
        return v ? std::optional<std::int16_t>{static_cast<std::int16_t>(*v)}
                 : std::nullopt;
    }
    std::optional<std::int32_t> readI32() {
        auto v = readU32();
        return v ? std::optional<std::int32_t>{static_cast<std::int32_t>(*v)}
                 : std::nullopt;
    }
    std::optional<std::int64_t> readI64() {
        auto v = readU64();
        return v ? std::optional<std::int64_t>{static_cast<std::int64_t>(*v)}
                 : std::nullopt;
    }
    std::optional<float>  readF32() {
        if (remaining() < 4) return std::nullopt;
        auto v = osf::detail::readLeF32(m_data + m_pos); m_pos += 4; return v;
    }
    std::optional<double> readF64() {
        if (remaining() < 8) return std::nullopt;
        auto v = osf::detail::readLeF64(m_data + m_pos); m_pos += 8; return v;
    }
    std::optional<bool> readBool() {
        auto v = readU8();
        return v ? std::optional<bool>{*v != 0} : std::nullopt;
    }

private:
    std::uint8_t const* m_data;
    std::size_t m_size;
    std::size_t m_pos = 0;
};

// ---------------------------------------------------------------------
// Sample-count reader. !multi → implicit N = 1; multi → u32 prefix.
// ---------------------------------------------------------------------

Result<std::size_t> readSampleCount(PayloadCursor& cur, bool multi) {
    if (!multi) return std::size_t{1};
    auto n = cur.readU32();
    if (!n) return tl::make_unexpected(invalidBlock("multi-sample N: short read"));
    return static_cast<std::size_t>(*n);
}

// ---------------------------------------------------------------------
// Numeric typed-run reader for bcStartData / bcContinuedData. Reads
// `n` samples of the channel's DataType and wraps them in a
// NumericPayload variant. Non-numeric types reject.
// ---------------------------------------------------------------------

#define OSF_READ_RUN(VARIANT, READ_EXPR)                          \
    {                                                             \
        std::vector<VARIANT> v;                                   \
        v.reserve(n);                                             \
        for (std::size_t i = 0; i < n; ++i) {                     \
            auto x = (READ_EXPR);                                 \
            if (!x) return tl::make_unexpected(invalidBlock(     \
                "numeric run: short read"));                      \
            v.push_back(*x);                                      \
        }                                                         \
        return NumericPayload{std::move(v)};                      \
    }

Result<NumericPayload> readNumericN(PayloadCursor& cur, DataType dt,
                                     std::size_t n) {
    switch (dt) {
        case DataType::Bool:   OSF_READ_RUN(bool,             cur.readBool())
        case DataType::Int8:   OSF_READ_RUN(std::int8_t,      cur.readI8())
        case DataType::Int16:  OSF_READ_RUN(std::int16_t,     cur.readI16())
        case DataType::Int32:  OSF_READ_RUN(std::int32_t,     cur.readI32())
        case DataType::Int64:  OSF_READ_RUN(std::int64_t,     cur.readI64())
        case DataType::UInt8:  OSF_READ_RUN(std::uint8_t,     cur.readU8())
        case DataType::UInt16: OSF_READ_RUN(std::uint16_t,    cur.readU16())
        case DataType::UInt32: OSF_READ_RUN(std::uint32_t,    cur.readU32())
        case DataType::UInt64: OSF_READ_RUN(std::uint64_t,    cur.readU64())
        case DataType::Float:  OSF_READ_RUN(float,            cur.readF32())
        case DataType::Double: OSF_READ_RUN(double,           cur.readF64())
        default:
            return tl::make_unexpected(invalidBlock(
                "equidistant blocks (bcStartData / bcContinuedData) only "
                "support numeric datatypes"));
    }
}

#undef OSF_READ_RUN

// ---------------------------------------------------------------------
// bcAbsTimeStampData parser. Handles string/binary specially per spec.
// ---------------------------------------------------------------------

#define OSF_READ_TS_PAIRS(VARIANT, READ_EXPR)                            \
    {                                                                    \
        std::vector<std::pair<std::int64_t, VARIANT>> v;                 \
        v.reserve(n);                                                    \
        for (std::size_t i = 0; i < n; ++i) {                            \
            auto ts = cur.readI64();                                     \
            if (!ts) return tl::make_unexpected(invalidBlock(           \
                "AbsTs ts: short read"));                                \
            auto value = (READ_EXPR);                                    \
            if (!value) return tl::make_unexpected(invalidBlock(        \
                "AbsTs value: short read"));                             \
            v.emplace_back(*ts, *value);                                 \
        }                                                                \
        return TimestampedPayload{std::move(v)};                         \
    }

// Spec rev 2026-05-24 — version-deterministic null-terminator rule.
//
// - OSF4: strip the last byte of `[p, p+n)` unconditionally. The byte
//   is guaranteed to be `0x00` per spec; if a writer is non-conforming
//   and emits a non-zero last byte, that byte is silently discarded.
//   The reader does not validate it because the rule is deterministic
//   and there is no fallback path to take.
// - OSF5: return `[p, p+n)` unchanged. A trailing `0x00` is a regular
//   data byte, not a sentinel.
std::vector<std::uint8_t> stripOsf4Terminator(std::uint8_t const* p,
                                                std::size_t n,
                                                OsfVersion osf_version) {
    if (osf_version == OsfVersion::Osf4 && n > 0) {
        return std::vector<std::uint8_t>(p, p + n - 1);
    }
    return std::vector<std::uint8_t>(p, p + n);
}

Result<TimestampedPayload> buildStringOrBinary(
    DataType dt, std::vector<std::pair<std::int64_t,
                                       std::vector<std::uint8_t>>>&& raw) {
    if (dt == DataType::String) {
        std::vector<std::pair<std::int64_t, std::string>> decoded;
        decoded.reserve(raw.size());
        for (auto& [ts, bytes] : raw) {
            // We do not enforce UTF-8 validity here. The Rust reference
            // returns an error on invalid UTF-8; field files predating
            // spec rev 2026-05-04 occasionally carry CP1252 payloads,
            // so leniency is the more useful default for now.
            decoded.emplace_back(
                ts, std::string{reinterpret_cast<char const*>(bytes.data()),
                                bytes.size()});
        }
        return TimestampedPayload{std::move(decoded)};
    }
    // Binary / ByteArray fall through.
    return TimestampedPayload{std::move(raw)};
}

// Null-terminator handling is version-deterministic per spec rev
// 2026-05-24: OSF4 strips the last byte of every per-sample payload
// unconditionally; OSF5 keeps the payload verbatim. See
// stripOsf4Terminator for the rationale.
Result<TimestampedPayload> parseAbsTsStringOrBinary(
    std::uint8_t const* body, std::size_t bodyLen, DataType dt, bool multi,
    OsfVersion osf_version) {
    std::size_t n = 1;
    std::size_t restOff = 0;

    if (multi) {
        if (bodyLen < 4) {
            return tl::make_unexpected(invalidBlock(
                "AbsTs string/binary N: short read"));
        }
        std::uint32_t const raw = osf::detail::readLeU32(body);
        if (raw == 0) {
            return buildStringOrBinary(dt, {});
        }
        n = static_cast<std::size_t>(raw);
        restOff = 4;
    }
    // !multi: bit-7 = 0 is the spec-canonical single-sample form
    // (implicit N=1, 4 bytes shorter than the bit-7 = 1 variant
    // with an explicit [u32 N] prefix). Both forms are valid; both
    // the C++ encoder and the Rust writer emit the canonical
    // bit-7 = 0 form, and either is accepted on input.

    std::size_t const restLen = bodyLen - restOff;
    std::uint8_t const* rest = body + restOff;

    if (n == 1) {
        if (restLen < 8) {
            return tl::make_unexpected(invalidBlock(
                "AbsTs string/binary ts: short read"));
        }
        std::int64_t const ts = osf::detail::readLeI64(rest);
        auto payload = stripOsf4Terminator(rest + 8, restLen - 8, osf_version);
        std::vector<std::pair<std::int64_t, std::vector<std::uint8_t>>> raw;
        raw.emplace_back(ts, std::move(payload));
        return buildStringOrBinary(dt, std::move(raw));
    }

    if (restLen % n != 0) {
        // Spec mandates equal-length segments; if the body length is
        // not divisible by N we cannot split it. Fall back to a single
        // sample (mirrors the Rust reference's warn-and-degrade path).
        if (restLen < 8) {
            return tl::make_unexpected(invalidBlock(
                "AbsTs string/binary ts: short read"));
        }
        std::int64_t const ts = osf::detail::readLeI64(rest);
        auto payload = stripOsf4Terminator(rest + 8, restLen - 8, osf_version);
        std::vector<std::pair<std::int64_t, std::vector<std::uint8_t>>> raw;
        raw.emplace_back(ts, std::move(payload));
        return buildStringOrBinary(dt, std::move(raw));
    }

    std::size_t const perSample = restLen / n;
    // OSF4 needs i64 ts (8) + at least one byte (the terminator alone
    // is a legal empty payload) = 9. OSF5 needs only i64 ts = 8.
    std::size_t const minPerSample =
        (osf_version == OsfVersion::Osf4) ? 9u : 8u;
    if (perSample < minPerSample) {
        std::ostringstream oss;
        oss << "AbsTs string/binary N=" << n
            << ": per-sample size " << perSample
            << " is less than " << minPerSample
            << " (need i64 ts";
        if (osf_version == OsfVersion::Osf4) {
            oss << " + at least the OSF4 null terminator";
        }
        oss << ")";
        return tl::make_unexpected(invalidBlock(oss.str()));
    }

    std::vector<std::pair<std::int64_t, std::vector<std::uint8_t>>> raw;
    raw.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::uint8_t const* chunk = rest + i * perSample;
        std::int64_t const ts = osf::detail::readLeI64(chunk);
        auto payload = stripOsf4Terminator(chunk + 8, perSample - 8,
                                             osf_version);
        raw.emplace_back(ts, std::move(payload));
    }
    return buildStringOrBinary(dt, std::move(raw));
}

Result<TimestampedPayload> parseAbsTimestampData(
    std::uint8_t const* body, std::size_t bodyLen, DataType dt, bool multi,
    OsfVersion osf_version) {
    if (dt == DataType::String || dt == DataType::Binary ||
        dt == DataType::ByteArray) {
        return parseAbsTsStringOrBinary(
            body, bodyLen, dt == DataType::ByteArray ? DataType::Binary : dt,
            multi, osf_version);
    }

    PayloadCursor cur{body, bodyLen};
    auto nR = readSampleCount(cur, multi);
    if (!nR) return tl::make_unexpected(std::move(nR).error());
    std::size_t const n = *nR;

    switch (dt) {
        case DataType::Bool:   OSF_READ_TS_PAIRS(bool,           cur.readBool())
        case DataType::Int8:   OSF_READ_TS_PAIRS(std::int8_t,    cur.readI8())
        case DataType::Int16:  OSF_READ_TS_PAIRS(std::int16_t,   cur.readI16())
        case DataType::Int32:  OSF_READ_TS_PAIRS(std::int32_t,   cur.readI32())
        case DataType::Int64:  OSF_READ_TS_PAIRS(std::int64_t,   cur.readI64())
        case DataType::UInt8:  OSF_READ_TS_PAIRS(std::uint8_t,   cur.readU8())
        case DataType::UInt16: OSF_READ_TS_PAIRS(std::uint16_t,  cur.readU16())
        case DataType::UInt32: OSF_READ_TS_PAIRS(std::uint32_t,  cur.readU32())
        case DataType::UInt64: OSF_READ_TS_PAIRS(std::uint64_t,  cur.readU64())
        case DataType::Float:  OSF_READ_TS_PAIRS(float,          cur.readF32())
        case DataType::Double: OSF_READ_TS_PAIRS(double,         cur.readF64())
        case DataType::GpsLocation: {
            std::vector<std::pair<std::int64_t, GpsLocation>> v;
            v.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                auto ts = cur.readI64();
                if (!ts) return tl::make_unexpected(invalidBlock(
                    "AbsTs gps ts: short read"));
                auto lat = cur.readF64();
                auto lon = cur.readF64();
                auto alt = cur.readF64();
                if (!lat || !lon || !alt) {
                    return tl::make_unexpected(invalidBlock(
                        "AbsTs gps payload: short read"));
                }
                v.emplace_back(*ts, GpsLocation{*lat, *lon, *alt});
            }
            return TimestampedPayload{std::move(v)};
        }
        case DataType::String:
        case DataType::Binary:
        case DataType::ByteArray:
            // Already routed above.
            return tl::make_unexpected(invalidBlock(
                "string/binary reached the typed AbsTs branch (bug)"));
        case DataType::Unsupported:
            return tl::make_unexpected(invalidBlock(
                "AbsTimeStampData on Unsupported channel reached the typed parser"));
    }
    return tl::make_unexpected(invalidBlock(
        "AbsTimeStampData: unhandled DataType"));
}

#undef OSF_READ_TS_PAIRS

// ---------------------------------------------------------------------
// bcContinuedRelStampData parser. Numeric only.
// ---------------------------------------------------------------------

#define OSF_READ_REL_PAIRS(VARIANT, READ_EXPR)                                  \
    {                                                                            \
        std::vector<std::pair<std::uint32_t, VARIANT>> v;                        \
        v.reserve(n);                                                            \
        for (std::size_t i = 0; i < n; ++i) {                                    \
            auto dtNs = cur.readU32();                                           \
            if (!dtNs) return tl::make_unexpected(invalidBlock(                 \
                "RelTs delta: short read"));                                     \
            auto value = (READ_EXPR);                                            \
            if (!value) return tl::make_unexpected(invalidBlock(                \
                "RelTs value: short read"));                                     \
            v.emplace_back(*dtNs, *value);                                       \
        }                                                                        \
        return RelTimestampedPayload{std::move(v)};                              \
    }

Result<RelTimestampedPayload> parseContinuedRelStampData(
    std::uint8_t const* body, std::size_t bodyLen, DataType dt, bool multi) {
    PayloadCursor cur{body, bodyLen};
    auto nR = readSampleCount(cur, multi);
    if (!nR) return tl::make_unexpected(std::move(nR).error());
    std::size_t const n = *nR;

    switch (dt) {
        case DataType::Bool:   OSF_READ_REL_PAIRS(bool,          cur.readBool())
        case DataType::Int8:   OSF_READ_REL_PAIRS(std::int8_t,   cur.readI8())
        case DataType::Int16:  OSF_READ_REL_PAIRS(std::int16_t,  cur.readI16())
        case DataType::Int32:  OSF_READ_REL_PAIRS(std::int32_t,  cur.readI32())
        case DataType::Int64:  OSF_READ_REL_PAIRS(std::int64_t,  cur.readI64())
        case DataType::UInt8:  OSF_READ_REL_PAIRS(std::uint8_t,  cur.readU8())
        case DataType::UInt16: OSF_READ_REL_PAIRS(std::uint16_t, cur.readU16())
        case DataType::UInt32: OSF_READ_REL_PAIRS(std::uint32_t, cur.readU32())
        case DataType::UInt64: OSF_READ_REL_PAIRS(std::uint64_t, cur.readU64())
        case DataType::Float:  OSF_READ_REL_PAIRS(float,         cur.readF32())
        case DataType::Double: OSF_READ_REL_PAIRS(double,        cur.readF64())
        default:
            return tl::make_unexpected(invalidBlock(
                "bcContinuedRelStampData not allowed for non-numeric datatype"));
    }
}

#undef OSF_READ_REL_PAIRS

// ---------------------------------------------------------------------
// bcStartData / bcContinuedData parsers (numeric only).
// ---------------------------------------------------------------------

struct StartDataParsed {
    std::int64_t startTimestampNs;
    double sampleRateHz;
    NumericPayload samples;
};

Result<StartDataParsed> parseStartData(std::uint8_t const* body,
                                         std::size_t bodyLen,
                                         DataType dt, bool multi) {
    PayloadCursor cur{body, bodyLen};
    auto ts = cur.readI64();
    if (!ts) return tl::make_unexpected(invalidBlock(
        "StartData timestamp: short read"));
    auto rate = cur.readF64();
    if (!rate) return tl::make_unexpected(invalidBlock(
        "StartData sample rate: short read"));
    auto nR = readSampleCount(cur, multi);
    if (!nR) return tl::make_unexpected(std::move(nR).error());
    auto samples = readNumericN(cur, dt, *nR);
    if (!samples) return tl::make_unexpected(std::move(samples).error());
    return StartDataParsed{*ts, *rate, std::move(*samples)};
}

Result<NumericPayload> parseContinuedData(std::uint8_t const* body,
                                            std::size_t bodyLen,
                                            DataType dt, bool multi) {
    PayloadCursor cur{body, bodyLen};
    auto nR = readSampleCount(cur, multi);
    if (!nR) return tl::make_unexpected(std::move(nR).error());
    return readNumericN(cur, dt, *nR);
}

// ---------------------------------------------------------------------
// AbsTs timestamp-range extractor for stats. Walks the variant to find
// the first and last (smallest position-wise, not value-wise — matches
// the Rust impl's behaviour).
// ---------------------------------------------------------------------

std::optional<std::pair<std::int64_t, std::int64_t>>
absTimestampRange(TimestampedPayload const& payload) {
    return std::visit([](auto const& v) -> std::optional<std::pair<std::int64_t, std::int64_t>> {
        if (v.empty()) return std::nullopt;
        return std::make_pair(v.front().first, v.back().first);
    }, payload);
}

// ---------------------------------------------------------------------
// Channel-info lookup helpers.
// ---------------------------------------------------------------------

std::optional<SkipReason> unsupportedReason(BlockReader::Iterator const&) = delete;
// (placeholder so the compiler errors loudly if someone tries to use the
// private overload pattern from Rust — we use a simple free function below)

std::optional<SkipReason> channelUnsupportedReason(ChannelType ct,
                                                    DataType dt) noexcept {
    if (dt == DataType::Unsupported) {
        return SkipReason{SkipReason::Kind::UnsupportedDataType, 0};
    }
    if (ct == ChannelType::Unsupported) {
        return SkipReason{SkipReason::Kind::UnsupportedChannelType, 0};
    }
    return std::nullopt;
}

}  // anonymous namespace

// =====================================================================
// BlockReader implementation
// =====================================================================

BlockReader::BlockReader(std::istream& stream, MetaBlock const& meta)
    : m_stream(&stream), m_started(std::chrono::steady_clock::now()) {
    // Spec rev 2026-05-24 — version-deterministic null-terminator
    // rule. version == 4 activates the OSF4 strip path; every other
    // value (5, 0, unknown) defaults to OSF5 (no strip). A
    // default-constructed MetaBlock yields version == 0 which the
    // test helpers rely on for OSF5 behaviour.
    m_osfVersion = (meta.fileInfo.version == 4)
                       ? OsfVersion::Osf4
                       : OsfVersion::Osf5;
    m_channels.reserve(meta.channels.size());
    m_stats.channelsTotal = meta.channels.size();
    for (auto const& ch : meta.channels) {
        bool const unsupported = ch.dataType == DataType::Unsupported ||
                                 ch.channelType == ChannelType::Unsupported;
        if (unsupported) ++m_stats.channelsUnsupported;
        m_channels.emplace(ch.index, ChannelInfo{ch.channelType, ch.dataType,
                                                ch.sizeOfLengthValue});
        ChannelStats cs;
        cs.name = ch.name;
        m_stats.perChannel.emplace(ch.index, std::move(cs));
    }
}

BlockReader::IoResult<std::uint32_t>
BlockReader::readLengthField(std::uint8_t sizeofField) {
    if (sizeofField == 2) {
        std::uint8_t buf[2];
        auto r = streamReadN(*m_stream, buf, 2);
        if (!r)             return tl::make_unexpected(std::move(r).error());
        if (!*r)            return std::optional<std::uint32_t>{};
        return std::optional<std::uint32_t>{osf::detail::readLeU16(buf)};
    }
    if (sizeofField == 4) {
        std::uint8_t buf[4];
        auto r = streamReadN(*m_stream, buf, 4);
        if (!r)             return tl::make_unexpected(std::move(r).error());
        if (!*r)            return std::optional<std::uint32_t>{};
        return std::optional<std::uint32_t>{osf::detail::readLeU32(buf)};
    }
    std::ostringstream oss;
    oss << "channel sizeoflengthvalue=" << int{sizeofField}
        << " reached the block reader; must be 2 or 4 "
           "(should have been validated in the metablock parser)";
    return tl::make_unexpected(Error{Error::Code::InvalidMetablock, oss.str()});
}

BlockReader::IoResult<std::vector<std::uint8_t>>
BlockReader::readPayload(std::size_t len) {
    std::vector<std::uint8_t> buf(len);
    if (len == 0) return std::optional<std::vector<std::uint8_t>>{std::move(buf)};
    auto r = streamReadN(*m_stream, buf.data(), static_cast<std::streamsize>(len));
    if (!r)  return tl::make_unexpected(std::move(r).error());
    if (!*r) return std::optional<std::vector<std::uint8_t>>{};
    return std::optional<std::vector<std::uint8_t>>{std::move(buf)};
}

Result<bool> BlockReader::drain(std::uint64_t len) {
    constexpr std::size_t CHUNK = 4096;
    std::uint8_t scratch[CHUNK];
    std::uint64_t left = len;
    while (left > 0) {
        std::size_t const want =
            (left > CHUNK) ? CHUNK : static_cast<std::size_t>(left);
        auto r = streamReadN(*m_stream, scratch,
                               static_cast<std::streamsize>(want));
        if (!r)  return tl::make_unexpected(std::move(r).error());
        if (!*r) return false;  // truncated
        left -= want;
    }
    return true;
}

Result<void> BlockReader::consumeTrailer() {
    // Per spec: [u32 length][u8 control = bcReserved][N bytes payload].
    // The length is always u32 here, NOT the per-channel
    // sizeoflengthvalue. The 2-byte channel index that introduced the
    // trailer was already counted by the caller.
    m_stats.dataSectionSizeBytes += 4;

    std::uint8_t buf[4];
    auto rr = streamReadN(*m_stream, buf, 4);
    if (!rr)  return tl::make_unexpected(std::move(rr).error());
    if (!*rr) {
        if (m_stats.blocksTruncated < 1) m_stats.blocksTruncated = 1;
        m_finished = true;
        m_stats.trailerSeen = true;
        return {};
    }
    std::uint32_t const length = osf::detail::readLeU32(buf);

    auto drained = drain(length);
    if (!drained) return tl::make_unexpected(std::move(drained).error());
    if (*drained) {
        m_stats.dataSectionSizeBytes += length;
    } else {
        if (m_stats.blocksTruncated < 1) m_stats.blocksTruncated = 1;
    }

    // Best-effort: try to consume the magic trailer if present. If the
    // file ends before then, we simply stop without error.
    std::uint8_t tail[MAGIC_TRAILER_LEN];
    auto tr = streamReadN(*m_stream, tail, MAGIC_TRAILER_LEN);
    if (!tr) return tl::make_unexpected(std::move(tr).error());
    if (*tr) {
        m_stats.dataSectionSizeBytes += MAGIC_TRAILER_LEN;
        // We do not enforce the "OSF_STREAM_END" prefix; if the bytes
        // disagree the file is still well-formed up to this point.
    }

    m_stats.trailerSeen = true;
    m_finished = true;
    return {};
}

void BlockReader::recordSkip(std::uint16_t channelIndex, std::uint32_t length,
                              SkipReason const& reason) {
    switch (reason.kind) {
        case SkipReason::Kind::UnsupportedDataType:
        case SkipReason::Kind::UnsupportedChannelType:
            ++m_stats.blocksSkippedUnsupported; break;
        case SkipReason::Kind::DeprecatedBlockType:
            ++m_stats.blocksSkippedDeprecatedType; break;
        case SkipReason::Kind::ReservedBlockType:
            ++m_stats.blocksSkippedReservedType; break;
        case SkipReason::Kind::CrcFailed:
            ++m_stats.blocksCrcFailed; break;
        case SkipReason::Kind::SignatureBlock:
            ++m_stats.blocksSignatureSkipped; break;
    }
    auto& cs = m_stats.perChannel[channelIndex];
    ++cs.blocksSkipped;
    cs.bytesPayload += length;
}

Result<Block> BlockReader::skipBlock(std::uint16_t channelIndex,
                                      std::size_t length,
                                      SkipReason reason) {
    if (m_captureSkipped) {
        auto bufR = readPayload(length);
        if (!bufR) return tl::make_unexpected(std::move(bufR).error());
        if (!*bufR) {
            if (m_stats.blocksTruncated < 1) m_stats.blocksTruncated = 1;
            m_finished = true;
            return tl::make_unexpected(ioError(
                "stream truncated mid-skip-payload"));
        }
        m_stats.dataSectionSizeBytes += length;
        recordSkip(channelIndex, static_cast<std::uint32_t>(length), reason);
        Block blk;
        blk.channelIndex = channelIndex;
        Skipped sk;
        sk.reason = reason;
        sk.bytesSkipped = length;
        sk.payload = std::move(**bufR);
        blk.kind = std::move(sk);
        return blk;
    }
    auto drained = drain(length);
    if (!drained) return tl::make_unexpected(std::move(drained).error());
    if (!*drained) {
        if (m_stats.blocksTruncated < 1) m_stats.blocksTruncated = 1;
        m_finished = true;
        return tl::make_unexpected(ioError(
            "stream truncated mid-skip-payload"));
    }
    m_stats.dataSectionSizeBytes += length;
    recordSkip(channelIndex, static_cast<std::uint32_t>(length), reason);
    Block blk;
    blk.channelIndex = channelIndex;
    Skipped sk;
    sk.reason = reason;
    sk.bytesSkipped = length;
    blk.kind = std::move(sk);
    return blk;
}

namespace {

// Verify the trailing 4-byte frame CRC over the whole frame (channel index,
// length field, control byte and payload). `payload` still includes the CRC.
bool verifyFrameCrc(std::uint16_t channelIndex, std::uint8_t sizeofField,
                    std::uint32_t length, std::vector<std::uint8_t> const& payload) {
    std::size_t const split = payload.size() - 4;
    std::uint32_t const stored = osf::detail::readLeU32(payload.data() + split);
    std::uint8_t const ciLe[2] = {
        static_cast<std::uint8_t>(channelIndex & 0xFFu),
        static_cast<std::uint8_t>((channelIndex >> 8) & 0xFFu)};
    std::uint8_t const lenLe[4] = {
        static_cast<std::uint8_t>(length & 0xFFu),
        static_cast<std::uint8_t>((length >> 8) & 0xFFu),
        static_cast<std::uint8_t>((length >> 16) & 0xFFu),
        static_cast<std::uint8_t>((length >> 24) & 0xFFu)};
    osf::detail::Crc32c c;
    c.update(ciLe, 2);
    c.update(lenLe, sizeofField);
    c.update(payload.data(), split);
    return c.value() == stored;
}

}  // namespace

std::optional<Result<Block>> BlockReader::next() {
    if (m_finished) return std::nullopt;

    // Step 1: 2-byte channel index. Clean EOF is the regular end of
    // the data section.
    std::uint8_t ciBuf[2];
    {
        auto r = streamReadN(*m_stream, ciBuf, 2);
        if (!r) { m_finished = true; return Result<Block>{tl::make_unexpected(r.error())}; }
        if (!*r) { m_finished = true; return std::nullopt; }
    }
    std::uint16_t const channelIndex = osf::detail::readLeU16(ciBuf);
    m_stats.dataSectionSizeBytes += 2;

    // Step 2: optional 0xFFFF trailer block.
    if (channelIndex == TRAILER_CHANNEL_INDEX) {
        auto r = consumeTrailer();
        if (!r) { m_finished = true; return Result<Block>{tl::make_unexpected(r.error())}; }
        return std::nullopt;
    }

    // Step 2b: integrity signature block (level signed). Channel 0xFFFE is the
    // reserved file-wide integrity channel and is not declared in the metablock.
    // This build reads level crc but not signatures, so skip it via its (u32)
    // length field and count it; a signed file therefore stays readable.
    if (m_integrity != IntegrityProfile::None && channelIndex == SIGNATURE_CHANNEL_INDEX) {
        std::uint8_t lb[4];
        auto lr = streamReadN(*m_stream, lb, 4);
        if (!lr) { m_finished = true; return Result<Block>{tl::make_unexpected(lr.error())}; }
        if (!*lr) {
            if (m_stats.blocksTruncated < 1) m_stats.blocksTruncated = 1;
            m_finished = true;
            return std::nullopt;
        }
        std::uint32_t const sigLen = osf::detail::readLeU32(lb);
        m_stats.dataSectionSizeBytes += 4;
        auto drained = drain(sigLen);
        if (!drained) { m_finished = true; return Result<Block>{tl::make_unexpected(drained.error())}; }
        if (!*drained) {
            if (m_stats.blocksTruncated < 1) m_stats.blocksTruncated = 1;
            m_finished = true;
            return std::nullopt;
        }
        m_stats.dataSectionSizeBytes += sigLen;
        SkipReason const reason{SkipReason::Kind::SignatureBlock, 0};
        recordSkip(channelIndex, sigLen, reason);
        Block blk;
        blk.channelIndex = channelIndex;
        Skipped sk;
        sk.reason = reason;
        sk.bytesSkipped = sigLen;
        blk.kind = std::move(sk);
        return Result<Block>{std::move(blk)};
    }

    // Step 3: channel lookup. An unknown index is a corruption signal
    // (we cannot even know how wide the length prefix should be).
    auto it = m_channels.find(channelIndex);
    if (it == m_channels.end()) {
        m_finished = true;
        std::ostringstream oss;
        oss << "block references unknown channel index " << channelIndex;
        return Result<Block>{tl::make_unexpected(
            Error{Error::Code::UnknownChannelIndex, oss.str()})};
    }
    ChannelInfo const& info = it->second;

    // Step 4: per-channel length prefix.
    auto lenR = readLengthField(info.sizeOfLengthValue);
    if (!lenR) { m_finished = true; return Result<Block>{tl::make_unexpected(lenR.error())}; }
    if (!*lenR) {
        if (m_stats.blocksTruncated < 1) m_stats.blocksTruncated = 1;
        m_finished = true;
        return std::nullopt;
    }
    std::uint32_t const length = **lenR;
    m_stats.dataSectionSizeBytes += info.sizeOfLengthValue;

    if (length == 0) {
        SkipReason const reason{SkipReason::Kind::ReservedBlockType, 0};
        recordSkip(channelIndex, 0, reason);
        Block blk;
        blk.channelIndex = channelIndex;
        Skipped sk;
        sk.reason = reason;
        sk.bytesSkipped = 0;
        blk.kind = std::move(sk);
        return Result<Block>{std::move(blk)};
    }

    std::size_t const lengthUsize = length;

    // Step 5: forward-compat skip — Unsupported channel.
    if (auto reason = channelUnsupportedReason(info.channelType, info.dataType)) {
        auto r = skipBlock(channelIndex, lengthUsize, *reason);
        if (!r) { m_finished = true; return Result<Block>{tl::make_unexpected(r.error())}; }
        return Result<Block>{std::move(*r)};
    }

    // Step 6: pull the full payload.
    auto payloadR = readPayload(lengthUsize);
    if (!payloadR) { m_finished = true; return Result<Block>{tl::make_unexpected(payloadR.error())}; }
    if (!*payloadR) {
        if (m_stats.blocksTruncated < 1) m_stats.blocksTruncated = 1;
        m_finished = true;
        return std::nullopt;
    }
    std::vector<std::uint8_t> payload = std::move(**payloadR);
    m_stats.dataSectionSizeBytes += length;

    // Step 6b: frame CRC (integrity level crc). The last four bytes of the data
    // area are a CRC32C over the whole frame; verify and strip them *before* the
    // typed parse (fail-closed framing — a residual check after decoding is
    // insufficient for variable-length payloads). A mismatch skips the block.
    if (m_integrity != IntegrityProfile::None) {
        if (payload.size() < 5 ||
            !verifyFrameCrc(channelIndex, info.sizeOfLengthValue, length, payload)) {
            SkipReason const reason{SkipReason::Kind::CrcFailed, 0};
            recordSkip(channelIndex, length, reason);
            Block blk;
            blk.channelIndex = channelIndex;
            Skipped sk;
            sk.reason = reason;
            sk.bytesSkipped = length;
            if (m_captureSkipped) {
                sk.payload = payload;
            }
            blk.kind = std::move(sk);
            return Result<Block>{std::move(blk)};
        }
        payload.resize(payload.size() - 4);  // CRC verified — drop it
    }

    // Step 7: decode control byte and route.
    std::uint8_t const controlRaw = payload[0];
    ControlByte const cb = decodeControlByte(controlRaw);
    std::uint8_t const* body = payload.data() + 1;
    std::size_t const bodyLen = payload.size() - 1;

    auto makeSkipped = [&](SkipReason::Kind kind, std::uint8_t raw) {
        SkipReason const reason{kind, raw};
        recordSkip(channelIndex, length, reason);
        Block blk;
        blk.channelIndex = channelIndex;
        Skipped sk;
        sk.reason = reason;
        sk.bytesSkipped = length;
        if (m_captureSkipped) {
            sk.payload = std::vector<std::uint8_t>(body, body + bodyLen);
        }
        blk.kind = std::move(sk);
        return blk;
    };

    switch (cb.kind) {
        case ControlKind::Reserved:
        case ControlKind::TimebaseRealign:
            return Result<Block>{makeSkipped(
                SkipReason::Kind::ReservedBlockType, cb.raw)};
        case ControlKind::TrustedTimestamp:
        case ControlKind::StatusEvent:
        case ControlKind::MessageEvent:
            return Result<Block>{makeSkipped(
                SkipReason::Kind::DeprecatedBlockType, cb.raw)};
        case ControlKind::Unknown:
            return Result<Block>{makeSkipped(
                SkipReason::Kind::ReservedBlockType, cb.raw)};

        case ControlKind::StartData: {
            auto r = parseStartData(body, bodyLen, info.dataType,
                                      cb.multiSample);
            if (!r) return Result<Block>{tl::make_unexpected(r.error())};
            std::size_t const n = numericPayloadLen(r->samples);
            auto& cs = m_stats.perChannel[channelIndex];
            ++cs.blocksRead;
            cs.bytesPayload += length;
            cs.samplesTotal += n;
            ++cs.segments;
            cs.observeTimestamp(r->startTimestampNs);
            if (n > 0 && r->sampleRateHz > 0.0) {
                double const spanNs =
                    (static_cast<double>(n - 1) / r->sampleRateHz) * 1.0e9;
                std::int64_t const last = r->startTimestampNs +
                    static_cast<std::int64_t>(spanNs);
                cs.observeTimestamp(last);
            }
            ++m_stats.blocksRead;
            Block blk;
            blk.channelIndex = channelIndex;
            StartData sd;
            sd.startTimestampNs = r->startTimestampNs;
            sd.sampleRateHz = r->sampleRateHz;
            sd.samples = std::move(r->samples);
            blk.kind = std::move(sd);
            return Result<Block>{std::move(blk)};
        }
        case ControlKind::ContinuedData: {
            auto r = parseContinuedData(body, bodyLen, info.dataType,
                                          cb.multiSample);
            if (!r) return Result<Block>{tl::make_unexpected(r.error())};
            std::size_t const n = numericPayloadLen(*r);
            auto& cs = m_stats.perChannel[channelIndex];
            ++cs.blocksRead;
            cs.bytesPayload += length;
            cs.samplesTotal += n;
            ++m_stats.blocksRead;
            Block blk;
            blk.channelIndex = channelIndex;
            ContinuedData cd;
            cd.samples = std::move(*r);
            blk.kind = std::move(cd);
            return Result<Block>{std::move(blk)};
        }
        case ControlKind::AbsTimeStampData: {
            auto r = parseAbsTimestampData(body, bodyLen, info.dataType,
                                              cb.multiSample, m_osfVersion);
            if (!r) return Result<Block>{tl::make_unexpected(r.error())};
            std::size_t const n = timestampedPayloadLen(*r);
            auto& cs = m_stats.perChannel[channelIndex];
            ++cs.blocksRead;
            cs.bytesPayload += length;
            cs.samplesTotal += n;
            if (auto range = absTimestampRange(*r)) {
                cs.observeTimestamp(range->first);
                cs.observeTimestamp(range->second);
            }
            ++m_stats.blocksRead;
            Block blk;
            blk.channelIndex = channelIndex;
            AbsTimestampData ad;
            ad.samples = std::move(*r);
            blk.kind = std::move(ad);
            return Result<Block>{std::move(blk)};
        }
        case ControlKind::ContinuedRelStampData: {
            auto r = parseContinuedRelStampData(body, bodyLen,
                                                    info.dataType,
                                                    cb.multiSample);
            if (!r) return Result<Block>{tl::make_unexpected(r.error())};
            std::size_t const n = relTimestampedPayloadLen(*r);
            auto& cs = m_stats.perChannel[channelIndex];
            ++cs.blocksRead;
            cs.bytesPayload += length;
            cs.samplesTotal += n;
            ++m_stats.blocksRead;
            Block blk;
            blk.channelIndex = channelIndex;
            ContinuedRelStampData rd;
            rd.samples = std::move(*r);
            blk.kind = std::move(rd);
            return Result<Block>{std::move(blk)};
        }
    }
    return Result<Block>{tl::make_unexpected(invalidBlock(
        "BlockReader::next: unhandled ControlKind"))};
}

ReaderStats BlockReader::stats() const {
    ReaderStats s = m_stats;
    s.elapsed = std::chrono::steady_clock::now() - m_started;
    s.blocksTotal = s.blocksRead + s.blocksSkippedUnsupported +
                     s.blocksSkippedDeprecatedType +
                     s.blocksSkippedReservedType +
                     s.blocksCrcFailed + s.blocksSignatureSkipped;
    s.channelsWithData = 0;
    for (auto const& [_, cs] : s.perChannel) {
        if (cs.blocksRead + cs.blocksSkipped > 0) ++s.channelsWithData;
    }
    return s;
}

// =====================================================================
// BlockReader::Iterator
// =====================================================================

BlockReader::Iterator::Iterator(BlockReader& reader) : m_reader(&reader) {
    m_current = m_reader->next();
}

BlockReader::Iterator& BlockReader::Iterator::operator++() {
    if (m_reader) m_current = m_reader->next();
    return *this;
}

void BlockReader::Iterator::operator++(int) { ++(*this); }

}  // namespace osf
