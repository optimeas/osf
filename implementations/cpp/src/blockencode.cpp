// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "blockencode_p.h"
#include "binaryio_p.h"

#include <cstring>

namespace osf::detail {
namespace {

// Reserve and append the [channel_index][payload_length] frame
// prefix. Returns the offset at which the caller should start
// writing the control byte. Validates sizeoflengthvalue and that
// payload_length fits.
//
// payload_length must already include the control byte.
Result<std::size_t> begin_frame(std::vector<std::uint8_t>& out,
                                std::uint16_t channel_index,
                                std::uint8_t sizeoflengthvalue,
                                std::uint64_t payload_length) {
    if (sizeoflengthvalue != 2 && sizeoflengthvalue != 4) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "sizeoflengthvalue must be 2 or 4"});
    }
    std::uint64_t const max_payload =
        (sizeoflengthvalue == 2) ? std::uint64_t{0xFFFFu}
                                 : std::uint64_t{0xFFFFFFFFu};
    if (payload_length > max_payload) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidBlock,
            "payload too large for sizeoflengthvalue"});
    }

    std::size_t const ctrl_offset =
        out.size() + 2u + sizeoflengthvalue;
    out.resize(out.size() + 2u + sizeoflengthvalue);

    // channel_index
    write_le_u16(out.data() + ctrl_offset - 2u - sizeoflengthvalue,
                 channel_index);
    // payload_length
    if (sizeoflengthvalue == 2) {
        write_le_u16(out.data() + ctrl_offset - 2u,
                     static_cast<std::uint16_t>(payload_length));
    } else {
        write_le_u32(out.data() + ctrl_offset - 4u,
                     static_cast<std::uint32_t>(payload_length));
    }
    return ctrl_offset;
}

// Write one sample of type T at the end of `out` (extends the vector).
template <typename T>
void append_sample(std::vector<std::uint8_t>& out, T v) {
    std::uint8_t buf[sizeof(T)];
    if constexpr (std::is_same_v<T, bool>) {
        buf[0] = v ? 1 : 0;
    } else if constexpr (std::is_same_v<T, std::int8_t>) {
        write_le_i8(buf, v);
    } else if constexpr (std::is_same_v<T, std::uint8_t>) {
        buf[0] = v;
    } else if constexpr (std::is_same_v<T, std::int16_t>) {
        write_le_i16(buf, v);
    } else if constexpr (std::is_same_v<T, std::uint16_t>) {
        write_le_u16(buf, v);
    } else if constexpr (std::is_same_v<T, std::int32_t>) {
        write_le_i32(buf, v);
    } else if constexpr (std::is_same_v<T, std::uint32_t>) {
        write_le_u32(buf, v);
    } else if constexpr (std::is_same_v<T, std::int64_t>) {
        write_le_i64(buf, v);
    } else if constexpr (std::is_same_v<T, std::uint64_t>) {
        write_le_u64(buf, v);
    } else if constexpr (std::is_same_v<T, float>) {
        write_le_f32(buf, v);
    } else if constexpr (std::is_same_v<T, double>) {
        write_le_f64(buf, v);
    } else {
        static_assert(sizeof(T) == 0, "append_sample: unsupported T");
    }
    out.insert(out.end(), std::begin(buf), std::end(buf));
}

}  // anonymous namespace

template <typename T>
Result<void> encode_start_data(std::vector<std::uint8_t>& out,
                               std::uint16_t channel_index,
                               std::uint8_t sizeoflengthvalue,
                               std::int64_t start_timestamp_ns,
                               double sample_rate_hz,
                               T const* samples,
                               std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "encode_start_data: count must be > 0"});
    }
    bool const multi = count > 1;
    // 1 (ctrl) + 8 (ts) + 8 (rate) + (4 if multi) + count * sizeof(T)
    std::uint64_t const payload_len =
        1u + 8u + 8u + (multi ? 4u : 0u) + count * sizeof(T);

    auto begin = begin_frame(out, channel_index, sizeoflengthvalue, payload_len);
    if (!begin) return tl::make_unexpected(begin.error());

    std::uint8_t const ctrl = static_cast<std::uint8_t>(
        0x06 | (multi ? 0x80 : 0x00));   // bcStartData = 6 per block.h

    // Pre-allocate for the body.
    out.reserve(out.size() + payload_len);
    out.push_back(ctrl);

    std::uint8_t buf8[8];
    write_le_i64(buf8, start_timestamp_ns);
    out.insert(out.end(), std::begin(buf8), std::end(buf8));
    write_le_f64(buf8, sample_rate_hz);
    out.insert(out.end(), std::begin(buf8), std::end(buf8));

    if (multi) {
        std::uint8_t buf4[4];
        write_le_u32(buf4, static_cast<std::uint32_t>(count));
        out.insert(out.end(), std::begin(buf4), std::end(buf4));
    }
    for (std::size_t i = 0; i < count; ++i) {
        append_sample<T>(out, samples[i]);
    }
    return {};
}

