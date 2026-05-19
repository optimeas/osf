// SPDX-License-Identifier: Apache-2.0

#include <osf/error.hpp>

namespace osf {

std::string_view error_category_name(Error::Code code) noexcept {
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
    }
    // fallback for cast-from-int values outside the declared enumerators
    return "Unknown";
}

}  // namespace osf
