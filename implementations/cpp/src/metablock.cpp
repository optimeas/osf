// SPDX-License-Identifier: Apache-2.0

#include <osf/metablock.hpp>

#include <climits>
#include <sstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace osf {

namespace {

using nlohmann::json;

Error invalid_metablock(std::string msg) {
    return Error{Error::Code::InvalidMetablock, std::move(msg)};
}

// nlohmann::json carries an explicit value_t::discarded sentinel that
// the non-throwing parse() returns on failure. Wrapping it once keeps
// the call sites focused on field-level concerns.
bool is_discarded(json const& v) noexcept {
    return v.type() == json::value_t::discarded;
}

// Returns the value of `key` from `obj` as an std::string when present
// and convertible. JSON null is treated as absent.
//   - string  → the string itself
//   - null    → nullopt
//   - other   → JSON-dump representation (numbers as decimal text, bools
//               as "true" / "false"). Matches the Rust impl, which uses
//               value.to_string() for non-string fallthroughs.
std::optional<std::string> get_optional_string(json const& obj, char const* key) {
    auto it = obj.find(key);
    if (it == obj.end()) return std::nullopt;
    if (it->is_null()) return std::nullopt;
    if (it->is_string()) return it->template get<std::string>();
    return it->dump();
}

std::optional<double> get_optional_double(json const& obj, char const* key) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number()) return std::nullopt;
    return it->template get<double>();
}

Result<FileInfo> parse_file_info(json const& osf_obj) {
    FileInfo info;

    auto file_it = osf_obj.find("file");
    if (file_it == osf_obj.end()) {
        return info;  // file block is optional
    }
    if (!file_it->is_object()) {
        return tl::make_unexpected(invalid_metablock(
            "OSF5 \"file\" must be an object"));
    }
    json const& fobj = *file_it;

    info.created_utc          = get_optional_string(fobj, "created_utc");
    info.creator              = get_optional_string(fobj, "creator");
    info.reason               = get_optional_string(fobj, "reason");
    info.tag                  = get_optional_string(fobj, "tag");
    info.comment              = get_optional_string(fobj, "comment");
    info.namespace_sep        = get_optional_string(fobj, "namespacesep");
    info.created_at_latitude  = get_optional_double(fobj, "created_at_latitude");
    info.created_at_longitude = get_optional_double(fobj, "created_at_longitude");
    info.created_at_altitude  = get_optional_double(fobj, "created_at_altitude");

    // Some early OSF5 emitters used the short spelling (`latitude=`
    // without the `created_at_` prefix). Accept and normalise on read;
    // writers always emit the spec form.
    if (!info.created_at_latitude) {
        info.created_at_latitude = get_optional_double(fobj, "latitude");
    }
    if (!info.created_at_longitude) {
        info.created_at_longitude = get_optional_double(fobj, "longitude");
    }
    if (!info.created_at_altitude) {
        info.created_at_altitude = get_optional_double(fobj, "altitude");
    }

    return info;
}

Result<std::uint8_t> validate_size_of_length_value(std::uint64_t raw,
                                                  std::string const& channel_name) {
    if (raw == 2 || raw == 4) return static_cast<std::uint8_t>(raw);
    std::ostringstream oss;
    oss << "channel \"" << channel_name
        << "\" sizeoflengthvalue must be 2 or 4, got " << raw;
    return tl::make_unexpected(invalid_metablock(oss.str()));
}

