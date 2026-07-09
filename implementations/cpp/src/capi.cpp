// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Ensure the header exports (rather than imports) the symbols even if the
// build system forgets to define this; CMake also sets it PRIVATE.
#ifndef OSF_C_BUILDING
#define OSF_C_BUILDING
#endif

#include "osf/capi.h"

#include "osf/blockwriter.h"
#include "osf/datachannel.h"
#include "osf/error.h"
#include "osf/manager.h"
#include "osf/types.h"
#include "osf/version.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

// The opaque manager handle owns a DataManager. An osf_channel handle is a
// borrowed `osf::DataChannel const*` into the manager's channels().
struct osf_manager {
    osf::DataManager mgr;
};

namespace {

// Thread-local last-error message. Initialised non-null so c_str() is
// always valid.
thread_local std::string gLastError;

void setError(std::string msg) { gLastError = std::move(msg); }

osf_status statusFromCode(osf::Error::Code code) noexcept {
    using C = osf::Error::Code;
    switch (code) {
        case C::Unknown:                 return OSF_ERR_UNKNOWN;
        case C::InvalidArgument:         return OSF_ERR_INVALID_ARGUMENT;
        case C::IoError:                 return OSF_ERR_IO;
        case C::ParseError:              return OSF_ERR_PARSE;
        case C::NotFound:                return OSF_ERR_NOT_FOUND;
        case C::InvalidMagicHeader:      return OSF_ERR_INVALID_MAGIC_HEADER;
        case C::UnsupportedVersion:      return OSF_ERR_UNSUPPORTED_VERSION;
        case C::MagicHeaderTooLong:      return OSF_ERR_MAGIC_HEADER_TOO_LONG;
        case C::InvalidMetablock:        return OSF_ERR_INVALID_METABLOCK;
        case C::RemovedInSpec:           return OSF_ERR_REMOVED_IN_SPEC;
        case C::JsonParseError:          return OSF_ERR_JSON_PARSE;
        case C::XmlParseError:           return OSF_ERR_XML_PARSE;
        case C::UnknownChannelIndex:     return OSF_ERR_UNKNOWN_CHANNEL_INDEX;
        case C::InvalidBlock:            return OSF_ERR_INVALID_BLOCK;
        case C::ChannelMixedBlockTypes:  return OSF_ERR_CHANNEL_MIXED_BLOCK_TYPES;
        case C::ContinuedDataWithoutStart: return OSF_ERR_CONTINUED_WITHOUT_START;
        case C::RelStampWithoutAnchor:   return OSF_ERR_RELSTAMP_WITHOUT_ANCHOR;
        case C::DataTypeMismatch:        return OSF_ERR_DATA_TYPE_MISMATCH;
        case C::UnknownHeaderToken:      return OSF_ERR_UNKNOWN_HEADER_TOKEN;
        case C::MetablockCrcMismatch:    return OSF_ERR_METABLOCK_CRC_MISMATCH;
    }
    return OSF_ERR_UNKNOWN;
}

osf_integrity_profile integrityToC(osf::IntegrityProfile p) noexcept {
    using P = osf::IntegrityProfile;
    switch (p) {
        case P::None:    return OSF_INTEGRITY_NONE;
        case P::Crc32c:  return OSF_INTEGRITY_CRC32C;
        case P::Ed25519: return OSF_INTEGRITY_ED25519;
    }
    return OSF_INTEGRITY_NONE;
}

osf_data_type dataTypeToC(osf::DataType dt) noexcept {
    using D = osf::DataType;
    switch (dt) {
        case D::Bool:        return OSF_DT_BOOL;
        case D::Int8:        return OSF_DT_INT8;
        case D::Int16:       return OSF_DT_INT16;
        case D::Int32:       return OSF_DT_INT32;
        case D::Int64:       return OSF_DT_INT64;
        case D::UInt8:       return OSF_DT_UINT8;
        case D::UInt16:      return OSF_DT_UINT16;
        case D::UInt32:      return OSF_DT_UINT32;
        case D::UInt64:      return OSF_DT_UINT64;
        case D::Float:       return OSF_DT_FLOAT;
        case D::Double:      return OSF_DT_DOUBLE;
        case D::GpsLocation: return OSF_DT_GPS_LOCATION;
        case D::String:      return OSF_DT_STRING;
        case D::Binary:      return OSF_DT_BINARY;
        case D::ByteArray:   return OSF_DT_BINARY;  // read-side alias
        case D::Unsupported: return OSF_DT_UNSUPPORTED;
    }
    return OSF_DT_UNSUPPORTED;
}

osf_compression_format compressionToC(osf::CompressionFormat f) noexcept {
    switch (f) {
        case osf::CompressionFormat::None: return OSF_COMPRESSION_NONE;
        case osf::CompressionFormat::Zlib: return OSF_COMPRESSION_ZLIB;
        case osf::CompressionFormat::Gzip: return OSF_COMPRESSION_GZIP;
    }
    return OSF_COMPRESSION_NONE;
}

osf::DataChannel const* asDc(osf_channel const* c) noexcept {
    return reinterpret_cast<osf::DataChannel const*>(c);
}

bool isNumericScalar(osf::DataType dt) noexcept {
    using D = osf::DataType;
    switch (dt) {
        case D::Bool: case D::Int8: case D::Int16: case D::Int32:
        case D::Int64: case D::UInt8: case D::UInt16: case D::UInt32:
        case D::UInt64: case D::Float: case D::Double:
            return true;
        default:
            return false;
    }
}

// Materialise the numeric (timestamp, value) samples for Equidistant /
// Timestamped channels; empty for Variable channels.
std::vector<osf::Sample<osf::NumericValueRef>>
numericSamples(osf::DataChannel const& dc) {
    if (auto const* e = std::get_if<osf::EquidistantChannel>(&dc)) {
        return e->samplesVector();
    }
    if (auto const* t = std::get_if<osf::TimestampedChannel>(&dc)) {
        return t->samplesVector();
    }
    return {};
}

}  // namespace

