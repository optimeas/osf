// SPDX-License-Identifier: MIT

/// \file header.hpp
/// OSF magic-header detection.
///
/// Every OSF file starts with a single ASCII line that identifies
/// the version and announces the byte length of the metablock that
/// follows:
///
/// ```text
/// <IDENTIFIER> <metablock_length>\n
/// ```
///
/// Accepted identifiers:
///
/// | On disk                   | Maps to            |
/// |---------------------------|--------------------|
/// | `OSF4`                    | `OsfVersion::Osf4` |
/// | `OCEAN_STREAM_FORMAT4`    | `OsfVersion::Osf4` |
/// | `OCEAN_STREAMING_FORMAT4` | `OsfVersion::Osf4` |
/// | `OSF5`                    | `OsfVersion::Osf5` |
///
/// Optimeas devices emit `OCEAN_STREAM_FORMAT4` in production;
/// field files in `examples/` use it.
///
/// See `docs/de/osf_general.md` for the full specification and
/// `DECISIONS.md` §20 for the C++ implementation architecture.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>

#include <osf/error.hpp>

namespace osf {

/// Cap on the magic-header line length.
///
/// The longest valid line is `OCEAN_STREAMING_FORMAT4 ` (24 bytes)
/// + 20 digits of `std::uint64_t::max()` + `\n` = 45 bytes. 128
/// leaves comfortable headroom for unforeseen identifiers without
/// letting a corrupt or non-OSF file run away.
///
/// Soft limit semantics: the parser tolerates up to and including
/// MAX_MAGIC_HEADER_LEN bytes before the terminating newline; only
/// strictly more triggers `Error::Code::MagicHeaderTooLong`. Matches
/// the behaviour of the Rust reference implementation.
inline constexpr std::size_t MAX_MAGIC_HEADER_LEN = 128;

/// On-disk OSF format version, derived from the magic header.
enum class OsfVersion {
    /// OSF4: XML metablock, classic control-byte set, file trailer.
    Osf4,
    /// OSF5: JSON metablock, simplified control byte, no trailer.
    Osf5,
};

/// Parsed contents of the OSF magic-header line.
struct MagicHeader {
    /// Version detected from the identifier prefix. Default-constructed
    /// MagicHeader objects carry Osf4 as a placeholder; they are not
    /// valid headers — populate via parse_magic_header().
    OsfVersion version = OsfVersion::Osf4;
    /// Byte length of the metablock that immediately follows the
    /// terminating newline. Zero on default-constructed instances.
    std::uint64_t metablock_len = 0;

    friend bool operator==(MagicHeader const& a, MagicHeader const& b) noexcept {
        return a.version == b.version && a.metablock_len == b.metablock_len;
    }
    friend bool operator!=(MagicHeader const& a, MagicHeader const& b) noexcept {
        return !(a == b);
    }
};

/// Read and parse the magic-header line from `in`.
///
/// On success, the stream is positioned immediately after the
/// terminating `\n`, so the next read yields the first byte of
/// the metablock. A trailing `\r` before the `\n` (CRLF) is
/// silently tolerated even though the spec mandates LF only.
///
/// Possible errors (`Result::error().code`):
/// - `Error::Code::IoError` — the underlying stream signalled a
///   read failure or reached EOF before a `\n` was seen.
/// - `Error::Code::MagicHeaderTooLong` — no `\n` found within
///   `MAX_MAGIC_HEADER_LEN` bytes; almost certainly not an OSF
///   file.
/// - `Error::Code::InvalidMagicHeader` — line is malformed:
///   missing separator, missing length number, non-UTF-8 bytes,
///   or length is not parseable as `std::uint64_t`.
/// - `Error::Code::UnsupportedVersion` — line is parseable but
///   the identifier is none of the four accepted spellings.
[[nodiscard]] Result<MagicHeader> parse_magic_header(std::istream& in);

/// Buffer convenience overload. Internally constructs a
/// `std::stringstream` over `[data, data + size)` and delegates
/// to the `std::istream` form.
[[nodiscard]] Result<MagicHeader> parse_magic_header(std::uint8_t const* data,
                                                    std::size_t size);

/// Path convenience overload. Opens `path` in binary mode and
/// delegates to the `std::istream` form. The stream is closed
/// when the function returns.
[[nodiscard]] Result<MagicHeader> parse_magic_header(std::filesystem::path const& path);

}  // namespace osf
