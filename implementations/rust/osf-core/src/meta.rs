// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! Common OSF metablock data model.
//!
//! OSF4 and OSF5 differ only in *how* the metablock is serialised — XML
//! vs. JSON — not in *what* it carries. This module defines the shared
//! data structures that both [`parse_metablock_json`] and
//! [`parse_metablock_xml`] populate. Block readers, writers, and the
//! Python bindings see [`MetaBlock`] only; the format split stops here.
//!
//! The validation helpers ([`parse_data_type`], [`parse_channel_type`])
//! also live here so the JSON and XML parsers can apply identical rules
//! when they encounter a datatype string or channel-type string. That is
//! what guarantees that an OSF4 file and the equivalent OSF5 file end up
//! in the same shape.
//!
//! [`parse_metablock_json`]: crate::meta_json::parse_metablock_json
//! [`parse_metablock_xml`]: crate::meta_xml::parse_metablock_xml

use crate::error::OsfError;
use crate::types::{ChannelType, DataType};
use log::{debug, warn};

/// Parsed contents of an OSF metablock — version-independent.
///
/// Population is symmetric between OSF4 and OSF5: every field that one
/// parser fills, the other parser fills from the equivalent on-disk
/// representation.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct MetaBlock {
    /// File-level metadata (creator, timestamp, geolocation, …).
    pub file_info: FileInfo,
    /// Channel definitions, in the order they appear on disk.
    pub channels: Vec<Channel>,
    /// Optional free-form key/value pairs supplied by the writer.
    pub infos: Vec<Info>,
}

/// File-level metadata. Mirrors DECISIONS.md §13.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct FileInfo {
    /// On-disk format version: 4 or 5.
    pub version: u32,
    /// ISO-8601 timestamp of file creation. Kept as `String` for now;
    /// proper datetime parsing lands when we adopt a datetime crate.
    pub created_utc: Option<String>,
    /// Free-form name of the writing device or application.
    pub creator: Option<String>,
    /// Latitude of the recording location in decimal degrees. The OSF4
    /// short spelling `latitude=` (without the `created_at_` prefix) is
    /// accepted on read for OSFGenerator-style files; writers always
    /// emit the spec form `created_at_latitude`.
    pub created_at_latitude: Option<f64>,
    /// Longitude of the recording location in decimal degrees.
    pub created_at_longitude: Option<f64>,
    /// Altitude of the recording location in meters.
    pub created_at_altitude: Option<f64>,
    /// Free-form text describing why the recording was made.
    pub reason: Option<String>,
    /// Separator used between path components in channel names.
    /// Default is `"."`; preserved verbatim if explicit on disk.
    pub namespace_sep: Option<String>,
    /// Free-form tag set by the writer (`"default"` if not overridden).
    pub tag: Option<String>,
    /// Free-form comment.
    pub comment: Option<String>,
}

/// Definition of a single channel as recorded in the metablock.
#[derive(Debug, Clone, PartialEq)]
pub struct Channel {
    /// Stable index used by the binary block stream to refer to this
    /// channel.
    pub index: u16,
    /// Fully qualified channel name (e.g. `"Sensor/Temperature"`).
    pub name: String,
    /// Optional reference identifier (free-form).
    pub reference: Option<String>,
    /// Channel type (scalar / equidistant / timestamped / unsupported).
    pub channel_type: ChannelType,
    /// Channel datatype (one of the spec rev 2026-05-04 datatypes or
    /// `Unsupported`).
    pub data_type: DataType,
    /// Sample period in nanoseconds. `Some(0)` and `None` both mean the
    /// channel is timestamped (no fixed sample rate); `Some(n)` with
    /// `n > 0` indicates an equidistant channel with period `n` ns.
    pub time_increment_ns: Option<i64>,
    /// Width of the per-value length prefix in bytes — must be 2 or 4.
    /// The metablock parser rejects any other value as an
    /// [`OsfError::InvalidMetablock`] because a wrong length-prefix size
    /// would silently corrupt every block read for this channel.
    pub size_of_length_value: u8,
    /// MIME type for `binary` channels.
    pub mime_type: Option<String>,
    /// Spectrum subtype, if the channel carries spectral data.
    pub spectrum_type: Option<SpectrumType>,
    /// Physical unit (e.g. `"°C"`, `"bar"`).
    pub physical_unit: Option<String>,
    /// Physical dimension (e.g. `"temperature"`).
    pub physical_dimension: Option<String>,
    /// Display name for UIs.
    pub display_name: Option<String>,
    /// Free-form comment.
    pub comment: Option<String>,
}

/// Optional metablock entry supplied by the writer (e.g. machine
/// configuration, recording parameters). Spec revision 2026-05-04 keeps
/// values as opaque strings; a typed variant may follow later.
#[derive(Debug, Clone, PartialEq)]
pub struct Info {
    /// Logical name of the entry.
    pub name: String,
    /// Stringified value. The on-disk type is recorded in `data_type`.
    pub value: String,
    /// Datatype the writer declared for the value.
    pub data_type: DataType,
    /// Physical unit, if applicable.
    pub physical_unit: Option<String>,
}