// ── version / last error ─────────────────────────────────────────────

const char* osf_version(void) {
    static const std::string v{std::string(osf::version())};
    return v.c_str();
}

const char* osf_last_error_message(void) {
    return gLastError.c_str();
}

// ── manager ──────────────────────────────────────────────────────────

osf_status osf_load_file(const char* path, osf_manager** out) {
    try {
        if (path == nullptr || out == nullptr) {
            setError("osf_load_file: null argument");
            return OSF_ERR_INVALID_ARGUMENT;
        }
        *out = nullptr;
        auto r = osf::DataManager::loadFromFile(path);
        if (!r) {
            setError(r.error().message);
            return statusFromCode(r.error().code);
        }
        *out = new osf_manager{std::move(*r)};
        return OSF_OK;
    } catch (std::exception const& e) {
        setError(e.what());
        return OSF_ERR_UNKNOWN;
    } catch (...) {
        setError("osf_load_file: unknown error");
        return OSF_ERR_UNKNOWN;
    }
}

void osf_manager_free(osf_manager* m) { delete m; }

size_t osf_manager_channel_count(const osf_manager* m) {
    return (m == nullptr) ? 0u : m->mgr.channels().size();
}

const osf_channel* osf_manager_channel_at(const osf_manager* m, size_t index) {
    if (m == nullptr || index >= m->mgr.channels().size()) {
        return nullptr;
    }
    return reinterpret_cast<const osf_channel*>(&m->mgr.channels()[index]);
}

