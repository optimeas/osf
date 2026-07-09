// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <osf/metablock.h>

#include <climits>
#include <sstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace osf {

namespace {

using nlohmann::json;

Error invalidMetablock(std::string msg) {
    return Error{Error::Code::InvalidMetablock, std::move(msg)};
}

// nlohmann::json carries an explicit value_t::discarded sentinel that
// the non-throwing parse() returns on failure. Wrapping it once keeps
// the call sites focused on field-level concerns.
bool isDiscarded(json const& v) noexcept {
    return v.type() == json::value_t::discarded;
}

// Returns the value of `key` from `obj` as an std::string when present
// and convertible. JSON null is treated as absent.
//   - string  → the string itself
//   - null    → nullopt
//   - other   → JSON-dump representation (numbers as decimal text, bools
//               as "true" / "false"). Matches the Rust impl, which uses
//               value.to_string() for non-string fallthroughs.
std::optional<std::string> getOptionalString(json const& obj, char const* key) {
    auto it = obj.find(key);
    if (it == obj.end()) return std::nullopt;
    if (it->is_null()) return std::nullopt;
    if (it->is_string()) return it->template get<std::string>();
    return it->dump();
}

std::optional<double> getOptionalDouble(json const& obj, char const* key) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number()) return std::nullopt;
    return it->template get<double>();
}

Result<FileInfo> parseFileInfo(json const& osfObj) {
    FileInfo info;

    auto fileIt = osfObj.find("file");
    if (fileIt == osfObj.end()) {
        return info;  // file block is optional
    }
    if (!fileIt->is_object()) {
        return tl::make_unexpected(invalidMetablock(
            "OSF5 \"file\" must be an object"));
    }
    json const& fobj = *fileIt;

    info.createdUtc          = getOptionalString(fobj, "created_utc");
    info.creator              = getOptionalString(fobj, "creator");
    info.reason               = getOptionalString(fobj, "reason");
    info.tag                  = getOptionalString(fobj, "tag");
    info.comment              = getOptionalString(fobj, "comment");
    info.namespaceSep        = getOptionalString(fobj, "namespacesep");
    info.createdAtLatitude  = getOptionalDouble(fobj, "created_at_latitude");
    info.createdAtLongitude = getOptionalDouble(fobj, "created_at_longitude");
    info.createdAtAltitude  = getOptionalDouble(fobj, "created_at_altitude");

    // Some early OSF5 emitters used the short spelling (`latitude=`
    // without the `created_at_` prefix). Accept and normalise on read;
    // writers always emit the spec form.
    if (!info.createdAtLatitude) {
        info.createdAtLatitude = getOptionalDouble(fobj, "latitude");
    }
    if (!info.createdAtLongitude) {
        info.createdAtLongitude = getOptionalDouble(fobj, "longitude");
    }
    if (!info.createdAtAltitude) {
        info.createdAtAltitude = getOptionalDouble(fobj, "altitude");
    }

    return info;
}

Result<std::uint8_t> validateSizeOfLengthValue(std::uint64_t raw,
                                                  std::string const& channelName) {
    if (raw == 2 || raw == 4) return static_cast<std::uint8_t>(raw);
    std::ostringstream oss;
    oss << "channel \"" << channelName
        << "\" sizeoflengthvalue must be 2 or 4, got " << raw;
    return tl::make_unexpected(invalidMetablock(oss.str()));
}

