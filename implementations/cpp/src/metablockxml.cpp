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

Error invalid_metablock(std::string msg) {
    return Error{Error::Code::InvalidMetablock, std::move(msg)};
}

Error xml_parse_error(std::string msg) {
    return Error{Error::Code::XmlParseError, std::move(msg)};
}

// Wire-attribute reads. pugixml returns an empty string when an
// attribute does not exist; the explicit empty()-check on the
// attribute object distinguishes "missing" from "present but empty".

bool has_attr(pugi::xml_node const& n, char const* name) noexcept {
    return !n.attribute(name).empty();
}

std::optional<std::string> get_optional_string(pugi::xml_node const& n,
                                               char const* name) {
    if (!has_attr(n, name)) return std::nullopt;
    return std::string{n.attribute(name).as_string()};
}

// Parse a required string attribute. The metablock parser treats a
// present-but-empty value as valid (e.g. `comment=""`).
Result<std::string> get_required_string(pugi::xml_node const& n,
                                        char const* name,
                                        std::string const& context) {
    if (!has_attr(n, name)) {
        std::ostringstream oss;
        oss << "OSF4 " << context << " is missing required attribute "
            << name;
        return tl::make_unexpected(invalid_metablock(oss.str()));
    }
    return std::string{n.attribute(name).as_string()};
}

Result<std::optional<double>> parse_optional_double(pugi::xml_node const& n,
                                                   char const* name) {
    if (!has_attr(n, name)) return std::optional<double>{};
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
        return tl::make_unexpected(invalid_metablock(oss.str()));
    }
    return std::optional<double>{v};
}

Result<std::optional<std::int64_t>> parse_optional_i64(pugi::xml_node const& n,
                                                     char const* name) {
    if (!has_attr(n, name)) return std::optional<std::int64_t>{};
    pugi::xml_attribute const& attr = n.attribute(name);
    char const* raw = attr.as_string();
    char* end = nullptr;
    long long const v = std::strtoll(raw, &end, 10);
    if (end == raw || *end != '\0') {
        std::ostringstream oss;
        oss << "OSF4 attribute " << name << "=" << '"' << raw
            << "\" is not a valid integer";
        return tl::make_unexpected(invalid_metablock(oss.str()));
    }
    return std::optional<std::int64_t>{static_cast<std::int64_t>(v)};
}