template <typename T>
Result<void> encode_continued_data(std::vector<std::uint8_t>& out,
                                   std::uint16_t channel_index,
                                   std::uint8_t sizeoflengthvalue,
                                   T const* samples,
                                   std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "encode_continued_data: count must be > 0"});
    }
    bool const multi = count > 1;
    std::uint64_t const payload_len =
        1u + (multi ? 4u : 0u) + count * sizeof(T);

    auto begin = begin_frame(out, channel_index, sizeoflengthvalue, payload_len);
    if (!begin) return tl::make_unexpected(begin.error());

    std::uint8_t const ctrl = static_cast<std::uint8_t>(
        0x05 | (multi ? 0x80 : 0x00));   // bcContinuedData = 5 per block.h
    out.reserve(out.size() + payload_len);
    out.push_back(ctrl);

    if (multi) {
        std::uint8_t buf4[4];
        write_le_u32(buf4, static_cast<std::uint32_t>(count));
        out.insert(out.end(), std::begin(buf4), std::end(buf4));
    }
    for (std::size_t i = 0; i < count; ++i) {
        append_sample<T>(out, samples[i]);
    }
    return {};
}

#define OSF_INSTANTIATE_EQUIDISTANT(T)                                       \
    template Result<void> encode_start_data<T>(                              \
        std::vector<std::uint8_t>&, std::uint16_t, std::uint8_t,             \
        std::int64_t, double, T const*, std::size_t);                        \
    template Result<void> encode_continued_data<T>(                          \
        std::vector<std::uint8_t>&, std::uint16_t, std::uint8_t,             \
        T const*, std::size_t)

OSF_INSTANTIATE_EQUIDISTANT(float);
OSF_INSTANTIATE_EQUIDISTANT(double);

#undef OSF_INSTANTIATE_EQUIDISTANT

template <typename T>
Result<void> encode_abs_timestamp_data(std::vector<std::uint8_t>& out,
                                       std::uint16_t channel_index,
                                       std::uint8_t sizeoflengthvalue,
                                       std::int64_t const* timestamps_ns,
                                       T const* samples,
                                       std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "encode_abs_timestamp_data: count must be > 0"});
    }
    bool const multi = count > 1;
    // Per-pair size: i64 ts + one sample.
    std::uint64_t const pair_bytes = 8u + sizeof(T);
    std::uint64_t const payload_len =
        1u + (multi ? 4u : 0u) + count * pair_bytes;

    auto begin = begin_frame(out, channel_index, sizeoflengthvalue, payload_len);
    if (!begin) return tl::make_unexpected(begin.error());

    std::uint8_t const ctrl = static_cast<std::uint8_t>(
        0x08 | (multi ? 0x80 : 0x00));   // bcAbsTimeStampData = 8 per block.h
    out.reserve(out.size() + payload_len);
    out.push_back(ctrl);

    if (multi) {
        std::uint8_t buf4[4];
        write_le_u32(buf4, static_cast<std::uint32_t>(count));
        out.insert(out.end(), std::begin(buf4), std::end(buf4));
    }

    std::uint8_t tsbuf[8];
    for (std::size_t i = 0; i < count; ++i) {
        write_le_i64(tsbuf, timestamps_ns[i]);
        out.insert(out.end(), std::begin(tsbuf), std::end(tsbuf));
        append_sample<T>(out, samples[i]);
    }
    return {};
}

#define OSF_INSTANTIATE_TIMESTAMPED(T)                                       \
    template Result<void> encode_abs_timestamp_data<T>(                      \
        std::vector<std::uint8_t>&, std::uint16_t, std::uint8_t,             \
        std::int64_t const*, T const*, std::size_t)

