// SPDX-License-Identifier: Apache-2.0
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