Result<Channel> parse_channel(json const& obj, std::size_t position) {
    Channel ch;

    // index — required, must fit u16
    auto idx_it = obj.find("index");
    if (idx_it == obj.end() || !idx_it->is_number_integer()) {
        std::ostringstream oss;
        oss << "channel at position " << position
            << " is missing required field \"index\"";
        return tl::make_unexpected(invalid_metablock(oss.str()));
    }
    auto raw_idx = idx_it->template get<std::int64_t>();
    if (raw_idx < 0 || raw_idx > static_cast<std::int64_t>(UINT16_MAX)) {
        std::ostringstream oss;
        oss << "channel at position " << position << " has index=" << raw_idx
            << " out of range 0.." << UINT16_MAX;
        return tl::make_unexpected(invalid_metablock(oss.str()));
    }
    ch.index = static_cast<std::uint16_t>(raw_idx);

    // name — required
    auto name_it = obj.find("name");
    if (name_it == obj.end() || !name_it->is_string()) {
        std::ostringstream oss;
        oss << "channel at index " << ch.index
            << " is missing required field \"name\"";
        return tl::make_unexpected(invalid_metablock(oss.str()));
    }
    ch.name = name_it->template get<std::string>();

    // channeltype — required
    auto ct_it = obj.find("channeltype");
    if (ct_it == obj.end() || !ct_it->is_string()) {
        return tl::make_unexpected(invalid_metablock(
            "channel \"" + ch.name + "\" is missing required field \"channeltype\""));
    }
    ch.channel_type_raw = ct_it->template get<std::string>();
    auto ct_r = parse_channel_type(ch.channel_type_raw);
    if (!ct_r) return tl::make_unexpected(std::move(ct_r).error());
    ch.channel_type = *ct_r;

    // datatype — required; RemovedInSpec propagates as-is
    auto dt_it = obj.find("datatype");
    if (dt_it == obj.end() || !dt_it->is_string()) {
        return tl::make_unexpected(invalid_metablock(
            "channel \"" + ch.name + "\" is missing required field \"datatype\""));
    }
    ch.data_type_raw = dt_it->template get<std::string>();
    auto dt_r = parse_data_type(ch.data_type_raw);
    if (!dt_r) return tl::make_unexpected(std::move(dt_r).error());
    ch.data_type = *dt_r;

    // sizeoflengthvalue — required, must be 2 or 4
    auto sol_it = obj.find("sizeoflengthvalue");
    if (sol_it == obj.end() || !sol_it->is_number_integer()) {
        return tl::make_unexpected(invalid_metablock(
            "channel \"" + ch.name + "\" is missing required field \"sizeoflengthvalue\""));
    }
    auto raw_sol = sol_it->template get<std::int64_t>();
    if (raw_sol < 0) {
        std::ostringstream oss;
        oss << "channel \"" << ch.name
            << "\" sizeoflengthvalue must be 2 or 4, got " << raw_sol;
        return tl::make_unexpected(invalid_metablock(oss.str()));
    }
    auto sol_r = validate_size_of_length_value(
        static_cast<std::uint64_t>(raw_sol), ch.name);
    if (!sol_r) return tl::make_unexpected(std::move(sol_r).error());
    ch.size_of_length_value = *sol_r;

    // Optional fields
    auto ti_it = obj.find("timeincrement");
    if (ti_it != obj.end() && ti_it->is_number_integer()) {
        ch.time_increment_ns = ti_it->template get<std::int64_t>();
    }

    ch.mime_type          = get_optional_string(obj, "mimetype");
    ch.physical_unit      = get_optional_string(obj, "physicalunit");
    ch.physical_dimension = get_optional_string(obj, "physicaldimension");
    ch.display_name       = get_optional_string(obj, "displayname");
    ch.comment            = get_optional_string(obj, "comment");
    ch.reference          = get_optional_string(obj, "reference");

    auto st_it = obj.find("spectrumtype");
    if (st_it != obj.end() && st_it->is_string()) {
        ch.spectrum_type = parse_spectrum_type(
            st_it->template get<std::string>());
    }

    // Channel-level fields removed in spec revision 2026-05-04
    // (`scale`, `offset`, `physicalunit{1,2,3}`,
    // `physicaldimension{1,2,3}`) are tolerated but ignored. Real-world
    // field files (e.g. examples/steam_loco.osf) still carry them; the
    // Rust reference logs them with `log::warn!`. The C++ build has no
    // logging facade yet, so this is currently silent.

    return ch;
}

Result<std::vector<Channel>> parse_channels(json const& osf_obj) {
    auto it = osf_obj.find("channels");
    if (it == osf_obj.end()) return std::vector<Channel>{};
    if (!it->is_array()) {
        return tl::make_unexpected(invalid_metablock(
            "OSF5 \"channels\" must be an array"));
    }
    std::vector<Channel> out;
    out.reserve(it->size());
    for (std::size_t i = 0; i < it->size(); ++i) {
        json const& entry = it->at(i);
        if (!entry.is_object()) {
            std::ostringstream oss;
            oss << "OSF5 channels[" << i << "] must be an object";
            return tl::make_unexpected(invalid_metablock(oss.str()));
        }
        auto ch = parse_channel(entry, i);
        if (!ch) return tl::make_unexpected(std::move(ch).error());
        out.push_back(std::move(*ch));
    }
    return out;
}

