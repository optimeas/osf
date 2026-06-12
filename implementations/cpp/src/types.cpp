// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <osf/types.h>

#include <string>
#include <utility>

namespace osf {

namespace {

Error make_removed_in_spec(std::string_view field,
                           std::string_view value,
                           std::string_view replacement) {
    std::string msg;
    msg.reserve(field.size() + value.size() + replacement.size() + 96);
    msg.append("field \"").append(field).append("\" uses \"")
       .append(value).append("\", removed in spec revision 2026-05-04");
    if (!replacement.empty()) {
        msg.append(" — replacement: \"").append(replacement).append("\"");
    }
    return Error{Error::Code::RemovedInSpec, std::move(msg)};
}

}  // namespace

Result<DataType> parse_data_type(std::string_view raw) {
    if (raw == "bool")        return DataType::Bool;
    if (raw == "int8")        return DataType::Int8;
    if (raw == "int16")       return DataType::Int16;
    if (raw == "int32")       return DataType::Int32;
    if (raw == "int64")       return DataType::Int64;
    if (raw == "uint8")       return DataType::UInt8;
    if (raw == "uint16")      return DataType::UInt16;
    if (raw == "uint32")      return DataType::UInt32;
    if (raw == "uint64")      return DataType::UInt64;
    if (raw == "float")       return DataType::Float;
    if (raw == "double")      return DataType::Double;
    if (raw == "string")      return DataType::String;
    if (raw == "binary")      return DataType::Binary;
    if (raw == "gpslocation") return DataType::GpsLocation;

    // Read-side alias: normalise bytearray to Binary. Writers always
    // emit "binary"; this branch only covers files produced by other
    // toolchains. The owning Channel preserves the raw spelling on
    // data_type_raw so callers can still see it was spelled differently.
    if (raw == "bytearray") return DataType::Binary;

    // Removed in spec revision 2026-05-04 (DECISIONS §16). Rejected
    // because the binary blocks for these datatypes cannot be decoded
    // by a current build — silently mapping to a current type would
    // produce wrong data, not best-effort recovery.
    if (raw == "gpsdata") {
        return tl::make_unexpected(
            make_removed_in_spec("datatype", raw, "gpslocation"));
    }
    if (raw == "pair" || raw == "triple") {
        return tl::make_unexpected(
            make_removed_in_spec("datatype", raw,
                                 "two or three separate double channels"));
    }
    if (raw == "candata") {
        return tl::make_unexpected(
            make_removed_in_spec("datatype", raw,
                                 "binary with an application-specific MIME type"));
    }

    // Unknown spelling: forward-compatible fallback. The channel parses
    // but block reads against it will fail explicitly when attempted.
    return DataType::Unsupported;
}

Result<ChannelType> parse_channel_type(std::string_view raw) {
    if (raw == "scalar")      return ChannelType::Scalar;
    if (raw == "equidistant") return ChannelType::Equidistant;
    if (raw == "timestamped") return ChannelType::Timestamped;
    // No removed channel-type strings in spec revision 2026-05-04.
    // Result return type is reserved for a future revision.
    return ChannelType::Unsupported;
}

SpectrumType parse_spectrum_type(std::string_view raw) noexcept {
    if (raw == "amplitude")   return SpectrumType::Amplitude;
    if (raw == "realimag")    return SpectrumType::RealImag;
    if (raw == "ampphaserad") return SpectrumType::AmpPhaseRad;
    if (raw == "ampphasedeg") return SpectrumType::AmpPhaseDeg;
    return SpectrumType::Amplitude;
}

}  // namespace osf
