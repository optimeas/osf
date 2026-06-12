// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <osf/metablock.h>

#include <climits>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <pugixml.hpp>

namespace osf {

namespace {

Error invalidMetablock(std::string msg) {
    return Error{Error::Code::InvalidMetablock, std::move(msg)};
}

Error xmlParseError(std::string msg) {
    return Error{Error::Code::XmlParseError, std::move(msg)};
}

// Wire-attribute reads. pugixml returns an empty string when an
// attribute does not exist; the explicit empty()-check on the
// attribute object distinguishes "missing" from "present but empty".

bool hasAttr(pugi::xml_node const& n, char const* name) noexcept {
    return !n.attribute(name).empty();
}

std::optional<std::string> getOptionalString(pugi::xml_node const& n,
                                               char const* name) {
    if (!hasAttr(n, name)) return std::nullopt;
    return std::string{n.attribute(name).as_string()};
}

// Parse a required string attribute. The metablock parser treats a
// present-but-empty value as valid (e.g. `comment=""`).
Result<std::string> getRequiredString(pugi::xml_node const& n,
                                        char const* name,
                                        std::string const& context) {
    if (!hasAttr(n, name)) {
        std::ostringstream oss;
        oss << "OSF4 " << context << " is missing required attribute "
            << name;
        return tl::make_unexpected(invalidMetablock(oss.str()));
    }
    return std::string{n.attribute(name).as_string()};
}

Result<std::optional<double>> parseOptionalDouble(pugi::xml_node const& n,
                                                   char const* name) {
    if (!hasAttr(n, name)) return std::optional<double>{};
    pugi::xml_attribute const& attr = n.attribute(name);
    // as_double returns 0.0 on malformed input — we re-parse via
    // strtod to detect that case explicitly.
    char const* raw = attr.as_string();
    char* end = nullptr;
    double const v = std::strtod(raw, &end);
    if (end == raw || *end != '\0') {
        std::ostringstream oss;
        oss << "OSF4 attribute " << name << "=" << '"' << raw
            << "\" is not a valid floating-point number";
        return tl::make_unexpected(invalidMetablock(oss.str()));
    }
    return std::optional<double>{v};
}

Result<std::optional<std::int64_t>> parseOptionalI64(pugi::xml_node const& n,
                                                     char const* name) {
    if (!hasAttr(n, name)) return std::optional<std::int64_t>{};
    pugi::xml_attribute const& attr = n.attribute(name);
    char const* raw = attr.as_string();
    char* end = nullptr;
    long long const v = std::strtoll(raw, &end, 10);
    if (end == raw || *end != '\0') {
        std::ostringstream oss;
        oss << "OSF4 attribute " << name << "=" << '"' << raw
            << "\" is not a valid integer";
        return tl::make_unexpected(invalidMetablock(oss.str()));
    }
    return std::optional<std::int64_t>{static_cast<std::int64_t>(v)};
}

Result<std::uint8_t> validateSizeOfLengthValue(std::int64_t raw,
                                                  std::string const& channelName) {
    if (raw == 2 || raw == 4) return static_cast<std::uint8_t>(raw);
    std::ostringstream oss;
    oss << "OSF4 channel \"" << channelName
        << "\" sizeoflengthvalue must be 2 or 4, got " << raw;
    return tl::make_unexpected(invalidMetablock(oss.str()));
}

// Channel-level fields removed in spec revision 2026-05-04 — readers
// tolerate them but ignore the values; OSFGenerator-style files emit
// `scale="1"` and `offset="0"` on every channel. Match the JSON
// parser, which is silent until a logging facade lands.
char const* const REMOVED_CHANNEL_FIELDS[] = {
    "scale",
    "offset",
    "physicalunit1", "physicalunit2", "physicalunit3",
    "physicaldimension1", "physicaldimension2", "physicaldimension3",
};

Result<FileInfo> parseOptimeasAttrs(pugi::xml_node const& root) {
    FileInfo info;
    info.version = 4;

    info.createdUtc   = getOptionalString(root, "created_utc");
    info.creator       = getOptionalString(root, "creator");
    info.tag           = getOptionalString(root, "tag");
    info.reason        = getOptionalString(root, "reason");
    info.comment       = getOptionalString(root, "comment");
    info.namespaceSep = getOptionalString(root, "namespacesep");

    auto lat = parseOptionalDouble(root, "created_at_latitude");
    if (!lat) return tl::make_unexpected(std::move(lat).error());
    info.createdAtLatitude = *lat;

    auto lon = parseOptionalDouble(root, "created_at_longitude");
    if (!lon) return tl::make_unexpected(std::move(lon).error());
    info.createdAtLongitude = *lon;

    auto alt = parseOptionalDouble(root, "created_at_altitude");
    if (!alt) return tl::make_unexpected(std::move(alt).error());
    info.createdAtAltitude = *alt;

    // Short-form GPS spellings (`latitude=` without the `created_at_`
    // prefix) are emitted by OSFGenerator-era files and by some early
    // field devices. Accept them as an alternative on read; writers
    // always emit the spec form.
    if (!info.createdAtLatitude) {
        auto v = parseOptionalDouble(root, "latitude");
        if (!v) return tl::make_unexpected(std::move(v).error());
        info.createdAtLatitude = *v;
    }
    if (!info.createdAtLongitude) {
        auto v = parseOptionalDouble(root, "longitude");
        if (!v) return tl::make_unexpected(std::move(v).error());
        info.createdAtLongitude = *v;
    }
    if (!info.createdAtAltitude) {
        auto v = parseOptionalDouble(root, "altitude");
        if (!v) return tl::make_unexpected(std::move(v).error());
        info.createdAtAltitude = *v;
    }

    return info;
}

Result<Channel> parseChannel(pugi::xml_node const& node, std::size_t position) {
    Channel ch;

    // index — required, must fit u16
    if (!hasAttr(node, "index")) {
        std::ostringstream oss;
        oss << "OSF4 <channel> at position " << position
            << " is missing required attribute index";
        return tl::make_unexpected(invalidMetablock(oss.str()));
    }
    char const* rawIdx = node.attribute("index").as_string();
    char* idxEnd = nullptr;
    long long const idxV = std::strtoll(rawIdx, &idxEnd, 10);
    if (idxEnd == rawIdx || *idxEnd != '\0' || idxV < 0 ||
        idxV > static_cast<long long>(UINT16_MAX)) {
        std::ostringstream oss;
        oss << "OSF4 <channel> at position " << position << " has index="
            << '"' << rawIdx << "\" out of range 0.." << UINT16_MAX;
        return tl::make_unexpected(invalidMetablock(oss.str()));
    }
    ch.index = static_cast<std::uint16_t>(idxV);

    std::ostringstream chanCtx;
    chanCtx << "<channel index=" << ch.index << ">";
    std::string const ctx = chanCtx.str();

    auto name = getRequiredString(node, "name", ctx);
    if (!name) return tl::make_unexpected(std::move(name).error());
    ch.name = std::move(*name);

    auto ct = getRequiredString(node, "channeltype", ctx);
    if (!ct) return tl::make_unexpected(std::move(ct).error());
    ch.channelTypeRaw = *ct;
    auto ctR = parseChannelType(ch.channelTypeRaw);
    if (!ctR) return tl::make_unexpected(std::move(ctR).error());
    ch.channelType = *ctR;

    auto dt = getRequiredString(node, "datatype", ctx);
    if (!dt) return tl::make_unexpected(std::move(dt).error());
    ch.dataTypeRaw = *dt;
    auto dtR = parseDataType(ch.dataTypeRaw);
    if (!dtR) return tl::make_unexpected(std::move(dtR).error());
    ch.dataType = *dtR;

    auto sol = getRequiredString(node, "sizeoflengthvalue", ctx);
    if (!sol) return tl::make_unexpected(std::move(sol).error());
    {
        char const* rawSol = sol->c_str();
        char* solEnd = nullptr;
        long long const solV = std::strtoll(rawSol, &solEnd, 10);
        if (solEnd == rawSol || *solEnd != '\0') {
            std::ostringstream oss;
            oss << "OSF4 channel \"" << ch.name
                << "\" sizeoflengthvalue=\"" << rawSol
                << "\" is not numeric";
            return tl::make_unexpected(invalidMetablock(oss.str()));
        }
        auto solV8 = validateSizeOfLengthValue(
            static_cast<std::int64_t>(solV), ch.name);
        if (!solV8) return tl::make_unexpected(std::move(solV8).error());
        ch.sizeOfLengthValue = *solV8;
    }

    auto ti = parseOptionalI64(node, "timeincrement");
    if (!ti) return tl::make_unexpected(std::move(ti).error());
    ch.timeIncrementNs = *ti;

    ch.mimeType          = getOptionalString(node, "mimetype");
    ch.physicalUnit      = getOptionalString(node, "physicalunit");
    ch.physicalDimension = getOptionalString(node, "physicaldimension");
    ch.displayName       = getOptionalString(node, "displayname");
    ch.comment            = getOptionalString(node, "comment");
    ch.reference          = getOptionalString(node, "reference");

    if (hasAttr(node, "spectrumtype")) {
        ch.spectrumType = parseSpectrumType(
            node.attribute("spectrumtype").as_string());
    }

    // Deprecated channel-level fields are tolerated (match the Rust
    // reference and the JSON parser). Real-world OSF4 files declare
    // them on every channel; rejecting them would break the corpus.
    // Once a logging facade exists, surface as `warn!`.
    for (char const* removed : REMOVED_CHANNEL_FIELDS) {
        (void) removed;  // silence -Wunused-variable on builds without logs
    }

    return ch;
}

Result<std::vector<Channel>> parseChannels(pugi::xml_node const& channelsRoot) {
    std::vector<Channel> out;
    std::size_t position = 0;
    for (pugi::xml_node node = channelsRoot.first_child(); node;
         node = node.next_sibling()) {
        if (node.type() != pugi::node_element) continue;
        if (std::strcmp(node.name(), "channel") != 0) {
            // Unknown child of <channels> — tolerate silently.
            continue;
        }
        auto ch = parseChannel(node, position++);
        if (!ch) return tl::make_unexpected(std::move(ch).error());
        out.push_back(std::move(*ch));
    }
    return out;
}

Result<Info> parseInfo(pugi::xml_node const& node) {
    Info info;

    auto name = getRequiredString(node, "name", "<info>");
    if (!name) return tl::make_unexpected(std::move(name).error());
    info.name = std::move(*name);

    std::string dtRaw = "string";
    if (hasAttr(node, "datatype")) {
        dtRaw = node.attribute("datatype").as_string();
    }
    auto dtR = parseDataType(dtRaw);
    if (!dtR) return tl::make_unexpected(std::move(dtR).error());
    info.dataType = *dtR;

    if (hasAttr(node, "value")) {
        info.value = node.attribute("value").as_string();
    }

    info.physicalUnit = getOptionalString(node, "physicalunit");
    return info;
}

Result<std::vector<Info>> parseInfos(pugi::xml_node const& infosRoot) {
    std::vector<Info> out;
    for (pugi::xml_node node = infosRoot.first_child(); node;
         node = node.next_sibling()) {
        if (node.type() != pugi::node_element) continue;
        if (std::strcmp(node.name(), "info") != 0) continue;
        auto info = parseInfo(node);
        if (!info) return tl::make_unexpected(std::move(info).error());
        out.push_back(std::move(*info));
    }
    return out;
}

}  // anonymous namespace

Result<MetaBlock> parseMetablockXml(std::uint8_t const* data, std::size_t size) {
    if (data == nullptr && size != 0) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "parseMetablockXml: data is null but size > 0"});
    }

    pugi::xml_document doc;
    // parse_default + an explicit UTF-8 encoding hint keeps pugixml
    // out of its BOM-detection path. Real-world OSF4 files declare
    // encoding="UTF-8" but in practice carry CP1252-encoded bytes for
    // characters like `°` in `°C`; pugixml's UTF-8 decoder replaces
    // invalid sequences with the Unicode replacement character rather
    // than failing the parse. The reference files all parse this way.
    pugi::xml_parse_result const r =
        doc.load_buffer(data, size, pugi::parse_default, pugi::encoding_utf8);
    if (!r) {
        std::ostringstream oss;
        oss << "OSF4 XML parse error at offset " << r.offset
            << ": " << r.description();
        return tl::make_unexpected(xmlParseError(oss.str()));
    }

    pugi::xml_node root = doc.document_element();
    if (!root) {
        return tl::make_unexpected(invalidMetablock(
            "OSF4 metablock is empty (no root element)"));
    }
    if (std::strcmp(root.name(), "optimeas") != 0) {
        std::ostringstream oss;
        oss << "OSF4 root element must be <optimeas>, got <"
            << root.name() << ">";
        return tl::make_unexpected(invalidMetablock(oss.str()));
    }

    MetaBlock mb;

    auto fileInfo = parseOptimeasAttrs(root);
    if (!fileInfo) return tl::make_unexpected(std::move(fileInfo).error());
    mb.fileInfo = std::move(*fileInfo);

    pugi::xml_node const channelsRoot = root.child("channels");
    if (channelsRoot) {
        auto channels = parseChannels(channelsRoot);
        if (!channels) return tl::make_unexpected(std::move(channels).error());
        mb.channels = std::move(*channels);
    }

    pugi::xml_node const infosRoot = root.child("infos");
    if (infosRoot) {
        auto infos = parseInfos(infosRoot);
        if (!infos) return tl::make_unexpected(std::move(infos).error());
        mb.infos = std::move(*infos);
    }

    return mb;
}

Result<MetaBlock> parseMetablockXml(std::string_view text) {
    return parseMetablockXml(
        reinterpret_cast<std::uint8_t const*>(text.data()),
        text.size());
}

}  // namespace osf