Result<std::vector<Info>> parse_infos(json const& osf_obj) {
    auto it = osf_obj.find("infos");
    if (it == osf_obj.end()) return std::vector<Info>{};
    if (!it->is_array()) {
        return tl::make_unexpected(invalid_metablock(
            "OSF5 \"infos\" must be an array"));
    }
    std::vector<Info> out;
    out.reserve(it->size());
    for (std::size_t i = 0; i < it->size(); ++i) {
        json const& entry = it->at(i);
        if (!entry.is_object()) {
            std::ostringstream oss;
            oss << "OSF5 infos[" << i << "] must be an object";
            return tl::make_unexpected(invalid_metablock(oss.str()));
        }
        Info info;

        auto name_it = entry.find("name");
        if (name_it == entry.end() || !name_it->is_string()) {
            std::ostringstream oss;
            oss << "OSF5 infos[" << i << "] is missing \"name\"";
            return tl::make_unexpected(invalid_metablock(oss.str()));
        }
        info.name = name_it->template get<std::string>();

        std::string dt_raw = "string";
        auto dt_it = entry.find("datatype");
        if (dt_it != entry.end() && dt_it->is_string()) {
            dt_raw = dt_it->template get<std::string>();
        }
        auto dt_r = parse_data_type(dt_raw);
        if (!dt_r) return tl::make_unexpected(std::move(dt_r).error());
        info.data_type = *dt_r;

        auto v_it = entry.find("value");
        if (v_it != entry.end()) {
            if (v_it->is_string()) {
                info.value = v_it->template get<std::string>();
            } else if (!v_it->is_null()) {
                info.value = v_it->dump();
            }
        }

        info.physical_unit = get_optional_string(entry, "physicalunit");
        out.push_back(std::move(info));
    }
    return out;
}

}  // anonymous namespace

Result<MetaBlock> parse_metablock_json(std::uint8_t const* data, std::size_t size) {
    if (data == nullptr && size != 0) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "parse_metablock_json: data is null but size > 0"});
    }

    // allow_exceptions=false: parse returns a discarded value on error
    // rather than throwing. Diagnostic detail is lost; this keeps the
    // core API exception-free (DECISIONS §20).
    auto root = json::parse(data, data + size,
                            /*cb=*/nullptr,
                            /*allow_exceptions=*/false);
    if (is_discarded(root)) {
        return tl::make_unexpected(Error{
            Error::Code::JsonParseError,
            "OSF5 metablock JSON parse error (malformed JSON)"});
    }

    if (!root.is_object()) {
        return tl::make_unexpected(invalid_metablock(
            "OSF5 root must be a JSON object"));
    }

    auto osf_it = root.find("osf");
    if (osf_it == root.end() || !osf_it->is_object()) {
        return tl::make_unexpected(invalid_metablock(
            "OSF5 root is missing the \"osf\" envelope"));
    }

    // Note: the on-disk `format` field is informational. The magic
    // header is the source of truth for which parser to dispatch; we
    // do not validate "format":"osf5" here. The Rust reference logs
    // a warning when the field disagrees; the C++ build skips
    // silently until a logging facade lands.

    MetaBlock mb;
    auto fi = parse_file_info(*osf_it);
    if (!fi) return tl::make_unexpected(std::move(fi).error());
    mb.file_info = std::move(*fi);
    mb.file_info.version = 5;

    auto ch = parse_channels(*osf_it);
    if (!ch) return tl::make_unexpected(std::move(ch).error());
    mb.channels = std::move(*ch);

    auto in = parse_infos(*osf_it);
    if (!in) return tl::make_unexpected(std::move(in).error());
    mb.infos = std::move(*in);

    return mb;
}

Result<MetaBlock> parse_metablock_json(std::string_view text) {
    return parse_metablock_json(
        reinterpret_cast<std::uint8_t const*>(text.data()),
        text.size());
}

}  // namespace osf