Result<Channel> parseChannel(json const& obj, std::size_t position) {
    Channel ch;

    // index — required, must fit u16
    auto idxIt = obj.find("index");
    if (idxIt == obj.end() || !idxIt->is_number_integer()) {
        std::ostringstream oss;
        oss << "channel at position " << position
            << " is missing required field \"index\"";
        return tl::make_unexpected(invalidMetablock(oss.str()));
    }
    auto rawIdx = idxIt->template get<std::int64_t>();
    if (rawIdx < 0 || rawIdx > static_cast<std::int64_t>(UINT16_MAX)) {
        std::ostringstream oss;
        oss << "channel at position " << position << " has index=" << rawIdx
            << " out of range 0.." << UINT16_MAX;
        return tl::make_unexpected(invalidMetablock(oss.str()));
    }
    ch.index = static_cast<std::uint16_t>(rawIdx);

    // name — required
    auto nameIt = obj.find("name");
    if (nameIt == obj.end() || !nameIt->is_string()) {
        std::ostringstream oss;
        oss << "channel at index " << ch.index
            << " is missing required field \"name\"";
        return tl::make_unexpected(invalidMetablock(oss.str()));
    }
    ch.name = nameIt->template get<std::string>();

    // channeltype — required
    auto ctIt = obj.find("channeltype");
    if (ctIt == obj.end() || !ctIt->is_string()) {
        return tl::make_unexpected(invalidMetablock(
            "channel \"" + ch.name + "\" is missing required field \"channeltype\""));
    }
    ch.channelTypeRaw = ctIt->template get<std::string>();
    auto ctR = parseChannelType(ch.channelTypeRaw);
    if (!ctR) return tl::make_unexpected(std::move(ctR).error());
    ch.channelType = *ctR;

    // datatype — required; RemovedInSpec propagates as-is
    auto dtIt = obj.find("datatype");
    if (dtIt == obj.end() || !dtIt->is_string()) {
        return tl::make_unexpected(invalidMetablock(
            "channel \"" + ch.name + "\" is missing required field \"datatype\""));
    }
    ch.dataTypeRaw = dtIt->template get<std::string>();
    auto dtR = parseDataType(ch.dataTypeRaw);
    if (!dtR) return tl::make_unexpected(std::move(dtR).error());
    ch.dataType = *dtR;

    // sizeoflengthvalue — required, must be 2 or 4
    auto solIt = obj.find("sizeoflengthvalue");
    if (solIt == obj.end() || !solIt->is_number_integer()) {
        return tl::make_unexpected(invalidMetablock(
            "channel \"" + ch.name + "\" is missing required field \"sizeoflengthvalue\""));
    }
    auto rawSol = solIt->template get<std::int64_t>();
    if (rawSol < 0) {
        std::ostringstream oss;
        oss << "channel \"" << ch.name
            << "\" sizeoflengthvalue must be 2 or 4, got " << rawSol;
        return tl::make_unexpected(invalidMetablock(oss.str()));
    }
    auto solR = validateSizeOfLengthValue(
        static_cast<std::uint64_t>(rawSol), ch.name);
    if (!solR) return tl::make_unexpected(std::move(solR).error());
    ch.sizeOfLengthValue = *solR;

    // Optional fields
    auto tiIt = obj.find("timeincrement");
    if (tiIt != obj.end() && tiIt->is_number_integer()) {
        ch.timeIncrementNs = tiIt->template get<std::int64_t>();
    }

    ch.mimeType          = getOptionalString(obj, "mimetype");
    ch.physicalUnit      = getOptionalString(obj, "physicalunit");
    ch.physicalDimension = getOptionalString(obj, "physicaldimension");
    ch.displayName       = getOptionalString(obj, "displayname");
    ch.comment            = getOptionalString(obj, "comment");
    ch.reference          = getOptionalString(obj, "reference");

    auto stIt = obj.find("spectrumtype");
    if (stIt != obj.end() && stIt->is_string()) {
        ch.spectrumType = parseSpectrumType(
            stIt->template get<std::string>());
    }

    // Channel-level fields removed in spec revision 2026-05-04
    // (`scale`, `offset`, `physicalunit{1,2,3}`,
    // `physicaldimension{1,2,3}`) are tolerated but ignored. Real-world
    // field files (e.g. examples/steam_loco.osf) still carry them; the
    // Rust reference logs them with `log::warn!`. The C++ build has no
    // logging facade yet, so this is currently silent.

    return ch;
}

Result<std::vector<Channel>> parseChannels(json const& osfObj) {
    auto it = osfObj.find("channels");
    if (it == osfObj.end()) return std::vector<Channel>{};
    if (!it->is_array()) {
        return tl::make_unexpected(invalidMetablock(
            "OSF5 \"channels\" must be an array"));
    }
    std::vector<Channel> out;
    out.reserve(it->size());
    for (std::size_t i = 0; i < it->size(); ++i) {
        json const& entry = it->at(i);
        if (!entry.is_object()) {
            std::ostringstream oss;
            oss << "OSF5 channels[" << i << "] must be an object";
            return tl::make_unexpected(invalidMetablock(oss.str()));
        }
        auto ch = parseChannel(entry, i);
        if (!ch) return tl::make_unexpected(std::move(ch).error());
        out.push_back(std::move(*ch));
    }
    return out;
}