/// Subtype for spectrum channels.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SpectrumType {
    /// Magnitude only (default).
    Amplitude,
    /// Real and imaginary parts.
    RealImag,
    /// Magnitude and phase in radians.
    AmpPhaseRad,
    /// Magnitude and phase in degrees.
    AmpPhaseDeg,
}

impl SpectrumType {
    /// Parse a wire-format spectrum-type string.
    ///
    /// Unknown spellings emit a `log::warn!` and resolve to
    /// [`SpectrumType::Amplitude`] — the spec defaults missing or
    /// unrecognised spectrum-type metadata to amplitude-only.
    #[must_use]
    pub fn from_wire(raw: &str) -> Self {
        match raw {
            "amplitude" => Self::Amplitude,
            "realimag" => Self::RealImag,
            "ampphaserad" => Self::AmpPhaseRad,
            "ampphasedeg" => Self::AmpPhaseDeg,
            other => {
                warn!("unknown spectrum type {other:?}; defaulting to amplitude");
                Self::Amplitude
            }
        }
    }
}

/// Names of the channel-level fields that were removed in spec revision
/// 2026-05-04. Encountering them is **not** a hard error (real-world
/// field files such as `examples/steam_loco.osf` still carry them on
/// every channel) — the parser logs them with `log::warn!` and skips.
pub(crate) const REMOVED_CHANNEL_FIELDS: &[&str] = &[
    "scale",
    "offset",
    "physicalunit1",
    "physicalunit2",
    "physicalunit3",
    "physicaldimension1",
    "physicaldimension2",
    "physicaldimension3",
];

/// Resolve a wire-format datatype string to a [`DataType`] variant.
///
/// Behaviour:
/// - Known current spelling → corresponding variant.
/// - `bytearray` → [`DataType::Binary`] (read-side alias) plus a
///   `log::debug!` that the alias was used.
/// - Removed datatype (`pair`, `triple`, `candata`, `gpsdata`) → hard
///   error [`OsfError::RemovedInSpec2026_05_04`]. Without a correct
///   datatype the binary blocks for this channel cannot be decoded, so
///   the file as a whole is rejected.
/// - Anything else → [`DataType::Unsupported`] plus a `log::warn!`. The
///   metablock as a whole still parses; block reads against this channel
///   will fail explicitly when attempted.
///
/// # Errors
///
/// Returns [`OsfError::RemovedInSpec2026_05_04`] when `raw` is one of the
/// four datatype strings removed in spec revision 2026-05-04.
pub fn parse_data_type(raw: &str) -> Result<DataType, OsfError> {
    match raw {
        "bool" => Ok(DataType::Bool),
        "int8" => Ok(DataType::Int8),
        "int16" => Ok(DataType::Int16),
        "int32" => Ok(DataType::Int32),
        "int64" => Ok(DataType::Int64),
        "uint8" => Ok(DataType::UInt8),
        "uint16" => Ok(DataType::UInt16),
        "uint32" => Ok(DataType::UInt32),
        "uint64" => Ok(DataType::UInt64),
        "float" => Ok(DataType::Float),
        "double" => Ok(DataType::Double),
        "string" => Ok(DataType::String),
        "binary" => Ok(DataType::Binary),
        "gpslocation" => Ok(DataType::GpsLocation),

        "bytearray" => {
            debug!("datatype \"bytearray\": read-side alias used, normalising to \"binary\"");
            Ok(DataType::Binary)
        }

        "gpsdata" => Err(OsfError::RemovedInSpec2026_05_04 {
            field: "datatype",
            value: raw.to_string(),
            replacement: Some("gpslocation"),
        }),
        "pair" | "triple" => Err(OsfError::RemovedInSpec2026_05_04 {
            field: "datatype",
            value: raw.to_string(),
            replacement: Some("two or three separate double channels"),
        }),
        "candata" => Err(OsfError::RemovedInSpec2026_05_04 {
            field: "datatype",
            value: raw.to_string(),
            replacement: Some("binary with an application-specific MIME type"),
        }),

        other => {
            warn!(
                "datatype {other:?} is not known to this build; \
                 channel kept as Unsupported, block reads will fail"
            );
            Ok(DataType::Unsupported(other.to_string()))
        }
    }
}

