// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <osf/block.h>

namespace osf {

namespace {

template <typename Variant>
std::size_t variant_vector_len(Variant const& v) noexcept {
    return std::visit([](auto const& vec) noexcept { return vec.size(); }, v);
}

}  // anonymous namespace

std::size_t numericPayloadLen(NumericPayload const& p) noexcept {
    return variant_vector_len(p);
}

bool numericPayloadEmpty(NumericPayload const& p) noexcept {
    return numericPayloadLen(p) == 0;
}

std::size_t timestampedPayloadLen(TimestampedPayload const& p) noexcept {
    return variant_vector_len(p);
}

bool timestampedPayloadEmpty(TimestampedPayload const& p) noexcept {
    return timestampedPayloadLen(p) == 0;
}

std::size_t relTimestampedPayloadLen(RelTimestampedPayload const& p) noexcept {
    return variant_vector_len(p);
}

bool relTimestampedPayloadEmpty(RelTimestampedPayload const& p) noexcept {
    return relTimestampedPayloadLen(p) == 0;
}

ControlByte decodeControlByte(std::uint8_t byte) noexcept {
    ControlByte out;
    out.multiSample = (byte & 0x80u) != 0;
    out.raw = static_cast<std::uint8_t>(byte & 0x7Fu);
    switch (out.raw) {
        case 0: out.kind = ControlKind::Reserved;              break;
        case 1: out.kind = ControlKind::TrustedTimestamp;      break;
        case 2: out.kind = ControlKind::TimebaseRealign;       break;
        case 3: out.kind = ControlKind::StatusEvent;           break;
        case 4: out.kind = ControlKind::MessageEvent;          break;
        case 5: out.kind = ControlKind::ContinuedData;         break;
        case 6: out.kind = ControlKind::StartData;             break;
        case 7: out.kind = ControlKind::ContinuedRelStampData; break;
        case 8: out.kind = ControlKind::AbsTimeStampData;      break;
        default: out.kind = ControlKind::Unknown;              break;
    }
    return out;
}

}  // namespace osf
