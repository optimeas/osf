// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "blockencode_p.h"
#include "binaryio_p.h"

#include <cstring>

namespace osf::detail {
namespace {

// Reserve and append the [channelIndex][payloadLength] frame
// prefix. Returns the offset at which the caller should start
// writing the control byte. Validates sizeoflengthvalue and that
// payloadLength fits.
//
// payloadLength must already include the control byte.
Result<std::size_t> beginFrame(std::vector<std::uint8_t>& out,
                                std::uint16_t channelIndex,
                                std::uint8_t sizeoflengthvalue,
                                std::uint64_t payloadLength) {
    if (sizeoflengthvalue != 2 && sizeoflengthvalue != 4) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "sizeoflengthvalue must be 2 or 4"});
    }
    std::uint64_t const maxPayload =
        (sizeoflengthvalue == 2) ? std::uint64_t{0xFFFFu}
                                 : std::uint64_t{0xFFFFFFFFu};
    if (payloadLength > maxPayload) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidBlock,
            "payload too large for sizeoflengthvalue"});
    }

    std::size_t const ctrlOffset =
        out.size() + 2u + sizeoflengthvalue;
    out.resize(out.size() + 2u + sizeoflengthvalue);

    // channelIndex
    writeLeU16(out.data() + ctrlOffset - 2u - sizeoflengthvalue,
                 channelIndex);
    // payloadLength
    if (sizeoflengthvalue == 2) {
        writeLeU16(out.data() + ctrlOffset - 2u,
                     static_cast<std::uint16_t>(payloadLength));
    } else {
        writeLeU32(out.data() + ctrlOffset - 4u,
                     static_cast<std::uint32_t>(payloadLength));
    }
    return ctrlOffset;
}

// Write one sample of type T at the end of `out` (extends the vector).
template <typename T>
void appendSample(std::vector<std::uint8_t>& out, T v) {
    std::uint8_t buf[sizeof(T)];
    if constexpr (std::is_same_v<T, bool>) {
        buf[0] = v ? 1 : 0;
    } else if constexpr (std::is_same_v<T, std::int8_t>) {
        writeLeI8(buf, v);
    } else if constexpr (std::is_same_v<T, std::uint8_t>) {
        buf[0] = v;
    } else if constexpr (std::is_same_v<T, std::int16_t>) {
        writeLeI16(buf, v);
    } else if constexpr (std::is_same_v<T, std::uint16_t>) {
        writeLeU16(buf, v);
    } else if constexpr (std::is_same_v<T, std::int32_t>) {
        writeLeI32(buf, v);
    } else if constexpr (std::is_same_v<T, std::uint32_t>) {
        writeLeU32(buf, v);
    } else if constexpr (std::is_same_v<T, std::int64_t>) {
        writeLeI64(buf, v);
    } else if constexpr (std::is_same_v<T, std::uint64_t>) {
        writeLeU64(buf, v);
    } else if constexpr (std::is_same_v<T, float>) {
        writeLeF32(buf, v);
    } else if constexpr (std::is_same_v<T, double>) {
        writeLeF64(buf, v);
    } else {
        static_assert(sizeof(T) == 0, "appendSample: unsupported T");
    }
    out.insert(out.end(), std::begin(buf), std::end(buf));
}

}  // anonymous namespace

template <typename T>
Result<void> encodeStartData(std::vector<std::uint8_t>& out,
                               std::uint16_t channelIndex,
                               std::uint8_t sizeoflengthvalue,
                               std::int64_t startTimestampNs,
                               double sampleRateHz,
                               T const* samples,
                               std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "encodeStartData: count must be > 0"});
    }
    bool const multi = count > 1;
    // 1 (ctrl) + 8 (ts) + 8 (rate) + (4 if multi) + count * sizeof(T)
    std::uint64_t const payloadLen =
        1u + 8u + 8u + (multi ? 4u : 0u) + count * sizeof(T);

    auto begin = beginFrame(out, channelIndex, sizeoflengthvalue, payloadLen);
    if (!begin) return tl::make_unexpected(begin.error());

    std::uint8_t const ctrl = static_cast<std::uint8_t>(
        0x06 | (multi ? 0x80 : 0x00));   // bcStartData = 6 per block.h

    // Pre-allocate for the body.
    out.reserve(out.size() + payloadLen);
    out.push_back(ctrl);

    std::uint8_t buf8[8];
    writeLeI64(buf8, startTimestampNs);
    out.insert(out.end(), std::begin(buf8), std::end(buf8));
    writeLeF64(buf8, sampleRateHz);
    out.insert(out.end(), std::begin(buf8), std::end(buf8));

    if (multi) {
        std::uint8_t buf4[4];
        writeLeU32(buf4, static_cast<std::uint32_t>(count));
        out.insert(out.end(), std::begin(buf4), std::end(buf4));
    }
    for (std::size_t i = 0; i < count; ++i) {
        appendSample<T>(out, samples[i]);
    }
    return {};
}

