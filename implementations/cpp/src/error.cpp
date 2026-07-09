// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <osf/error.h>

namespace osf {

std::string_view errorCategoryName(Error::Code code) noexcept {
    switch (code) {
        case Error::Code::Unknown:         return "Unknown";
        case Error::Code::InvalidArgument: return "InvalidArgument";
        case Error::Code::IoError:         return "IoError";
        case Error::Code::ParseError:      return "ParseError";
        case Error::Code::NotFound:        return "NotFound";
        case Error::Code::InvalidMagicHeader: return "InvalidMagicHeader";
        case Error::Code::UnsupportedVersion: return "UnsupportedVersion";
        case Error::Code::MagicHeaderTooLong: return "MagicHeaderTooLong";
        case Error::Code::InvalidMetablock:   return "InvalidMetablock";
        case Error::Code::RemovedInSpec:      return "RemovedInSpec";
        case Error::Code::JsonParseError:     return "JsonParseError";
        case Error::Code::XmlParseError:      return "XmlParseError";
        case Error::Code::UnknownChannelIndex:        return "UnknownChannelIndex";
        case Error::Code::InvalidBlock:               return "InvalidBlock";
        case Error::Code::ChannelMixedBlockTypes:     return "ChannelMixedBlockTypes";
        case Error::Code::ContinuedDataWithoutStart:  return "ContinuedDataWithoutStart";
        case Error::Code::RelStampWithoutAnchor:      return "RelStampWithoutAnchor";
        case Error::Code::DataTypeMismatch:           return "DataTypeMismatch";
        case Error::Code::UnknownHeaderToken:         return "UnknownHeaderToken";
        case Error::Code::MetablockCrcMismatch:       return "MetablockCrcMismatch";
    }
    // fallback for cast-from-int values outside the declared enumerators
    return "Unknown";
}

}  // namespace osf