/// Resolve a wire-format channel-type string to a [`ChannelType`]
/// variant.
///
/// Unknown spellings produce [`ChannelType::Unsupported`] plus a
/// `log::warn!`. There are no removed channel-type strings in spec
/// revision 2026-05-04, so this function is currently infallible — the
/// `Result` return type is kept for symmetry with [`parse_data_type`]
/// and to preserve compatibility once a future revision retires a value.
///
/// # Errors
///
/// Currently never returns an error. The signature reserves the option
/// for future spec revisions.
pub fn parse_channel_type(raw: &str) -> Result<ChannelType, OsfError> {
    match raw {
        "scalar" => Ok(ChannelType::Scalar),
        "equidistant" => Ok(ChannelType::Equidistant),
        "timestamped" => Ok(ChannelType::Timestamped),
        other => {
            warn!(
                "channeltype {other:?} is not known to this build; \
                 channel kept as Unsupported, block reads will fail"
            );
            Ok(ChannelType::Unsupported(other.to_string()))
        }
    }
}

/// Validate the `sizeoflengthvalue` value found on disk. Spec accepts 2
/// and 4. Anything else is rejected as [`OsfError::InvalidMetablock`]
/// because a wrong length-prefix size silently corrupts every subsequent
/// block read for the affected channel — that is not best-effort
/// territory.
///
/// # Errors
///
/// Returns [`OsfError::InvalidMetablock`] for any value other than 2 or 4.
pub(crate) fn validate_size_of_length_value(raw: u32) -> Result<u8, OsfError> {
    match raw {
        2 | 4 => Ok(raw as u8),
        other => Err(OsfError::InvalidMetablock(format!(
            "sizeoflengthvalue must be 2 or 4, got {other}"
        ))),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_all_current_data_types() {
        assert_eq!(parse_data_type("bool").unwrap(), DataType::Bool);
        assert_eq!(parse_data_type("int8").unwrap(), DataType::Int8);
        assert_eq!(parse_data_type("int16").unwrap(), DataType::Int16);
        assert_eq!(parse_data_type("int32").unwrap(), DataType::Int32);
        assert_eq!(parse_data_type("int64").unwrap(), DataType::Int64);
        assert_eq!(parse_data_type("uint8").unwrap(), DataType::UInt8);
        assert_eq!(parse_data_type("uint16").unwrap(), DataType::UInt16);
        assert_eq!(parse_data_type("uint32").unwrap(), DataType::UInt32);
        assert_eq!(parse_data_type("uint64").unwrap(), DataType::UInt64);
        assert_eq!(parse_data_type("float").unwrap(), DataType::Float);
        assert_eq!(parse_data_type("double").unwrap(), DataType::Double);
        assert_eq!(parse_data_type("string").unwrap(), DataType::String);
        assert_eq!(parse_data_type("binary").unwrap(), DataType::Binary);
        assert_eq!(parse_data_type("gpslocation").unwrap(), DataType::GpsLocation);
    }

    #[test]
    fn bytearray_normalises_to_binary() {
        assert_eq!(parse_data_type("bytearray").unwrap(), DataType::Binary);
    }

    #[test]
    fn gpsdata_is_rejected_with_replacement_hint() {
        let err = parse_data_type("gpsdata").unwrap_err();
        match err {
            OsfError::RemovedInSpec2026_05_04 {
                field,
                value,
                replacement,
            } => {
                assert_eq!(field, "datatype");
                assert_eq!(value, "gpsdata");
                assert_eq!(replacement, Some("gpslocation"));
            }
            other => panic!("expected RemovedInSpec2026_05_04, got {other:?}"),
        }
    }

    #[test]
    fn pair_triple_candata_are_rejected() {
        for legacy in ["pair", "triple", "candata"] {
            assert!(
                matches!(
                    parse_data_type(legacy),
                    Err(OsfError::RemovedInSpec2026_05_04 { .. })
                ),
                "{legacy:?} should be rejected"
            );
        }
    }

    #[test]
    fn unknown_datatype_becomes_unsupported() {
        let dt = parse_data_type("future_type_xy").unwrap();
        assert_eq!(dt, DataType::Unsupported("future_type_xy".to_string()));
    }

    #[test]
    fn parses_channel_types() {
        assert_eq!(parse_channel_type("scalar").unwrap(), ChannelType::Scalar);
        assert_eq!(
            parse_channel_type("equidistant").unwrap(),
            ChannelType::Equidistant
        );
        assert_eq!(
            parse_channel_type("timestamped").unwrap(),
            ChannelType::Timestamped
        );
    }

    #[test]
    fn unknown_channel_type_becomes_unsupported() {
        let ct = parse_channel_type("vector").unwrap();
        assert_eq!(ct, ChannelType::Unsupported("vector".to_string()));
    }

    #[test]
    fn size_of_length_value_accepts_2_and_4() {
        assert_eq!(validate_size_of_length_value(2).unwrap(), 2);
        assert_eq!(validate_size_of_length_value(4).unwrap(), 4);
    }

    #[test]
    fn size_of_length_value_rejects_other_values() {
        for bad in [0u32, 1, 3, 5, 8, 100] {
            assert!(
                matches!(
                    validate_size_of_length_value(bad),
                    Err(OsfError::InvalidMetablock(_))
                ),
                "{bad} should be rejected"
            );
        }
    }
}
