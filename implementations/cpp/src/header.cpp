// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <osf/header.h>

#include <charconv>
#include <fstream>
#include <istream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

// A `key` is 1*(lowercase a-z / 0-9 / '-').
bool isValidTokenKey(std::string_view key) {
    if (key.empty()) {
        return false;
    }
    for (char c : key) {
        bool const ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool isHex(std::string_view s, std::size_t len, bool upper) {
    if (s.size() != len) {
        return false;
    }
    for (char c : s) {
        bool const digit = c >= '0' && c <= '9';
        bool const hex = upper ? (c >= 'A' && c <= 'F') : (c >= 'a' && c <= 'f');
        if (!digit && !hex) {
            return false;
        }
    }
    return true;
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
    OsfVersion const version = *versionResult;

    // Split `rest` on single spaces. The header grammar is
    // `identifier SP metablock-len *(SP token) LF` with exactly one space
    // between fields and no trailing space, so an empty field is malformed.
    std::vector<std::string_view> fields;
    for (std::size_t start = 0;;) {
        auto space = rest.find(' ', start);
        if (space == std::string_view::npos) {
            fields.push_back(rest.substr(start));
            break;
        }
        fields.push_back(rest.substr(start, space - start));
        start = space + 1;
    }

    std::string_view const lenStr = fields.front();
    if (lenStr.empty()) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidMagicHeader,
            "missing metablock length after identifier \"" + std::string{identifier} + "\""});
    }
    std::uint64_t metablockLen = 0;
    auto [ptr, ec] = std::from_chars(lenStr.data(), lenStr.data() + lenStr.size(), metablockLen);
    if (ec != std::errc{} || ptr != lenStr.data() + lenStr.size()) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidMagicHeader,
            "metablock length is not a valid uint64: \"" + std::string{lenStr} + "\""});
    }

    IntegrityProfile integrity = IntegrityProfile::None;
    std::optional<std::uint32_t> metablockCrc;
    bool sawCrc = false;

    for (std::size_t i = 1; i < fields.size(); ++i) {
        std::string_view const token = fields[i];
        if (token.empty()) {
            return tl::make_unexpected(Error{
                Error::Code::InvalidMagicHeader,
                "malformed magic header: fields must be separated by a single space "
                "with no trailing space"});
        }
        // Tokens are an OSF5-only feature.
        if (version != OsfVersion::Osf5) {
            return tl::make_unexpected(Error{
                Error::Code::InvalidMagicHeader,
                "header tokens are only allowed for OSF5; found \"" + std::string{token} +
                    "\" after an OSF4 identifier"});
        }
        auto colon = token.find(':');
        std::string_view const key = colon == std::string_view::npos
                                          ? token
                                          : token.substr(0, colon);
        std::string_view const value = colon == std::string_view::npos
                                           ? std::string_view{}
                                           : token.substr(colon + 1);
        if (!isValidTokenKey(key)) {
            return tl::make_unexpected(Error{
                Error::Code::InvalidMagicHeader,
                "malformed header token key: \"" + std::string{key} + "\""});
        }
        if (key == "crc32c") {
            if (!isHex(value, 8, /*upper=*/true)) {
                return tl::make_unexpected(Error{
                    Error::Code::InvalidMagicHeader,
                    "crc32c token value must be 8 uppercase hex digits, got \"" +
                        std::string{value} + "\""});
            }
            std::uint32_t crc = 0;
            std::from_chars(value.data(), value.data() + value.size(), crc, 16);
            metablockCrc = crc;
            integrity = IntegrityProfile::Crc32c;
            sawCrc = true;
        } else if (key == "ed25519") {
            if (!isHex(value, 16, /*upper=*/false)) {
                return tl::make_unexpected(Error{
                    Error::Code::InvalidMagicHeader,
                    "ed25519 keyid must be 16 lowercase hex digits, got \"" +
                        std::string{value} + "\""});
            }
            if (!sawCrc) {
                return tl::make_unexpected(Error{
                    Error::Code::InvalidMagicHeader,
                    "ed25519 token is only valid after a crc32c token (crc32c first)"});
            }
            integrity = IntegrityProfile::Ed25519;
        } else {
            return tl::make_unexpected(Error{
                Error::Code::UnknownHeaderToken,
                "unknown header token '" + std::string{key} + "'"});
        }
    }

    return MagicHeader{version, metablockLen, integrity, metablockCrc};
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