const osf_channel* osf_manager_channel_by_name(const osf_manager* m,
                                               const char* name) {
    if (m == nullptr || name == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<const osf_channel*>(m->mgr.channel(name));
}

int osf_manager_is_compressed(const osf_manager* m) {
    return (m != nullptr && m->mgr.stats.compressed) ? 1 : 0;
}

osf_compression_format osf_manager_compression_format(const osf_manager* m) {
    if (m == nullptr) {
        return OSF_COMPRESSION_NONE;
    }
    return compressionToC(m->mgr.stats.compressionFormat);
}

const char* osf_manager_creator(const osf_manager* m) {
    if (m == nullptr || !m->mgr.meta.fileInfo.creator) {
        return "";
    }
    return m->mgr.meta.fileInfo.creator->c_str();
}

const char* osf_manager_created_utc(const osf_manager* m) {
    if (m == nullptr || !m->mgr.meta.fileInfo.createdUtc) {
        return "";
    }
    return m->mgr.meta.fileInfo.createdUtc->c_str();
}

osf_integrity_profile osf_manager_integrity(const osf_manager* m) {
    if (m == nullptr) {
        return OSF_INTEGRITY_NONE;
    }
    return integrityToC(m->mgr.stats.integrity);
}

uint64_t osf_manager_blocks_crc_failed(const osf_manager* m) {
    return (m == nullptr) ? 0u : m->mgr.stats.blocksCrcFailed;
}

uint64_t osf_manager_blocks_signature_skipped(const osf_manager* m) {
    return (m == nullptr) ? 0u : m->mgr.stats.blocksSignatureSkipped;
}

const char* osf_manager_verification_status(const osf_manager* m) {
    if (m == nullptr) {
        return "none";
    }
    // verificationStatus() returns a view over a static NUL-terminated
    // string literal, so .data() is a valid C string with static lifetime.
    return m->mgr.stats.verificationStatus().data();
}

// ── channel ──────────────────────────────────────────────────────────

const char* osf_channel_name(const osf_channel* c) {
    auto const* dc = asDc(c);
    return (dc == nullptr) ? "" : osf::channelName(*dc).c_str();
}

uint16_t osf_channel_index(const osf_channel* c) {
    auto const* dc = asDc(c);
    return (dc == nullptr) ? 0u : osf::channelIndex(*dc);
}

osf_data_type osf_channel_data_type(const osf_channel* c) {
    auto const* dc = asDc(c);
    return (dc == nullptr) ? OSF_DT_UNSUPPORTED
                           : dataTypeToC(osf::channelDataType(*dc));
}

const char* osf_channel_physical_unit(const osf_channel* c) {
    auto const* dc = asDc(c);
    if (dc == nullptr) {
        return "";
    }
    // Read the unit straight from the channel's own `physicalUnit` member
    // (stable for the manager's lifetime) rather than the common accessor,
    // which returns a temporary optional<string> by value — the borrowed
    // pointer must stay valid until osf_manager_free.
    return std::visit(
        [](auto const& ch) -> const char* {
            return ch.physicalUnit ? ch.physicalUnit->c_str() : "";
        },
        *dc);
}

size_t osf_channel_sample_count(const osf_channel* c) {
    auto const* dc = asDc(c);
    return (dc == nullptr) ? 0u : osf::channelSampleCount(*dc);
}

size_t osf_channel_read_timestamps(const osf_channel* c, int64_t* out,
                                   size_t cap) {
    auto const* dc = asDc(c);
    if (dc == nullptr || out == nullptr) {
        return 0u;
    }
    if (auto const* var = std::get_if<osf::VariableChannel>(dc)) {
        auto const samples = var->samplesVector();
        size_t const n = std::min(samples.size(), cap);
        for (size_t i = 0; i < n; ++i) {
            out[i] = samples[i].timestampNs;
        }
        return n;
    }
    auto const samples = numericSamples(*dc);
    size_t const n = std::min(samples.size(), cap);
    for (size_t i = 0; i < n; ++i) {
        out[i] = samples[i].timestampNs;
    }
    return n;
}

size_t osf_channel_read_f64(const osf_channel* c, double* out, size_t cap) {
    auto const* dc = asDc(c);
    if (dc == nullptr || out == nullptr ||
        !isNumericScalar(osf::channelDataType(*dc))) {
        return 0u;
    }
    auto const samples = numericSamples(*dc);
    size_t const n = std::min(samples.size(), cap);
    for (size_t i = 0; i < n; ++i) {
        out[i] = std::visit(
            [](auto const& v) -> double {
                using U = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<U, osf::GpsLocation>) {
                    return 0.0;  // guarded out above; keeps the visit total
                } else {
                    return static_cast<double>(v);
                }
            },
            samples[i].value);
    }
    return n;
}

