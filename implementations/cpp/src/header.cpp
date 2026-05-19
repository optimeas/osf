// SPDX-License-Identifier: MIT

#include <osf/header.hpp>

#include <charconv>
#include <fstream>
#include <istream>
#include <sstream>
#include <string>
#include <utility>

namespace osf {

namespace {

// Reads bytes from `in` up to '\n'. Strips a trailing '\r' if present.
// Errors:
//   - MagicHeaderTooLong   if no '\n' within MAX_MAGIC_HEADER_LEN bytes.
//   - IoError              on a real stream failure (badbit set).
//   - InvalidMagicHeader   on EOF before any '\n'.
Result<std::string> read_first_line(std::istream& in) {
    std::string buf;
    buf.reserve(48);

    while (true) {
        int c = in.get();
        if (c == std::char_traits<char>::eof()) {
            if (in.bad()) {
                return tl::make_unexpected(Error{
                    Error::Code::IoError,
                    "stream read failure before newline"});
            }
            return tl::make_unexpected(Error{
                Error::Code::InvalidMagicHeader,
                "unexpected end of input before newline"});
        }
        if (c == '\n') {
            break;
        }
        buf.push_back(static_cast<char>(c));
        if (buf.size() > MAX_MAGIC_HEADER_LEN) {
            return tl::make_unexpected(Error{
                Error::Code::MagicHeaderTooLong,
                "no newline within " + std::to_string(MAX_MAGIC_HEADER_LEN) + " bytes"});
        }
    }

    // Tolerate CRLF (spec is LF-only, but cost nothing).
    if (!buf.empty() && buf.back() == '\r') {
        buf.pop_back();
    }

    return buf;
}

Result<OsfVersion> identifier_to_version(std::string_view identifier) {
    if (identifier == "OSF4" ||
        identifier == "OCEAN_STREAM_FORMAT4" ||
        identifier == "OCEAN_STREAMING_FORMAT4") {
        return OsfVersion::Osf4;
    }
    if (identifier == "OSF5") {
        return OsfVersion::Osf5;
    }
    return tl::make_unexpected(Error{
        Error::Code::UnsupportedVersion,
        "unknown OSF identifier: " + std::string{identifier}});
}

Result<MagicHeader> parse_magic_header_line(std::string_view line) {
    auto sep = line.find(' ');
    if (sep == std::string_view::npos) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidMagicHeader,
            "expected '<identifier> <length>', got: \"" + std::string{line} + "\""});
    }

    auto identifier = line.substr(0, sep);
    auto rest = line.substr(sep + 1);

    auto version_result = identifier_to_version(identifier);
    if (!version_result) {
        return tl::make_unexpected(std::move(version_result).error());
    }

    // Trim trailing whitespace; an empty rest after trimming means
    // the length is missing.
    auto end = rest.find_last_not_of(" \t");
    if (end == std::string_view::npos) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidMagicHeader,
            "missing metablock length after identifier \"" + std::string{identifier} + "\""});
    }
    auto len_str = rest.substr(0, end + 1);

    std::uint64_t metablock_len = 0;
    auto [ptr, ec] = std::from_chars(len_str.data(),
                                     len_str.data() + len_str.size(),
                                     metablock_len);
    if (ec != std::errc{} || ptr != len_str.data() + len_str.size()) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidMagicHeader,
            "metablock length is not a valid uint64: \"" + std::string{len_str} + "\""});
    }

    return MagicHeader{*version_result, metablock_len};
}

}  // anonymous namespace

Result<MagicHeader> parse_magic_header(std::istream& in) {
    auto line_result = read_first_line(in);
    if (!line_result) {
        return tl::make_unexpected(std::move(line_result).error());
    }
    return parse_magic_header_line(*line_result);
}

Result<MagicHeader> parse_magic_header(std::uint8_t const* data, std::size_t size) {
    std::string buf{reinterpret_cast<char const*>(data), size};
    std::istringstream stream{std::move(buf)};
    return parse_magic_header(static_cast<std::istream&>(stream));
}

Result<MagicHeader> parse_magic_header(std::filesystem::path const& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream.is_open()) {
        return tl::make_unexpected(Error{
            Error::Code::IoError,
            "failed to open file: " + path.string()});
    }
    return parse_magic_header(static_cast<std::istream&>(stream));
}

}  // namespace osf