template <typename T>
Result<void> encodeContinuedData(std::vector<std::uint8_t>& out,
                                   std::uint16_t channelIndex,
                                   std::uint8_t sizeoflengthvalue,
                                   T const* samples,
                                   std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "encodeContinuedData: count must be > 0"});
    }
    bool const multi = count > 1;
    std::uint64_t const payloadLen =
        1u + (multi ? 4u : 0u) + count * sizeof(T);

    auto begin = beginFrame(out, channelIndex, sizeoflengthvalue, payloadLen);
    if (!begin) return tl::make_unexpected(begin.error());

    std::uint8_t const ctrl = static_cast<std::uint8_t>(
        0x05 | (multi ? 0x80 : 0x00));   // bcContinuedData = 5 per block.h
    out.reserve(out.size() + payloadLen);
    out.push_back(ctrl);

    if (multi) {
        std::uint8_t buf4[4];
        writeLeU32(buf4, static_cast<std::uint32_t>(count));
        out.insert(out.end(), std::begin(buf4), std::end(buf4));
    }
    for (std::size_t i = 0; i < count; ++i) {
        appendSample<T>(out, samples[i]);
    }
    return {};
}

#define OSF_INSTANTIATE_EQUIDISTANT(T)                                       \
    template Result<void> encodeStartData<T>(                              \
        std::vector<std::uint8_t>&, std::uint16_t, std::uint8_t,             \
        std::int64_t, double, T const*, std::size_t);                        \
    template Result<void> encodeContinuedData<T>(                          \
        std::vector<std::uint8_t>&, std::uint16_t, std::uint8_t,             \
        T const*, std::size_t)

OSF_INSTANTIATE_EQUIDISTANT(float);
OSF_INSTANTIATE_EQUIDISTANT(double);

#undef OSF_INSTANTIATE_EQUIDISTANT

template <typename T>
Result<void> encodeAbsTimestampData(std::vector<std::uint8_t>& out,
                                       std::uint16_t channelIndex,
                                       std::uint8_t sizeoflengthvalue,
                                       std::int64_t const* timestampsNs,
                                       T const* samples,
                                       std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "encodeAbsTimestampData: count must be > 0"});
    }
    bool const multi = count > 1;
    // Per-pair size: i64 ts + one sample.
    std::uint64_t const pairBytes = 8u + sizeof(T);
    std::uint64_t const payloadLen =
        1u + (multi ? 4u : 0u) + count * pairBytes;

    auto begin = beginFrame(out, channelIndex, sizeoflengthvalue, payloadLen);
    if (!begin) return tl::make_unexpected(begin.error());

    std::uint8_t const ctrl = static_cast<std::uint8_t>(
        0x08 | (multi ? 0x80 : 0x00));   // bcAbsTimeStampData = 8 per block.h
    out.reserve(out.size() + payloadLen);
    out.push_back(ctrl);

    if (multi) {
        std::uint8_t buf4[4];
        writeLeU32(buf4, static_cast<std::uint32_t>(count));
        out.insert(out.end(), std::begin(buf4), std::end(buf4));
    }

    std::uint8_t tsbuf[8];
    for (std::size_t i = 0; i < count; ++i) {
        writeLeI64(tsbuf, timestampsNs[i]);
        out.insert(out.end(), std::begin(tsbuf), std::end(tsbuf));
        appendSample<T>(out, samples[i]);
    }
    return {};
}

