// SPDX-License-Identifier: MIT

#include <osf/reader.hpp>

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

Error invalid_block(std::string msg) {
    return Error{Error::Code::InvalidBlock, std::move(msg)};
}

Error io_error(std::string msg) {
    return Error{Error::Code::IoError, std::move(msg)};
}

// Pull `len` bytes from the stream. Returns:
// - `Ok(true)`  — full `len` bytes consumed.
// - `Ok(false)` — stream truncated; reached EOF early.
// - `Err`       — non-EOF I/O failure.
Result<bool> stream_read_n(std::istream& s, void* dst, std::streamsize len) {
    if (len == 0) return true;
    s.read(static_cast<char*>(dst), len);
    if (s.gcount() == len) return true;
    if (s.eof()) {
        s.clear();  // reset eofbit so subsequent calls behave predictably
        return false;
    }
    return tl::make_unexpected(io_error("istream::read failed before EOF"));
}

// ---------------------------------------------------------------------
// Little-endian byte-by-byte decoders. The OSF wire format is LE on
// every supported platform. Bit-pattern moves into float/double use
// std::memcpy, which the compiler folds into a single load on every
// modern toolchain (no aliasing/UB concerns).
// ---------------------------------------------------------------------

std::uint16_t le_u16(std::uint8_t const* p) noexcept {
    return  static_cast<std::uint16_t>(p[0])
         | (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t le_u32(std::uint8_t const* p) noexcept {
    return  static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t le_u64(std::uint8_t const* p) noexcept {
    return  static_cast<std::uint64_t>(p[0])
         | (static_cast<std::uint64_t>(p[1]) << 8)
         | (static_cast<std::uint64_t>(p[2]) << 16)
         | (static_cast<std::uint64_t>(p[3]) << 24)
         | (static_cast<std::uint64_t>(p[4]) << 32)
         | (static_cast<std::uint64_t>(p[5]) << 40)
         | (static_cast<std::uint64_t>(p[6]) << 48)
         | (static_cast<std::uint64_t>(p[7]) << 56);
}

std::int16_t le_i16(std::uint8_t const* p) noexcept {
    return static_cast<std::int16_t>(le_u16(p));
}
std::int32_t le_i32(std::uint8_t const* p) noexcept {
    return static_cast<std::int32_t>(le_u32(p));
}
std::int64_t le_i64(std::uint8_t const* p) noexcept {
    return static_cast<std::int64_t>(le_u64(p));
}

float le_f32(std::uint8_t const* p) noexcept {
    std::uint32_t const bits = le_u32(p);
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

double le_f64(std::uint8_t const* p) noexcept {
    std::uint64_t const bits = le_u64(p);
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

// ---------------------------------------------------------------------
// Cursor over an in-memory payload. Carries a position; every accessor
// either advances by the requested width and returns the value, or
// returns std::nullopt on overflow.
// ---------------------------------------------------------------------

class PayloadCursor {
public:
    PayloadCursor(std::uint8_t const* data, std::size_t size) noexcept
        : data_(data), size_(size) {}

    [[nodiscard]] std::size_t remaining() const noexcept { return size_ - pos_; }
    [[nodiscard]] std::size_t pos() const noexcept { return pos_; }
    [[nodiscard]] std::uint8_t const* tail() const noexcept { return data_ + pos_; }

    std::optional<std::uint8_t> read_u8() {
        if (remaining() < 1) return std::nullopt;
        return data_[pos_++];
    }
    std::optional<std::uint16_t> read_u16() {
        if (remaining() < 2) return std::nullopt;
        auto v = le_u16(data_ + pos_); pos_ += 2; return v;
    }
    std::optional<std::uint32_t> read_u32() {
        if (remaining() < 4) return std::nullopt;
        auto v = le_u32(data_ + pos_); pos_ += 4; return v;
    }
    std::optional<std::uint64_t> read_u64() {
        if (remaining() < 8) return std::nullopt;
        auto v = le_u64(data_ + pos_); pos_ += 8; return v;
    }
    std::optional<std::int8_t>  read_i8() {
        auto v = read_u8();
        return v ? std::optional<std::int8_t>{static_cast<std::int8_t>(*v)}
                 : std::nullopt;
    }
    std::optional<std::int16_t> read_i16() {
        auto v = read_u16();
        return v ? std::optional<std::int16_t>{static_cast<std::int16_t>(*v)}
                 : std::nullopt;
    }
    std::optional<std::int32_t> read_i32() {
        auto v = read_u32();
        return v ? std::optional<std::int32_t>{static_cast<std::int32_t>(*v)}
                 : std::nullopt;
    }
    std::optional<std::int64_t> read_i64() {
        auto v = read_u64();
        return v ? std::optional<std::int64_t>{static_cast<std::int64_t>(*v)}
                 : std::nullopt;
    }
    std::optional<float>  read_f32() {
        if (remaining() < 4) return std::nullopt;
        auto v = le_f32(data_ + pos_); pos_ += 4; return v;
    }
    std::optional<double> read_f64() {
        if (remaining() < 8) return std::nullopt;
        auto v = le_f64(data_ + pos_); pos_ += 8; return v;
    }
    std::optional<bool> read_bool() {
        auto v = read_u8();
        return v ? std::optional<bool>{*v != 0} : std::nullopt;
    }

private:
    std::uint8_t const* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

// ---------------------------------------------------------------------
// Sample-count reader. !multi → implicit N = 1; multi → u32 prefix.
// ---------------------------------------------------------------------

Result<std::size_t> read_sample_count(PayloadCursor& cur, bool multi) {
    if (!multi) return std::size_t{1};
    auto n = cur.read_u32();
    if (!n) return tl::make_unexpected(invalid_block("multi-sample N: short read"));
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
            if (!x) return tl::make_unexpected(invalid_block(     \
                "numeric run: short read"));                      \
            v.push_back(*x);                                      \
        }                                                         \
        return NumericPayload{std::move(v)};                      \
    }

Result<NumericPayload> read_numeric_n(PayloadCursor& cur, DataType dt,
                                     std::size_t n) {
    switch (dt) {
        case DataType::Bool:   OSF_READ_RUN(bool,             cur.read_bool())
        case DataType::Int8:   OSF_READ_RUN(std::int8_t,      cur.read_i8())
        case DataType::Int16:  OSF_READ_RUN(std::int16_t,     cur.read_i16())
        case DataType::Int32:  OSF_READ_RUN(std::int32_t,     cur.read_i32())
        case DataType::Int64:  OSF_READ_RUN(std::int64_t,     cur.read_i64())
        case DataType::UInt8:  OSF_READ_RUN(std::uint8_t,     cur.read_u8())
        case DataType::UInt16: OSF_READ_RUN(std::uint16_t,    cur.read_u16())
        case DataType::UInt32: OSF_READ_RUN(std::uint32_t,    cur.read_u32())
        case DataType::UInt64: OSF_READ_RUN(std::uint64_t,    cur.read_u64())
        case DataType::Float:  OSF_READ_RUN(float,            cur.read_f32())
        case DataType::Double: OSF_READ_RUN(double,           cur.read_f64())
        default:
            return tl::make_unexpected(invalid_block(
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
            auto ts = cur.read_i64();                                    \
            if (!ts) return tl::make_unexpected(invalid_block(           \
                "AbsTs ts: short read"));                                \
            auto value = (READ_EXPR);                                    \
            if (!value) return tl::make_unexpected(invalid_block(        \
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
std::vector<std::uint8_t> strip_osf4_terminator(std::uint8_t const* p,
                                                std::size_t n,
                                                OsfVersion osf_version) {
    if (osf_version == OsfVersion::Osf4 && n > 0) {
        return std::vector<std::uint8_t>(p, p + n - 1);
    }
    return std::vector<std::uint8_t>(p, p + n);
}

Result<TimestampedPayload> build_string_or_binary(
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
// strip_osf4_terminator for the rationale.
Result<TimestampedPayload> parse_abs_ts_string_or_binary(
    std::uint8_t const* body, std::size_t body_len, DataType dt, bool multi,
    OsfVersion osf_version) {
    std::size_t n = 1;
    std::size_t rest_off = 0;

    if (multi) {
        if (body_len < 4) {
            return tl::make_unexpected(invalid_block(
                "AbsTs string/binary N: short read"));
        }
        std::uint32_t const raw = le_u32(body);
        if (raw == 0) {
            return build_string_or_binary(dt, {});
        }
        n = static_cast<std::size_t>(raw);
        rest_off = 4;
    }
    // !multi: per spec bit 7 should be set; we tolerate clear bit as
    // implicit N=1, matching the Rust reference.

    std::size_t const rest_len = body_len - rest_off;
    std::uint8_t const* rest = body + rest_off;

    if (n == 1) {
        if (rest_len < 8) {
            return tl::make_unexpected(invalid_block(
                "AbsTs string/binary ts: short read"));
        }
        std::int64_t const ts = le_i64(rest);
        auto payload = strip_osf4_terminator(rest + 8, rest_len - 8, osf_version);
        std::vector<std::pair<std::int64_t, std::vector<std::uint8_t>>> raw;
        raw.emplace_back(ts, std::move(payload));
        return build_string_or_binary(dt, std::move(raw));
    }

    if (rest_len % n != 0) {
        // Spec mandates equal-length segments; if the body length is
        // not divisible by N we cannot split it. Fall back to a single
        // sample (mirrors the Rust reference's warn-and-degrade path).
        if (rest_len < 8) {
            return tl::make_unexpected(invalid_block(
                "AbsTs string/binary ts: short read"));
        }
        std::int64_t const ts = le_i64(rest);
        auto payload = strip_osf4_terminator(rest + 8, rest_len - 8, osf_version);
        std::vector<std::pair<std::int64_t, std::vector<std::uint8_t>>> raw;
        raw.emplace_back(ts, std::move(payload));
        return build_string_or_binary(dt, std::move(raw));
    }

    std::size_t const per_sample = rest_len / n;
    // OSF4 needs i64 ts (8) + at least one byte (the terminator alone
    // is a legal empty payload) = 9. OSF5 needs only i64 ts = 8.
    std::size_t const min_per_sample =
        (osf_version == OsfVersion::Osf4) ? 9u : 8u;
    if (per_sample < min_per_sample) {
        std::ostringstream oss;
        oss << "AbsTs string/binary N=" << n
            << ": per-sample size " << per_sample
            << " is less than " << min_per_sample
            << " (need i64 ts";
        if (osf_version == OsfVersion::Osf4) {
            oss << " + at least the OSF4 null terminator";
        }
        oss << ")";
        return tl::make_unexpected(invalid_block(oss.str()));
    }

    std::vector<std::pair<std::int64_t, std::vector<std::uint8_t>>> raw;
    raw.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::uint8_t const* chunk = rest + i * per_sample;
        std::int64_t const ts = le_i64(chunk);
        auto payload = strip_osf4_terminator(chunk + 8, per_sample - 8,
                                             osf_version);
        raw.emplace_back(ts, std::move(payload));
    }
    return build_string_or_binary(dt, std::move(raw));
}

Result<TimestampedPayload> parse_abs_timestamp_data(
    std::uint8_t const* body, std::size_t body_len, DataType dt, bool multi,
    OsfVersion osf_version) {
    if (dt == DataType::String || dt == DataType::Binary ||
        dt == DataType::ByteArray) {
        return parse_abs_ts_string_or_binary(
            body, body_len, dt == DataType::ByteArray ? DataType::Binary : dt,
            multi, osf_version);
    }

    PayloadCursor cur{body, body_len};
    auto n_r = read_sample_count(cur, multi);
    if (!n_r) return tl::make_unexpected(std::move(n_r).error());
    std::size_t const n = *n_r;

    switch (dt) {
        case DataType::Bool:   OSF_READ_TS_PAIRS(bool,           cur.read_bool())
        case DataType::Int8:   OSF_READ_TS_PAIRS(std::int8_t,    cur.read_i8())
        case DataType::Int16:  OSF_READ_TS_PAIRS(std::int16_t,   cur.read_i16())
        case DataType::Int32:  OSF_READ_TS_PAIRS(std::int32_t,   cur.read_i32())
        case DataType::Int64:  OSF_READ_TS_PAIRS(std::int64_t,   cur.read_i64())
        case DataType::UInt8:  OSF_READ_TS_PAIRS(std::uint8_t,   cur.read_u8())
        case DataType::UInt16: OSF_READ_TS_PAIRS(std::uint16_t,  cur.read_u16())
        case DataType::UInt32: OSF_READ_TS_PAIRS(std::uint32_t,  cur.read_u32())
        case DataType::UInt64: OSF_READ_TS_PAIRS(std::uint64_t,  cur.read_u64())
        case DataType::Float:  OSF_READ_TS_PAIRS(float,          cur.read_f32())
        case DataType::Double: OSF_READ_TS_PAIRS(double,         cur.read_f64())
        case DataType::GpsLocation: {
            std::vector<std::pair<std::int64_t, GpsLocation>> v;
            v.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                auto ts = cur.read_i64();
                if (!ts) return tl::make_unexpected(invalid_block(
                    "AbsTs gps ts: short read"));
                auto lat = cur.read_f64();
                auto lon = cur.read_f64();
                auto alt = cur.read_f64();
                if (!lat || !lon || !alt) {
                    return tl::make_unexpected(invalid_block(
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
            return tl::make_unexpected(invalid_block(
                "string/binary reached the typed AbsTs branch (bug)"));
        case DataType::Unsupported:
            return tl::make_unexpected(invalid_block(
                "AbsTimeStampData on Unsupported channel reached the typed parser"));
    }
    return tl::make_unexpected(invalid_block(
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
            auto dt_ns = cur.read_u32();                                         \
            if (!dt_ns) return tl::make_unexpected(invalid_block(                \
                "RelTs delta: short read"));                                     \
            auto value = (READ_EXPR);                                            \
            if (!value) return tl::make_unexpected(invalid_block(                \
                "RelTs value: short read"));                                     \
            v.emplace_back(*dt_ns, *value);                                      \
        }                                                                        \
        return RelTimestampedPayload{std::move(v)};                              \
    }

Result<RelTimestampedPayload> parse_continued_rel_stamp_data(
    std::uint8_t const* body, std::size_t body_len, DataType dt, bool multi) {
    PayloadCursor cur{body, body_len};
    auto n_r = read_sample_count(cur, multi);
    if (!n_r) return tl::make_unexpected(std::move(n_r).error());
    std::size_t const n = *n_r;

    switch (dt) {
        case DataType::Bool:   OSF_READ_REL_PAIRS(bool,          cur.read_bool())
        case DataType::Int8:   OSF_READ_REL_PAIRS(std::int8_t,   cur.read_i8())
        case DataType::Int16:  OSF_READ_REL_PAIRS(std::int16_t,  cur.read_i16())
        case DataType::Int32:  OSF_READ_REL_PAIRS(std::int32_t,  cur.read_i32())
        case DataType::Int64:  OSF_READ_REL_PAIRS(std::int64_t,  cur.read_i64())
        case DataType::UInt8:  OSF_READ_REL_PAIRS(std::uint8_t,  cur.read_u8())
        case DataType::UInt16: OSF_READ_REL_PAIRS(std::uint16_t, cur.read_u16())
        case DataType::UInt32: OSF_READ_REL_PAIRS(std::uint32_t, cur.read_u32())
        case DataType::UInt64: OSF_READ_REL_PAIRS(std::uint64_t, cur.read_u64())
        case DataType::Float:  OSF_READ_REL_PAIRS(float,         cur.read_f32())
        case DataType::Double: OSF_READ_REL_PAIRS(double,        cur.read_f64())
        default:
            return tl::make_unexpected(invalid_block(
                "bcContinuedRelStampData not allowed for non-numeric datatype"));
    }
}

#undef OSF_READ_REL_PAIRS

// ---------------------------------------------------------------------
// bcStartData / bcContinuedData parsers (numeric only).
// ---------------------------------------------------------------------

struct StartDataParsed {
    std::int64_t start_timestamp_ns;
    double sample_rate_hz;
    NumericPayload samples;
};

Result<StartDataParsed> parse_start_data(std::uint8_t const* body,
                                         std::size_t body_len,
                                         DataType dt, bool multi) {
    PayloadCursor cur{body, body_len};
    auto ts = cur.read_i64();
    if (!ts) return tl::make_unexpected(invalid_block(
        "StartData timestamp: short read"));
    auto rate = cur.read_f64();
    if (!rate) return tl::make_unexpected(invalid_block(
        "StartData sample rate: short read"));
    auto n_r = read_sample_count(cur, multi);
    if (!n_r) return tl::make_unexpected(std::move(n_r).error());
    auto samples = read_numeric_n(cur, dt, *n_r);
    if (!samples) return tl::make_unexpected(std::move(samples).error());
    return StartDataParsed{*ts, *rate, std::move(*samples)};
}

Result<NumericPayload> parse_continued_data(std::uint8_t const* body,
                                            std::size_t body_len,
                                            DataType dt, bool multi) {
    PayloadCursor cur{body, body_len};
    auto n_r = read_sample_count(cur, multi);
    if (!n_r) return tl::make_unexpected(std::move(n_r).error());
    return read_numeric_n(cur, dt, *n_r);
}

// ---------------------------------------------------------------------
// AbsTs timestamp-range extractor for stats. Walks the variant to find
// the first and last (smallest position-wise, not value-wise — matches
// the Rust impl's behaviour).
// ---------------------------------------------------------------------

std::optional<std::pair<std::int64_t, std::int64_t>>
abs_timestamp_range(TimestampedPayload const& payload) {
    return std::visit([](auto const& v) -> std::optional<std::pair<std::int64_t, std::int64_t>> {
        if (v.empty()) return std::nullopt;
        return std::make_pair(v.front().first, v.back().first);
    }, payload);
}

// ---------------------------------------------------------------------
// Channel-info lookup helpers.
// ---------------------------------------------------------------------

std::optional<SkipReason> unsupported_reason(BlockReader::Iterator const&) = delete;
// (placeholder so the compiler errors loudly if someone tries to use the
// private overload pattern from Rust — we use a simple free function below)

std::optional<SkipReason> channel_unsupported_reason(ChannelType ct,
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
    : stream_(&stream), started_(std::chrono::steady_clock::now()) {
    // Spec rev 2026-05-24 — version-deterministic null-terminator
    // rule. version == 4 activates the OSF4 strip path; every other
    // value (5, 0, unknown) defaults to OSF5 (no strip). A
    // default-constructed MetaBlock yields version == 0 which the
    // test helpers rely on for OSF5 behaviour.
    osf_version_ = (meta.file_info.version == 4)
                       ? OsfVersion::Osf4
                       : OsfVersion::Osf5;
    channels_.reserve(meta.channels.size());
    stats_.channels_total = meta.channels.size();
    for (auto const& ch : meta.channels) {
        bool const unsupported = ch.data_type == DataType::Unsupported ||
                                 ch.channel_type == ChannelType::Unsupported;
        if (unsupported) ++stats_.channels_unsupported;
        channels_.emplace(ch.index, ChannelInfo{ch.channel_type, ch.data_type,
                                                ch.size_of_length_value});
        ChannelStats cs;
        cs.name = ch.name;
        stats_.per_channel.emplace(ch.index, std::move(cs));
    }
}

BlockReader::IoResult<std::uint32_t>
BlockReader::read_length_field(std::uint8_t sizeof_field) {
    if (sizeof_field == 2) {
        std::uint8_t buf[2];
        auto r = stream_read_n(*stream_, buf, 2);
        if (!r)             return tl::make_unexpected(std::move(r).error());
        if (!*r)            return std::optional<std::uint32_t>{};
        return std::optional<std::uint32_t>{le_u16(buf)};
    }
    if (sizeof_field == 4) {
        std::uint8_t buf[4];
        auto r = stream_read_n(*stream_, buf, 4);
        if (!r)             return tl::make_unexpected(std::move(r).error());
        if (!*r)            return std::optional<std::uint32_t>{};
        return std::optional<std::uint32_t>{le_u32(buf)};
    }
    std::ostringstream oss;
    oss << "channel sizeoflengthvalue=" << int{sizeof_field}
        << " reached the block reader; must be 2 or 4 "
           "(should have been validated in the metablock parser)";
    return tl::make_unexpected(Error{Error::Code::InvalidMetablock, oss.str()});
}

BlockReader::IoResult<std::vector<std::uint8_t>>
BlockReader::read_payload(std::size_t len) {
    std::vector<std::uint8_t> buf(len);
    if (len == 0) return std::optional<std::vector<std::uint8_t>>{std::move(buf)};
    auto r = stream_read_n(*stream_, buf.data(), static_cast<std::streamsize>(len));
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
        auto r = stream_read_n(*stream_, scratch,
                               static_cast<std::streamsize>(want));
        if (!r)  return tl::make_unexpected(std::move(r).error());
        if (!*r) return false;  // truncated
        left -= want;
    }
    return true;
}

Result<void> BlockReader::consume_trailer() {
    // Per spec: [u32 length][u8 control = bcReserved][N bytes payload].
    // The length is always u32 here, NOT the per-channel
    // sizeoflengthvalue. The 2-byte channel index that introduced the
    // trailer was already counted by the caller.
    stats_.data_section_size_bytes += 4;

    std::uint8_t buf[4];
    auto rr = stream_read_n(*stream_, buf, 4);
    if (!rr)  return tl::make_unexpected(std::move(rr).error());
    if (!*rr) {
        if (stats_.blocks_truncated < 1) stats_.blocks_truncated = 1;
        finished_ = true;
        stats_.trailer_seen = true;
        return {};
    }
    std::uint32_t const length = le_u32(buf);

    auto drained = drain(length);
    if (!drained) return tl::make_unexpected(std::move(drained).error());
    if (*drained) {
        stats_.data_section_size_bytes += length;
    } else {
        if (stats_.blocks_truncated < 1) stats_.blocks_truncated = 1;
    }

    // Best-effort: try to consume the magic trailer if present. If the
    // file ends before then, we simply stop without error.
    std::uint8_t tail[MAGIC_TRAILER_LEN];
    auto tr = stream_read_n(*stream_, tail, MAGIC_TRAILER_LEN);
    if (!tr) return tl::make_unexpected(std::move(tr).error());
    if (*tr) {
        stats_.data_section_size_bytes += MAGIC_TRAILER_LEN;
        // We do not enforce the "OSF_STREAM_END" prefix; if the bytes
        // disagree the file is still well-formed up to this point.
    }

    stats_.trailer_seen = true;
    finished_ = true;
    return {};
}

void BlockReader::record_skip(std::uint16_t channel_index, std::uint32_t length,
                              SkipReason const& reason) {
    switch (reason.kind) {
        case SkipReason::Kind::UnsupportedDataType:
        case SkipReason::Kind::UnsupportedChannelType:
            ++stats_.blocks_skipped_unsupported; break;
        case SkipReason::Kind::DeprecatedBlockType:
            ++stats_.blocks_skipped_deprecated_type; break;
        case SkipReason::Kind::ReservedBlockType:
            ++stats_.blocks_skipped_reserved_type; break;
    }
    auto& cs = stats_.per_channel[channel_index];
    ++cs.blocks_skipped;
    cs.bytes_payload += length;
}

Result<Block> BlockReader::skip_block(std::uint16_t channel_index,
                                      std::size_t length,
                                      SkipReason reason) {
    if (capture_skipped_) {
        auto buf_r = read_payload(length);
        if (!buf_r) return tl::make_unexpected(std::move(buf_r).error());
        if (!*buf_r) {
            if (stats_.blocks_truncated < 1) stats_.blocks_truncated = 1;
            finished_ = true;
            return tl::make_unexpected(io_error(
                "stream truncated mid-skip-payload"));
        }
        stats_.data_section_size_bytes += length;
        record_skip(channel_index, static_cast<std::uint32_t>(length), reason);
        Block blk;
        blk.channel_index = channel_index;
        Skipped sk;
        sk.reason = reason;
        sk.bytes_skipped = length;
        sk.payload = std::move(**buf_r);
        blk.kind = std::move(sk);
        return blk;
    }
    auto drained = drain(length);
    if (!drained) return tl::make_unexpected(std::move(drained).error());
    if (!*drained) {
        if (stats_.blocks_truncated < 1) stats_.blocks_truncated = 1;
        finished_ = true;
        return tl::make_unexpected(io_error(
            "stream truncated mid-skip-payload"));
    }
    stats_.data_section_size_bytes += length;
    record_skip(channel_index, static_cast<std::uint32_t>(length), reason);
    Block blk;
    blk.channel_index = channel_index;
    Skipped sk;
    sk.reason = reason;
    sk.bytes_skipped = length;
    blk.kind = std::move(sk);
    return blk;
}

std::optional<Result<Block>> BlockReader::next() {
    if (finished_) return std::nullopt;

    // Step 1: 2-byte channel index. Clean EOF is the regular end of
    // the data section.
    std::uint8_t ci_buf[2];
    {
        auto r = stream_read_n(*stream_, ci_buf, 2);
        if (!r) { finished_ = true; return Result<Block>{tl::make_unexpected(r.error())}; }
        if (!*r) { finished_ = true; return std::nullopt; }
    }
    std::uint16_t const channel_index = le_u16(ci_buf);
    stats_.data_section_size_bytes += 2;

    // Step 2: optional 0xFFFF trailer block.
    if (channel_index == TRAILER_CHANNEL_INDEX) {
        auto r = consume_trailer();
        if (!r) { finished_ = true; return Result<Block>{tl::make_unexpected(r.error())}; }
        return std::nullopt;
    }

    // Step 3: channel lookup. An unknown index is a corruption signal
    // (we cannot even know how wide the length prefix should be).
    auto it = channels_.find(channel_index);
    if (it == channels_.end()) {
        finished_ = true;
        std::ostringstream oss;
        oss << "block references unknown channel index " << channel_index;
        return Result<Block>{tl::make_unexpected(
            Error{Error::Code::UnknownChannelIndex, oss.str()})};
    }
    ChannelInfo const& info = it->second;

    // Step 4: per-channel length prefix.
    auto len_r = read_length_field(info.size_of_length_value);
    if (!len_r) { finished_ = true; return Result<Block>{tl::make_unexpected(len_r.error())}; }
    if (!*len_r) {
        if (stats_.blocks_truncated < 1) stats_.blocks_truncated = 1;
        finished_ = true;
        return std::nullopt;
    }
    std::uint32_t const length = **len_r;
    stats_.data_section_size_bytes += info.size_of_length_value;

    if (length == 0) {
        SkipReason const reason{SkipReason::Kind::ReservedBlockType, 0};
        record_skip(channel_index, 0, reason);
        Block blk;
        blk.channel_index = channel_index;
        Skipped sk;
        sk.reason = reason;
        sk.bytes_skipped = 0;
        blk.kind = std::move(sk);
        return Result<Block>{std::move(blk)};
    }

    std::size_t const length_usize = length;

    // Step 5: forward-compat skip — Unsupported channel.
    if (auto reason = channel_unsupported_reason(info.channel_type, info.data_type)) {
        auto r = skip_block(channel_index, length_usize, *reason);
        if (!r) { finished_ = true; return Result<Block>{tl::make_unexpected(r.error())}; }
        return Result<Block>{std::move(*r)};
    }

    // Step 6: pull the full payload.
    auto payload_r = read_payload(length_usize);
    if (!payload_r) { finished_ = true; return Result<Block>{tl::make_unexpected(payload_r.error())}; }
    if (!*payload_r) {
        if (stats_.blocks_truncated < 1) stats_.blocks_truncated = 1;
        finished_ = true;
        return std::nullopt;
    }
    std::vector<std::uint8_t> payload = std::move(**payload_r);
    stats_.data_section_size_bytes += length;

    // Step 7: decode control byte and route.
    std::uint8_t const control_raw = payload[0];
    ControlByte const cb = decode_control_byte(control_raw);
    std::uint8_t const* body = payload.data() + 1;
    std::size_t const body_len = payload.size() - 1;

    auto make_skipped = [&](SkipReason::Kind kind, std::uint8_t raw) {
        SkipReason const reason{kind, raw};
        record_skip(channel_index, length, reason);
        Block blk;
        blk.channel_index = channel_index;
        Skipped sk;
        sk.reason = reason;
        sk.bytes_skipped = length;
        if (capture_skipped_) {
            sk.payload = std::vector<std::uint8_t>(body, body + body_len);
        }
        blk.kind = std::move(sk);
        return blk;
    };

    switch (cb.kind) {
        case ControlKind::Reserved:
        case ControlKind::TimebaseRealign:
            return Result<Block>{make_skipped(
                SkipReason::Kind::ReservedBlockType, cb.raw)};
        case ControlKind::TrustedTimestamp:
        case ControlKind::StatusEvent:
        case ControlKind::MessageEvent:
            return Result<Block>{make_skipped(
                SkipReason::Kind::DeprecatedBlockType, cb.raw)};
        case ControlKind::Unknown:
            return Result<Block>{make_skipped(
                SkipReason::Kind::ReservedBlockType, cb.raw)};

        case ControlKind::StartData: {
            auto r = parse_start_data(body, body_len, info.data_type,
                                      cb.multi_sample);
            if (!r) return Result<Block>{tl::make_unexpected(r.error())};
            std::size_t const n = numeric_payload_len(r->samples);
            auto& cs = stats_.per_channel[channel_index];
            ++cs.blocks_read;
            cs.bytes_payload += length;
            cs.samples_total += n;
            ++cs.segments;
            cs.observe_timestamp(r->start_timestamp_ns);
            if (n > 0 && r->sample_rate_hz > 0.0) {
                double const span_ns =
                    (static_cast<double>(n - 1) / r->sample_rate_hz) * 1.0e9;
                std::int64_t const last = r->start_timestamp_ns +
                    static_cast<std::int64_t>(span_ns);
                cs.observe_timestamp(last);
            }
            ++stats_.blocks_read;
            Block blk;
            blk.channel_index = channel_index;
            StartData sd;
            sd.start_timestamp_ns = r->start_timestamp_ns;
            sd.sample_rate_hz = r->sample_rate_hz;
            sd.samples = std::move(r->samples);
            blk.kind = std::move(sd);
            return Result<Block>{std::move(blk)};
        }
        case ControlKind::ContinuedData: {
            auto r = parse_continued_data(body, body_len, info.data_type,
                                          cb.multi_sample);
            if (!r) return Result<Block>{tl::make_unexpected(r.error())};
            std::size_t const n = numeric_payload_len(*r);
            auto& cs = stats_.per_channel[channel_index];
            ++cs.blocks_read;
            cs.bytes_payload += length;
            cs.samples_total += n;
            ++stats_.blocks_read;
            Block blk;
            blk.channel_index = channel_index;
            ContinuedData cd;
            cd.samples = std::move(*r);
            blk.kind = std::move(cd);
            return Result<Block>{std::move(blk)};
        }
        case ControlKind::AbsTimeStampData: {
            auto r = parse_abs_timestamp_data(body, body_len, info.data_type,
                                              cb.multi_sample, osf_version_);
            if (!r) return Result<Block>{tl::make_unexpected(r.error())};
            std::size_t const n = timestamped_payload_len(*r);
            auto& cs = stats_.per_channel[channel_index];
            ++cs.blocks_read;
            cs.bytes_payload += length;
            cs.samples_total += n;
            if (auto range = abs_timestamp_range(*r)) {
                cs.observe_timestamp(range->first);
                cs.observe_timestamp(range->second);
            }
            ++stats_.blocks_read;
            Block blk;
            blk.channel_index = channel_index;
            AbsTimestampData ad;
            ad.samples = std::move(*r);
            blk.kind = std::move(ad);
            return Result<Block>{std::move(blk)};
        }
        case ControlKind::ContinuedRelStampData: {
            auto r = parse_continued_rel_stamp_data(body, body_len,
                                                    info.data_type,
                                                    cb.multi_sample);
            if (!r) return Result<Block>{tl::make_unexpected(r.error())};
            std::size_t const n = rel_timestamped_payload_len(*r);
            auto& cs = stats_.per_channel[channel_index];
            ++cs.blocks_read;
            cs.bytes_payload += length;
            cs.samples_total += n;
            ++stats_.blocks_read;
            Block blk;
            blk.channel_index = channel_index;
            ContinuedRelStampData rd;
            rd.samples = std::move(*r);
            blk.kind = std::move(rd);
            return Result<Block>{std::move(blk)};
        }
    }
    return Result<Block>{tl::make_unexpected(invalid_block(
        "BlockReader::next: unhandled ControlKind"))};
}

ReaderStats BlockReader::stats() const {
    ReaderStats s = stats_;
    s.elapsed = std::chrono::steady_clock::now() - started_;
    s.blocks_total = s.blocks_read + s.blocks_skipped_unsupported +
                     s.blocks_skipped_deprecated_type +
                     s.blocks_skipped_reserved_type;
    s.channels_with_data = 0;
    for (auto const& [_, cs] : s.per_channel) {
        if (cs.blocks_read + cs.blocks_skipped > 0) ++s.channels_with_data;
    }
    return s;
}

// =====================================================================
// BlockReader::Iterator
// =====================================================================

BlockReader::Iterator::Iterator(BlockReader& reader) : reader_(&reader) {
    current_ = reader_->next();
}

BlockReader::Iterator& BlockReader::Iterator::operator++() {
    if (reader_) current_ = reader_->next();
    return *this;
}

void BlockReader::Iterator::operator++(int) { ++(*this); }

}  // namespace osf
