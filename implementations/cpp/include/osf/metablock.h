// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// \file metablock.h
/// Common OSF metablock data model and OSF5 JSON parser.
///
/// OSF4 and OSF5 differ only in *how* the metablock is serialised — XML
/// vs. JSON — not in *what* it carries. This header defines the shared
/// data structures that `parse_metablock_json` and `parse_metablock_xml`
/// populate. Block readers, writers,
/// and downstream tooling see `MetaBlock` only; the format split stops
/// here.
///
/// See the OSF5 reference in the OSF format specification for the JSON
/// wire form; spec revision 2026-05-04 pins the supported field set.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <osf/error.h>
#include <osf/types.h>

namespace osf {

/// File-level metadata.
struct FileInfo {
    /// On-disk format version: 4 or 5. Populated by the metablock
    /// parser, not derived from the magic header alone.
    std::uint32_t version = 0;
    /// ISO-8601 timestamp of file creation. Kept as `std::string` for
    /// now; proper datetime parsing lands when we adopt a datetime
    /// dependency.
    std::optional<std::string> created_utc;
    /// Free-form name of the writing device or application.
    std::optional<std::string> creator;
    /// Latitude of the recording location in decimal degrees. The OSF4
    /// short spelling `latitude=` (without the `created_at_` prefix) is
    /// accepted on read for OSFGenerator-style files; writers always
    /// emit the spec form `created_at_latitude`.
    std::optional<double> created_at_latitude;
    /// Longitude of the recording location in decimal degrees.
    std::optional<double> created_at_longitude;
    /// Altitude of the recording location in meters.
    std::optional<double> created_at_altitude;
    /// Free-form text describing why the recording was made.
    std::optional<std::string> reason;
    /// Separator used between path components in channel names.
    /// Default is `"."`; preserved verbatim if explicit on disk.
    std::optional<std::string> namespace_sep;
    /// Free-form tag set by the writer (`"default"` if not overridden).
    std::optional<std::string> tag;
    /// Free-form comment.
    std::optional<std::string> comment;
};

/// Definition of a single channel as recorded in the metablock.
struct Channel {
    /// Stable index used by the binary block stream to refer to this
    /// channel.
    std::uint16_t index = 0;
    /// Fully qualified channel name (e.g. `"Sensor/Temperature"`).
    std::string name;
    /// Optional reference identifier (free-form).
    std::optional<std::string> reference;
    /// Channel type (scalar / equidistant / timestamped / unsupported).
    ChannelType channel_type = ChannelType::Scalar;
    /// On-disk spelling of `channeltype`. For known kinds this equals
    /// the canonical wire form (`"scalar"`, `"equidistant"`,
    /// `"timestamped"`); for `Unsupported` it carries the original
    /// spelling so callers can produce useful diagnostics.
    std::string channel_type_raw;
    /// Channel datatype (one of the spec rev 2026-05-04 datatypes or
    /// `Unsupported`).
    DataType data_type = DataType::Unsupported;
    /// On-disk spelling of `datatype`. For known kinds this equals the
    /// canonical wire form (`"double"`, `"binary"`, …) or the alias
    /// actually present on disk (e.g. `"bytearray"` for the
    /// `Binary`-aliased read path); for `Unsupported` it carries the
    /// original unknown spelling.
    std::string data_type_raw;
    /// Sample period in nanoseconds. `std::nullopt` or `0` means the
    /// channel is timestamped (no fixed sample rate); a positive value
    /// indicates an equidistant channel with that period.
    std::optional<std::int64_t> time_increment_ns;
    /// Width of the per-value length prefix in bytes — must be 2 or 4.
    /// The metablock parser rejects any other value as
    /// `Error::Code::InvalidMetablock` because a wrong length-prefix
    /// size would silently corrupt every block read for this channel.
    std::uint8_t size_of_length_value = 0;
    /// MIME type for `binary` channels.
    std::optional<std::string> mime_type;
    /// Spectrum subtype, if the channel carries spectral data.
    std::optional<SpectrumType> spectrum_type;
    /// Physical unit (e.g. `"°C"`, `"bar"`).
    std::optional<std::string> physical_unit;
    /// Physical dimension (e.g. `"temperature"`).
    std::optional<std::string> physical_dimension;
    /// Display name for UIs.
    std::optional<std::string> display_name;
    /// Free-form comment.
    std::optional<std::string> comment;
};

/// Optional metablock entry supplied by the writer (e.g. machine
/// configuration, recording parameters). Spec revision 2026-05-04
/// keeps values as opaque strings; a typed variant may follow later.
struct Info {
    /// Logical name of the entry.
    std::string name;
    /// Stringified value. The on-disk type is recorded in `data_type`.
    std::string value;
    /// Datatype the writer declared for the value. Defaults to
    /// `DataType::String` when the metablock omits `datatype`.
    DataType data_type = DataType::String;
    /// Physical unit, if applicable.
    std::optional<std::string> physical_unit;
};

/// Parsed contents of an OSF metablock — version-independent.
///
/// Population is symmetric between OSF4 and OSF5: every field that
/// one parser fills, the other parser fills from the equivalent
/// on-disk representation.
struct MetaBlock {
    /// File-level metadata (creator, timestamp, geolocation, …).
    FileInfo file_info;
    /// Channel definitions, in the order they appear on disk.
    std::vector<Channel> channels;
    /// Optional free-form key/value pairs supplied by the writer.
    std::vector<Info> infos;
};

/// Parse an OSF5 metablock body into a `MetaBlock`.
///
/// `data` must point to exactly the metablock — no magic-header line
/// and no following block-stream bytes. The caller is responsible for
/// determining the metablock length from the magic header and handing
/// only that slice to this function.
///
/// Possible errors (`Result::error().code`):
/// - `Error::Code::JsonParseError` — body is not valid JSON.
/// - `Error::Code::InvalidMetablock` — a required structural field is
///   missing or malformed (no `osf` envelope, `channels` is not an
///   array, `sizeoflengthvalue` not 2 or 4, …).
/// - `Error::Code::RemovedInSpec` — a channel references a datatype
///   removed in spec revision 2026-05-04.
[[nodiscard]] Result<MetaBlock> parse_metablock_json(std::uint8_t const* data,
                                                    std::size_t size);

/// String-view convenience overload. Equivalent to the pointer/size
/// form; bytes are interpreted as the JSON text.
[[nodiscard]] Result<MetaBlock> parse_metablock_json(std::string_view text);

/// Parse an OSF4 metablock body into a `MetaBlock`.
///
/// The OSF4 wire form is a single `<optimeas>` element carrying
/// file-level attributes plus `<channels>` and optionally `<infos>`
/// children. `data` must point to exactly the metablock — no
/// magic-header line and no following block-stream bytes.
///
/// Population of the returned `MetaBlock` is symmetric with
/// `parse_metablock_json`: every field one parser sets, the other
/// parser fills from the equivalent on-disk representation.
///
/// Real-world OSF4 field files declare `encoding="UTF-8"` but in
/// practice carry CP1252-encoded bytes for non-ASCII characters such
/// as `°` in `°C`. To stay usable on those files the parser configures
/// pugixml with `parse_default | parse_ws_pcdata_single` and a UTF-8
/// encoding hint; invalid byte sequences become Unicode replacement
/// characters rather than parse errors.
///
/// Possible errors (`Result::error().code`):
/// - `Error::Code::XmlParseError` — body is not well-formed XML.
/// - `Error::Code::InvalidMetablock` — the root element is not
///   `<optimeas>`, a required attribute is missing, or
///   `sizeoflengthvalue` is neither 2 nor 4.
/// - `Error::Code::RemovedInSpec` — a channel references a datatype
///   removed in spec revision 2026-05-04.
[[nodiscard]] Result<MetaBlock> parse_metablock_xml(std::uint8_t const* data,
                                                   std::size_t size);

/// String-view convenience overload. Equivalent to the pointer/size
/// form; bytes are interpreted as the XML text.
[[nodiscard]] Result<MetaBlock> parse_metablock_xml(std::string_view text);

/// Serialise a `MetaBlock` into the OSF5 JSON wire form.
///
/// The output is the canonical OSF5 envelope:
/// `{"osf": {"format": "osf5", "version": 5, "file": {...},
/// "channels": [...], "infos": [...]}}`.
/// Field naming and types match what `parse_metablock_json` consumes,
/// so a round-trip (serialise → parse) preserves every populated field
/// up to optional-field presence and JSON pretty-printing whitespace.
///
/// The writer is OSF5-only, so this helper always emits
/// `"osf5"` / `5` regardless of `meta.file_info.version`. Optional
/// fields (`creator`, `created_utc`, the `created_at_*` triple,
/// `reason`, `namespace_sep`, `tag`, `comment`, per-channel
/// `mime_type`, `physical_unit`, …) are **omitted when unset** rather
/// than written as JSON `null`.
///
/// Pretty-printed with two-space indentation so the metablock remains
/// reasonably readable in a hex viewer.
///
/// Returns the JSON text. Serialization never fails for a
/// well-formed `MetaBlock` — the function is total over its input
/// domain.
[[nodiscard]] std::string serialize_metablock_json(MetaBlock const& meta);

}  // namespace osf