#define OSF_INSTANTIATE_TIMESTAMPED(T)                                       \
    template Result<void> encodeAbsTimestampData<T>(                      \
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

Result<void> encodeAbsTimestampData(std::vector<std::uint8_t>& out,
                                       std::uint16_t channelIndex,
                                       std::uint8_t sizeoflengthvalue,
                                       std::int64_t timestampNs,
                                       std::string_view sample) {
    // Single-sample only; bit-7 = 0, no N-prefix, no trailing 0x00.
    std::uint64_t const payloadLen = 1u + 8u + sample.size();

    auto begin = beginFrame(out, channelIndex, sizeoflengthvalue, payloadLen);
    if (!begin) return tl::make_unexpected(begin.error());

    out.reserve(out.size() + payloadLen);
    out.push_back(0x08);                     // bcAbsTimeStampData, bit-7 clear

    std::uint8_t tsbuf[8];
    writeLeI64(tsbuf, timestampNs);
    out.insert(out.end(), std::begin(tsbuf), std::end(tsbuf));

    // Payload bytes verbatim — OSF5 writer per spec rev 2026-05-24.
    std::uint8_t const* p = reinterpret_cast<std::uint8_t const*>(sample.data());
    out.insert(out.end(), p, p + sample.size());
    return {};
}

Result<void> encodeAbsTimestampData(std::vector<std::uint8_t>& out,
                                       std::uint16_t channelIndex,
                                       std::uint8_t sizeoflengthvalue,
                                       std::int64_t timestampNs,
                                       BinarySample sample) {
    std::uint64_t const payloadLen = 1u + 8u + sample.size;

    auto begin = beginFrame(out, channelIndex, sizeoflengthvalue, payloadLen);
    if (!begin) return tl::make_unexpected(begin.error());

    out.reserve(out.size() + payloadLen);
    out.push_back(0x08);                     // bcAbsTimeStampData, bit-7 clear

    std::uint8_t tsbuf[8];
    writeLeI64(tsbuf, timestampNs);
    out.insert(out.end(), std::begin(tsbuf), std::end(tsbuf));

    out.insert(out.end(), sample.data, sample.data + sample.size);
    return {};
}

Result<void> encodeAbsTimestampDataGps(std::vector<std::uint8_t>& out,
                                          std::uint16_t channelIndex,
                                          std::uint8_t sizeoflengthvalue,
                                          std::int64_t const* timestampsNs,
                                          GpsLocation const* samples,
                                          std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "encodeAbsTimestampDataGps: count must be > 0"});
    }
    bool const multi = count > 1;
    // Per-sample: 8 (ts) + 24 (lat/lon/alt) = 32 bytes.
    std::uint64_t const payloadLen =
        1u + (multi ? 4u : 0u) + count * 32u;

    auto begin = beginFrame(out, channelIndex, sizeoflengthvalue, payloadLen);
    if (!begin) return tl::make_unexpected(begin.error());

    std::uint8_t const ctrl = static_cast<std::uint8_t>(
        0x08 | (multi ? 0x80 : 0x00));   // bcAbsTimeStampData = 8 per block.h
    out.reserve(out.size() + payloadLen);
    out.push_back(ctrl);

    if (multi) {
        std::uint8_t buf4[4];
        writeLeU32(buf4, static_cast<std::uint32_t>(count));
        out.insert(out.end(), std::begin(buf4), std::end(buf4));
    }

    std::uint8_t buf8[8];
    for (std::size_t i = 0; i < count; ++i) {
        writeLeI64(buf8, timestampsNs[i]);
        out.insert(out.end(), std::begin(buf8), std::end(buf8));
        writeLeF64(buf8, samples[i].latitude);
        out.insert(out.end(), std::begin(buf8), std::end(buf8));
        writeLeF64(buf8, samples[i].longitude);
        out.insert(out.end(), std::begin(buf8), std::end(buf8));
        writeLeF64(buf8, samples[i].altitude);
        out.insert(out.end(), std::begin(buf8), std::end(buf8));
    }
    return {};
}

}  // namespace osf::detail