size_t osf_channel_read_i64(const osf_channel* c, int64_t* out, size_t cap) {
    auto const* dc = asDc(c);
    if (dc == nullptr || out == nullptr ||
        !isNumericScalar(osf::channelDataType(*dc))) {
        return 0u;
    }
    auto const samples = numericSamples(*dc);
    size_t const n = std::min(samples.size(), cap);
    for (size_t i = 0; i < n; ++i) {
        out[i] = std::visit(
            [](auto const& v) -> int64_t {
                using U = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<U, osf::GpsLocation>) {
                    return 0;  // guarded out above
                } else {
                    return static_cast<int64_t>(v);
                }
            },
            samples[i].value);
    }
    return n;
}

size_t osf_channel_read_gps(const osf_channel* c, double* out_lla,
                            size_t cap_samples) {
    auto const* dc = asDc(c);
    if (dc == nullptr || out_lla == nullptr ||
        osf::channelDataType(*dc) != osf::DataType::GpsLocation) {
        return 0u;
    }
    auto const samples = numericSamples(*dc);
    size_t const n = std::min(samples.size(), cap_samples);
    for (size_t i = 0; i < n; ++i) {
        auto const* g = std::get_if<osf::GpsLocation>(&samples[i].value);
        if (g == nullptr) {
            out_lla[i * 3 + 0] = 0.0;
            out_lla[i * 3 + 1] = 0.0;
            out_lla[i * 3 + 2] = 0.0;
        } else {
            out_lla[i * 3 + 0] = g->latitude;
            out_lla[i * 3 + 1] = g->longitude;
            out_lla[i * 3 + 2] = g->altitude;
        }
    }
    return n;
}

const char* osf_channel_string_at(const osf_channel* c, size_t i) {
    auto const* dc = asDc(c);
    if (dc == nullptr) {
        return nullptr;
    }
    auto const* var = std::get_if<osf::VariableChannel>(dc);
    if (var == nullptr) {
        setError("osf_channel_string_at: channel is not a string channel");
        return nullptr;
    }
    auto r = var->asStrings();
    if (!r) {
        setError(r.error().message);
        return nullptr;
    }
    auto const* vec = *r;
    if (i >= vec->size()) {
        return nullptr;
    }
    return (*vec)[i].c_str();
}

const uint8_t* osf_channel_binary_at(const osf_channel* c, size_t i,
                                     size_t* out_len) {
    auto const* dc = asDc(c);
    if (dc == nullptr || out_len == nullptr) {
        return nullptr;
    }
    *out_len = 0;
    auto const* var = std::get_if<osf::VariableChannel>(dc);
    if (var == nullptr) {
        setError("osf_channel_binary_at: channel is not a binary channel");
        return nullptr;
    }
    auto r = var->asBinaries();
    if (!r) {
        setError(r.error().message);
        return nullptr;
    }
    auto const* vec = *r;
    if (i >= vec->size()) {
        return nullptr;
    }
    auto const& bytes = (*vec)[i];
    *out_len = bytes.size();
    return bytes.data();
}

// ── write (round-trip; always OSF5) ──────────────────────────────────

osf_status osf_write_to_file(const osf_manager* m, const char* path) {
    try {
        if (m == nullptr || path == nullptr) {
            setError("osf_write_to_file: null argument");
            return OSF_ERR_INVALID_ARGUMENT;
        }
        auto r = osf::writeToFile(m->mgr, path);
        if (!r) {
            setError(r.error().message);
            return statusFromCode(r.error().code);
        }
        return OSF_OK;
    } catch (std::exception const& e) {
        setError(e.what());
        return OSF_ERR_UNKNOWN;
    } catch (...) {
        setError("osf_write_to_file: unknown error");
        return OSF_ERR_UNKNOWN;
    }
}