Result<std::vector<Info>> parseInfos(json const& osfObj) {
    auto it = osfObj.find("infos");
    if (it == osfObj.end()) return std::vector<Info>{};
    if (!it->is_array()) {
        return tl::make_unexpected(invalidMetablock(
            "OSF5 \"infos\" must be an array"));
    }
    std::vector<Info> out;
    out.reserve(it->size());
    for (std::size_t i = 0; i < it->size(); ++i) {
        json const& entry = it->at(i);
        if (!entry.is_object()) {
            std::ostringstream oss;
            oss << "OSF5 infos[" << i << "] must be an object";
            return tl::make_unexpected(invalidMetablock(oss.str()));
        }
        Info info;

        auto nameIt = entry.find("name");
        if (nameIt == entry.end() || !nameIt->is_string()) {
            std::ostringstream oss;
            oss << "OSF5 infos[" << i << "] is missing \"name\"";
            return tl::make_unexpected(invalidMetablock(oss.str()));
        }
        info.name = nameIt->template get<std::string>();

        std::string dtRaw = "string";
        auto dtIt = entry.find("datatype");
        if (dtIt != entry.end() && dtIt->is_string()) {
            dtRaw = dtIt->template get<std::string>();
        }
        auto dtR = parseDataType(dtRaw);
        if (!dtR) return tl::make_unexpected(std::move(dtR).error());
        info.dataType = *dtR;

        auto vIt = entry.find("value");
        if (vIt != entry.end()) {
            if (vIt->is_string()) {
                info.value = vIt->template get<std::string>();
            } else if (!vIt->is_null()) {
                info.value = vIt->dump();
            }
        }

        info.physicalUnit = getOptionalString(entry, "physicalunit");
        out.push_back(std::move(info));
    }
    return out;
}

}  // anonymous namespace

Result<MetaBlock> parseMetablockJson(std::uint8_t const* data, std::size_t size) {
    if (data == nullptr && size != 0) {
        return tl::make_unexpected(Error{
            Error::Code::InvalidArgument,
            "parseMetablockJson: data is null but size > 0"});
    }

    // allow_exceptions=false: parse returns a discarded value on error
    // rather than throwing. Diagnostic detail is lost; this keeps the
    // core API exception-free (DECISIONS §20).
    auto root = json::parse(data, data + size,
                            /*cb=*/nullptr,
                            /*allow_exceptions=*/false);
    if (isDiscarded(root)) {
        return tl::make_unexpected(Error{
            Error::Code::JsonParseError,
            "OSF5 metablock JSON parse error (malformed JSON)"});
    }

    if (!root.is_object()) {
        return tl::make_unexpected(invalidMetablock(
            "OSF5 root must be a JSON object"));
    }

    auto osfIt = root.find("osf");
    if (osfIt == root.end() || !osfIt->is_object()) {
        return tl::make_unexpected(invalidMetablock(
            "OSF5 root is missing the \"osf\" envelope"));
    }

    // Note: the on-disk `format` field is informational. The magic
    // header is the source of truth for which parser to dispatch; we
    // do not validate "format":"osf5" here. The Rust reference logs
    // a warning when the field disagrees; the C++ build skips
    // silently until a logging facade lands.

    MetaBlock mb;
    auto fi = parseFileInfo(*osfIt);
    if (!fi) return tl::make_unexpected(std::move(fi).error());
    mb.fileInfo = std::move(*fi);
    mb.fileInfo.version = 5;

    auto ch = parseChannels(*osfIt);
    if (!ch) return tl::make_unexpected(std::move(ch).error());
    mb.channels = std::move(*ch);

    auto in = parseInfos(*osfIt);
    if (!in) return tl::make_unexpected(std::move(in).error());
    mb.infos = std::move(*in);

    return mb;
}

Result<MetaBlock> parseMetablockJson(std::string_view text) {
    return parseMetablockJson(
        reinterpret_cast<std::uint8_t const*>(text.data()),
        text.size());
}

