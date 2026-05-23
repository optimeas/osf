// SPDX-License-Identifier: MIT

#include <osf/block.hpp>

namespace osf {

namespace {

template <typename Variant>
std::size_t variant_vector_len(Variant const& v) noexcept {
    return std::visit([](auto const& vec) noexcept { return vec.size(); }, v);
}

}  // anonymous namespace

std::size_t numeric_payload_len(NumericPayload const& p) noexcept {
    return variant_vector_len(p);
}

bool numeric_payload_empty(NumericPayload const& p) noexcept {
    return numeric_payload_len(p) == 0;
}

std::size_t timestamped_payload_len(TimestampedPayload const& p) noexcept {
    return variant_vector_len(p);
}

bool timestamped_payload_empty(TimestampedPayload const& p) noexcept {
    return timestamped_payload_len(p) == 0;
}

std::size_t rel_timestamped_payload_len(RelTimestampedPayload const& p) noexcept {
    return variant_vector_len(p);
}

bool rel_timestamped_payload_empty(RelTimestampedPayload const& p) noexcept {
    return rel_timestamped_payload_len(p) == 0;
}

ControlByte decode_control_byte(std::uint8_t byte) noexcept {
    ControlByte out;
    out.multi_sample = (byte & 0x80u) != 0;
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
