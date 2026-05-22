// SPDX-License-Identifier: MIT
//
// osf::Error and osf::Result<T> — the foundation error type used by
// every fallible operation in the OSF C++ library.
//
// See DECISIONS.md §20 for the rationale (Result<T> as the core API,
// throwing wrappers as opt-in).

#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <tl/expected.hpp>

namespace osf {

struct Error {
    enum class Code {
        Unknown,
        InvalidArgument,
        IoError,
        ParseError,
        NotFound,
        InvalidMagicHeader,
        UnsupportedVersion,
        MagicHeaderTooLong,
        /// Metablock body was structurally malformed: missing required
        /// field, unparseable number, invalid `sizeoflengthvalue`, etc.
        /// The metablock is the contract for the binary blocks that
        /// follow, so a malformed metablock rejects the whole file.
        InvalidMetablock,
        /// Metablock referenced a string or datatype that was removed in
        /// spec revision 2026-05-04 (`pair`, `triple`, `candata`,
        /// `gpsdata`). The block decoder cannot reproduce the obsolete
        /// payload layout from a current build, so this is rejected
        /// rather than best-effort decoded.
        RemovedInSpec,
        /// JSON parser (nlohmann::json) could not tokenise the metablock
        /// body. Carries the parser's diagnostic in `message`.
        JsonParseError,
        /// XML parser (pugixml) could not tokenise the metablock body.
        /// Carries the parser's diagnostic plus byte offset in `message`.
        XmlParseError,
    };

    Code code = Code::Unknown;
    std::string message;

    Error() = default;
    Error(Code c, std::string msg) : code(c), message(std::move(msg)) {}
};

template <typename T>
using Result = tl::expected<T, Error>;

// Returns a stable string identifier for the given Code, suitable
// for logging. The returned view points into static storage.
[[nodiscard]] std::string_view error_category_name(Error::Code code) noexcept;

}  // namespace osf
