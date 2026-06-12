// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <osf/header.h>

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
Result<std::string> readFirstLine(std::istream& in) {
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

Result<OsfVersion> identifierToVersion(std::string_view identifier) {
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

Result<MagicHeader> parseMagicHeaderLine(std::string_view line) {
    auto sep = line.find(' ');
    if (sep == std::string_view::npos) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidMagicHeader,
            "expected '<identifier> <length>', got: \"" + std::string{line} + "\""});
    }

    auto identifier = line.substr(0, sep);
    auto rest = line.substr(sep + 1);

    auto versionResult = identifierToVersion(identifier);
    if (!versionResult) {
        return tl::make_unexpected(std::move(versionResult).error());
    }

    // Trim trailing whitespace; an empty rest after trimming means
    // the length is missing.
    auto end = rest.find_last_not_of(" \t");
    if (end == std::string_view::npos) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidMagicHeader,
            "missing metablock length after identifier \"" + std::string{identifier} + "\""});
    }
    auto lenStr = rest.substr(0, end + 1);

    std::uint64_t metablockLen = 0;
    auto [ptr, ec] = std::from_chars(lenStr.data(),
                                     lenStr.data() + lenStr.size(),
                                     metablockLen);
    if (ec != std::errc{} || ptr != lenStr.data() + lenStr.size()) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidMagicHeader,
            "metablock length is not a valid uint64: \"" + std::string{lenStr} + "\""});
    }

    return MagicHeader{*versionResult, metablockLen};
}

}  // anonymous namespace

Result<MagicHeader> parseMagicHeader(std::istream& in) {
    auto lineResult = readFirstLine(in);
    if (!lineResult) {
        return tl::make_unexpected(std::move(lineResult).error());
    }
    return parseMagicHeaderLine(*lineResult);
}

Result<MagicHeader> parseMagicHeader(std::uint8_t const* data, std::size_t size) {
    std::string buf{reinterpret_cast<char const*>(data), size};
    std::istringstream stream{std::move(buf)};
    return parseMagicHeader(static_cast<std::istream&>(stream));
}

Result<MagicHeader> parseMagicHeader(std::filesystem::path const& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream.is_open()) {
        return tl::make_unexpected(Error{
            Error::Code::IoError,
            "failed to open file: " + path.string()});
    }
    return parseMagicHeader(static_cast<std::istream&>(stream));
}

}  // namespace osf