Result<std::uint8_t> validate_size_of_length_value(std::int64_t raw,
                                                  std::string const& channelName) {
    if (raw == 2 || raw == 4) return static_cast<std::uint8_t>(raw);
    std::ostringstream oss;
    oss << "OSF4 channel \"" << channelName
        << "\" sizeoflengthvalue must be 2 or 4, got " << raw;
    return tl::make_unexpected(invalid_metablock(oss.str()));
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

Result<FileInfo> parse_optimeas_attrs(pugi::xml_node const& root) {
    FileInfo info;
    info.version = 4;

    info.createdUtc   = get_optional_string(root, "created_utc");
    info.creator       = get_optional_string(root, "creator");
    info.tag           = get_optional_string(root, "tag");
    info.reason        = get_optional_string(root, "reason");
    info.comment       = get_optional_string(root, "comment");
    info.namespaceSep = get_optional_string(root, "namespacesep");

    auto lat = parse_optional_double(root, "created_at_latitude");
    if (!lat) return tl::make_unexpected(std::move(lat).error());
    info.createdAtLatitude = *lat;

    auto lon = parse_optional_double(root, "created_at_longitude");
    if (!lon) return tl::make_unexpected(std::move(lon).error());
    info.createdAtLongitude = *lon;

    auto alt = parse_optional_double(root, "created_at_altitude");
    if (!alt) return tl::make_unexpected(std::move(alt).error());
    info.createdAtAltitude = *alt;

    // Short-form GPS spellings (`latitude=` without the `created_at_`
    // prefix) are emitted by OSFGenerator-era files and by some early
    // field devices. Accept them as an alternative on read; writers
    // always emit the spec form.
    if (!info.createdAtLatitude) {
        auto v = parse_optional_double(root, "latitude");
        if (!v) return tl::make_unexpected(std::move(v).error());
        info.createdAtLatitude = *v;
    }
    if (!info.createdAtLongitude) {
        auto v = parse_optional_double(root, "longitude");
        if (!v) return tl::make_unexpected(std::move(v).error());
        info.createdAtLongitude = *v;
    }
    if (!info.createdAtAltitude) {
        auto v = parse_optional_double(root, "altitude");
        if (!v) return tl::make_unexpected(std::move(v).error());
        info.createdAtAltitude = *v;
    }

    return info;
}

Result<Channel> parse_channel(pugi::xml_node const& node, std::size_t position) {
    Channel ch;

    // index — required, must fit u16
    if (!has_attr(node, "index")) {
        std::ostringstream oss;
        oss << "OSF4 <channel> at position " << position
            << " is missing required attribute index";
        return tl::make_unexpected(invalid_metablock(oss.str()));
    }
    char const* raw_idx = node.attribute("index").as_string();
    char* idx_end = nullptr;
    long long const idx_v = std::strtoll(raw_idx, &idx_end, 10);
    if (idx_end == raw_idx || *idx_end != '\0' || idx_v < 0 ||
        idx_v > static_cast<long long>(UINT16_MAX)) {
        std::ostringstream oss;
        oss << "OSF4 <channel> at position " << position << " has index="
            << '"' << raw_idx << "\" out of range 0.." << UINT16_MAX;
        return tl::make_unexpected(invalid_metablock(oss.str()));
    }
    ch.index = static_cast<std::uint16_t>(idx_v);

    std::ostringstream chan_ctx;
    chan_ctx << "<channel index=" << ch.index << ">";
    std::string const ctx = chan_ctx.str();

    auto name = get_required_string(node, "name", ctx);
    if (!name) return tl::make_unexpected(std::move(name).error());
    ch.name = std::move(*name);

    auto ct = get_required_string(node, "channeltype", ctx);
    if (!ct) return tl::make_unexpected(std::move(ct).error());
    ch.channelTypeRaw = *ct;
    auto ct_r = parseChannelType(ch.channelTypeRaw);
    if (!ct_r) return tl::make_unexpected(std::move(ct_r).error());
    ch.channelType = *ct_r;

    auto dt = get_required_string(node, "datatype", ctx);
    if (!dt) return tl::make_unexpected(std::move(dt).error());
    ch.dataTypeRaw = *dt;
    auto dt_r = parseDataType(ch.dataTypeRaw);
    if (!dt_r) return tl::make_unexpected(std::move(dt_r).error());
    ch.dataType = *dt_r;

    auto sol = get_required_string(node, "sizeoflengthvalue", ctx);
    if (!sol) return tl::make_unexpected(std::move(sol).error());
    {
        char const* raw_sol = sol->c_str();
        char* sol_end = nullptr;
        long long const sol_v = std::strtoll(raw_sol, &sol_end, 10);
        if (sol_end == raw_sol || *sol_end != '\0') {
            std::ostringstream oss;
            oss << "OSF4 channel \"" << ch.name
                << "\" sizeoflengthvalue=\"" << raw_sol
                << "\" is not numeric";
            return tl::make_unexpected(invalid_metablock(oss.str()));
        }
        auto sol_v8 = validate_size_of_length_value(
            static_cast<std::int64_t>(sol_v), ch.name);
        if (!sol_v8) return tl::make_unexpected(std::move(sol_v8).error());
        ch.sizeOfLengthValue = *sol_v8;
    }

    auto ti = parse_optional_i64(node, "timeincrement");
    if (!ti) return tl::make_unexpected(std::move(ti).error());
    ch.timeIncrementNs = *ti;

    ch.mimeType          = get_optional_string(node, "mimetype");
    ch.physicalUnit      = get_optional_string(node, "physicalunit");
    ch.physicalDimension = get_optional_string(node, "physicaldimension");
    ch.displayName       = get_optional_string(node, "displayname");
    ch.comment            = get_optional_string(node, "comment");
    ch.reference          = get_optional_string(node, "reference");

    if (has_attr(node, "spectrumtype")) {
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

Result<std::vector<Channel>> parse_channels(pugi::xml_node const& channels_root) {
    std::vector<Channel> out;
    std::size_t position = 0;
    for (pugi::xml_node node = channels_root.first_child(); node;
         node = node.next_sibling()) {
        if (node.type() != pugi::node_element) continue;
        if (std::strcmp(node.name(), "channel") != 0) {
            // Unknown child of <channels> — tolerate silently.
            continue;
        }
        auto ch = parse_channel(node, position++);
        if (!ch) return tl::make_unexpected(std::move(ch).error());
        out.push_back(std::move(*ch));
    }
    return out;
}

Result<Info> parse_info(pugi::xml_node const& node) {
    Info info;

    auto name = get_required_string(node, "name", "<info>");
    if (!name) return tl::make_unexpected(std::move(name).error());
    info.name = std::move(*name);

    std::string dt_raw = "string";
    if (has_attr(node, "datatype")) {
        dt_raw = node.attribute("datatype").as_string();
    }
    auto dt_r = parseDataType(dt_raw);
    if (!dt_r) return tl::make_unexpected(std::move(dt_r).error());
    info.dataType = *dt_r;

    if (has_attr(node, "value")) {
        info.value = node.attribute("value").as_string();
    }

    info.physicalUnit = get_optional_string(node, "physicalunit");
    return info;
}

Result<std::vector<Info>> parse_infos(pugi::xml_node const& infos_root) {
    std::vector<Info> out;
    for (pugi::xml_node node = infos_root.first_child(); node;
         node = node.next_sibling()) {
        if (node.type() != pugi::node_element) continue;
        if (std::strcmp(node.name(), "info") != 0) continue;
        auto info = parse_info(node);
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
        return tl::make_unexpected(xml_parse_error(oss.str()));
    }

    pugi::xml_node root = doc.document_element();
    if (!root) {
        return tl::make_unexpected(invalid_metablock(
            "OSF4 metablock is empty (no root element)"));
    }
    if (std::strcmp(root.name(), "optimeas") != 0) {
        std::ostringstream oss;
        oss << "OSF4 root element must be <optimeas>, got <"
            << root.name() << ">";
        return tl::make_unexpected(invalid_metablock(oss.str()));
    }

    MetaBlock mb;

    auto fileInfo = parse_optimeas_attrs(root);
    if (!fileInfo) return tl::make_unexpected(std::move(fileInfo).error());
    mb.fileInfo = std::move(*fileInfo);

    pugi::xml_node const channels_root = root.child("channels");
    if (channels_root) {
        auto channels = parse_channels(channels_root);
        if (!channels) return tl::make_unexpected(std::move(channels).error());
        mb.channels = std::move(*channels);
    }

    pugi::xml_node const infos_root = root.child("infos");
    if (infos_root) {
        auto infos = parse_infos(infos_root);
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