namespace {

using nlohmann::json;

// Map a known DataType enum to its canonical wire spelling. For
// Unsupported we fall back to the channel's raw spelling (preserved
// from parse time for round-trip).
std::string dataTypeToWire(DataType dt, std::string_view rawFallback) {
    switch (dt) {
        case DataType::Bool:        return "bool";
        case DataType::Int8:        return "int8";
        case DataType::Int16:       return "int16";
        case DataType::Int32:       return "int32";
        case DataType::Int64:       return "int64";
        case DataType::UInt8:       return "uint8";
        case DataType::UInt16:      return "uint16";
        case DataType::UInt32:      return "uint32";
        case DataType::UInt64:      return "uint64";
        case DataType::Float:       return "float";
        case DataType::Double:      return "double";
        case DataType::String:      return "string";
        case DataType::Binary:      return "binary";
        case DataType::ByteArray:   return "binary";   // canonical write form
        case DataType::GpsLocation: return "gpslocation";
        case DataType::Unsupported:
            return std::string{rawFallback.empty() ? "double" : rawFallback};
    }
    return std::string{rawFallback.empty() ? "double" : rawFallback};
}

std::string channelTypeToWire(ChannelType ct,
                                 std::string_view rawFallback) {
    switch (ct) {
        case ChannelType::Scalar: return "scalar";
        case ChannelType::Vector: return "vector";
        case ChannelType::Matrix: return "matrix";
        case ChannelType::Binary: return "binary";
        case ChannelType::Unsupported:
            return std::string{rawFallback.empty() ? "scalar"
                                                    : rawFallback};
    }
    return std::string{rawFallback.empty() ? "scalar" : rawFallback};
}

std::string spectrumTypeToWire(SpectrumType st) {
    switch (st) {
        case SpectrumType::Amplitude:   return "amplitude";
        case SpectrumType::RealImag:    return "realimag";
        case SpectrumType::AmpPhaseRad: return "ampphaserad";
        case SpectrumType::AmpPhaseDeg: return "ampphasedeg";
    }
    return "amplitude";
}

json fileInfoToJson(FileInfo const& fi) {
    json obj = json::object();
    if (fi.createdUtc)            obj["created_utc"]          = *fi.createdUtc;
    if (fi.creator)                obj["creator"]              = *fi.creator;
    if (fi.tag)                    obj["tag"]                  = *fi.tag;
    if (fi.reason)                 obj["reason"]               = *fi.reason;
    if (fi.namespaceSep)          obj["namespacesep"]         = *fi.namespaceSep;
    if (fi.comment)                obj["comment"]              = *fi.comment;
    if (fi.createdAtLatitude)    obj["created_at_latitude"]  = *fi.createdAtLatitude;
    if (fi.createdAtLongitude)   obj["created_at_longitude"] = *fi.createdAtLongitude;
    if (fi.createdAtAltitude)    obj["created_at_altitude"]  = *fi.createdAtAltitude;
    return obj;
}

json channelToJson(Channel const& ch) {
    json obj = json::object();
    obj["index"]             = ch.index;
    obj["name"]              = ch.name;
    obj["channeltype"]       = channelTypeToWire(ch.channelType,
                                                    ch.channelTypeRaw);
    obj["datatype"]          = dataTypeToWire(ch.dataType,
                                                 ch.dataTypeRaw);
    obj["sizeoflengthvalue"] = ch.sizeOfLengthValue;

    if (ch.timeIncrementNs)   obj["timeincrement"]      = *ch.timeIncrementNs;
    if (ch.physicalUnit)       obj["physicalunit"]       = *ch.physicalUnit;
    if (ch.physicalDimension)  obj["physicaldimension"]  = *ch.physicalDimension;
    if (ch.displayName)        obj["displayname"]        = *ch.displayName;
    if (ch.mimeType)           obj["mimetype"]           = *ch.mimeType;
    if (ch.reference)           obj["reference"]          = *ch.reference;
    if (ch.comment)             obj["comment"]            = *ch.comment;
    if (ch.spectrumType)       obj["spectrumtype"]       =
        spectrumTypeToWire(*ch.spectrumType);
    return obj;
}

json infoToJson(Info const& info) {
    json obj = json::object();
    obj["name"]     = info.name;
    obj["value"]    = info.value;
    obj["datatype"] = dataTypeToWire(info.dataType, /*rawFallback=*/"");
    if (info.physicalUnit) obj["physicalunit"] = *info.physicalUnit;
    return obj;
}

}  // namespace

std::string serializeMetablockJson(MetaBlock const& meta) {
    json channelsArr = json::array();
    for (auto const& ch : meta.channels) {
        channelsArr.push_back(channelToJson(ch));
    }

    json infosArr = json::array();
    for (auto const& info : meta.infos) {
        infosArr.push_back(infoToJson(info));
    }

    json envelope = json::object();
    json inner    = json::object();
    inner["format"]   = "osf5";
    inner["version"]  = 5;
    inner["file"]     = fileInfoToJson(meta.fileInfo);
    inner["channels"] = std::move(channelsArr);
    if (!meta.infos.empty()) {
        inner["infos"] = std::move(infosArr);
    }
    envelope["osf"] = std::move(inner);

    return envelope.dump(2);
}

}  // namespace osf