OSF_INSTANTIATE_TIMESTAMPED(bool);
OSF_INSTANTIATE_TIMESTAMPED(std::int8_t);
OSF_INSTANTIATE_TIMESTAMPED(std::int16_t);
OSF_INSTANTIATE_TIMESTAMPED(std::int32_t);
OSF_INSTANTIATE_TIMESTAMPED(std::int64_t);
OSF_INSTANTIATE_TIMESTAMPED(std::uint8_t);
OSF_INSTANTIATE_TIMESTAMPED(std::uint16_t);
OSF_INSTANTIATE_TIMESTAMPED(std::uint32_t);
OSF_INSTANTIATE_TIMESTAMPED(std::uint64_t);
OSF_INSTANTIATE_TIMESTAMPED(float);
OSF_INSTANTIATE_TIMESTAMPED(double);

#undef OSF_INSTANTIATE_TIMESTAMPED

Result<void> encode_abs_timestamp_data(std::vector<std::uint8_t>& out,
                                       std::uint16_t channel_index,
                                       std::uint8_t sizeoflengthvalue,
                                       std::int64_t timestamp_ns,
                                       std::string_view sample) {
    // Single-sample only; bit-7 = 0, no N-prefix, no trailing 0x00.
    std::uint64_t const payload_len = 1u + 8u + sample.size();

    auto begin = begin_frame(out, channel_index, sizeoflengthvalue, payload_len);
    if (!begin) return tl::make_unexpected(begin.error());

    out.reserve(out.size() + payload_len);
    out.push_back(0x08);                     // bcAbsTimeStampData, bit-7 clear

    std::uint8_t tsbuf[8];
    write_le_i64(tsbuf, timestamp_ns);
    out.insert(out.end(), std::begin(tsbuf), std::end(tsbuf));

    // Payload bytes verbatim — OSF5 writer per spec rev 2026-05-24.
    std::uint8_t const* p = reinterpret_cast<std::uint8_t const*>(sample.data());
    out.insert(out.end(), p, p + sample.size());
    return {};
}

Result<void> encode_abs_timestamp_data(std::vector<std::uint8_t>& out,
                                       std::uint16_t channel_index,
                                       std::uint8_t sizeoflengthvalue,
                                       std::int64_t timestamp_ns,
                                       BinarySample sample) {
    std::uint64_t const payload_len = 1u + 8u + sample.size;

    auto begin = begin_frame(out, channel_index, sizeoflengthvalue, payload_len);
    if (!begin) return tl::make_unexpected(begin.error());

    out.reserve(out.size() + payload_len);
    out.push_back(0x08);                     // bcAbsTimeStampData, bit-7 clear

    std::uint8_t tsbuf[8];
    write_le_i64(tsbuf, timestamp_ns);
    out.insert(out.end(), std::begin(tsbuf), std::end(tsbuf));

    out.insert(out.end(), sample.data, sample.data + sample.size);
    return {};
}

Result<void> encode_abs_timestamp_data_gps(std::vector<std::uint8_t>& out,
                                          std::uint16_t channel_index,
                                          std::uint8_t sizeoflengthvalue,
                                          std::int64_t const* timestamps_ns,
                                          GpsLocation const* samples,
                                          std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "encode_abs_timestamp_data_gps: count must be > 0"});
    }
    bool const multi = count > 1;
    // Per-sample: 8 (ts) + 24 (lat/lon/alt) = 32 bytes.
    std::uint64_t const payload_len =
        1u + (multi ? 4u : 0u) + count * 32u;

    auto begin = begin_frame(out, channel_index, sizeoflengthvalue, payload_len);
    if (!begin) return tl::make_unexpected(begin.error());

    std::uint8_t const ctrl = static_cast<std::uint8_t>(
        0x08 | (multi ? 0x80 : 0x00));   // bcAbsTimeStampData = 8 per block.h
    out.reserve(out.size() + payload_len);
    out.push_back(ctrl);

    if (multi) {
        std::uint8_t buf4[4];
        write_le_u32(buf4, static_cast<std::uint32_t>(count));
        out.insert(out.end(), std::begin(buf4), std::end(buf4));
    }

    std::uint8_t buf8[8];
    for (std::size_t i = 0; i < count; ++i) {
        write_le_i64(buf8, timestamps_ns[i]);
        out.insert(out.end(), std::begin(buf8), std::end(buf8));
        write_le_f64(buf8, samples[i].latitude);
        out.insert(out.end(), std::begin(buf8), std::end(buf8));
        write_le_f64(buf8, samples[i].longitude);
        out.insert(out.end(), std::begin(buf8), std::end(buf8));
        write_le_f64(buf8, samples[i].altitude);
        out.insert(out.end(), std::begin(buf8), std::end(buf8));
    }
    return {};
}

}  // namespace osf::detail
