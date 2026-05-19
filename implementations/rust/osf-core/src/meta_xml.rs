// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! OSF4 XML metablock parser.
//!
//! On disk an OSF4 metablock is a single `<optimeas>` element with
//! file-level attributes plus `<channels>` and optionally `<infos>`
//! children:
//!
//! ```xml
//! <?xml version="1.0" encoding="UTF-8"?>
//! <optimeas creator="..." created_utc="..." ...>
//!   <channels count="N">
//!     <channel index="0" name="..." channeltype="scalar"
//!              datatype="double" sizeoflengthvalue="2" .../>
//!     ...
//!   </channels>
//!   <infos>
//!     <info name="..." datatype="..." value="..."/>
//!   </infos>
//! </optimeas>
//! ```
//!
//! Implementation notes:
//!
//! 1. The parser drives `quick-xml::Reader` event-by-event rather than
//!    using `quick-xml::de`. Same reasons as the JSON parser: the wire
//!    spelling diverges from the public Rust API, removed-in-spec
//!    values must error, and deprecated fields must produce a
//!    `log::warn!` rather than be silently dropped.
//! 2. Real-world field files (`examples/steam_loco.osf`,
//!    `examples/motorbike.osf`) declare `encoding="UTF-8"` but in
//!    practice carry CP1252-encoded bytes for non-ASCII characters such
//!    as `°` in `°C`. To keep the parser usable on those files we
//!    pre-decode the input with [`String::from_utf8_lossy`], which
//!    replaces invalid sequences with the Unicode replacement
//!    character. The cost is a single garbled glyph in the affected
//!    physical-unit strings; the upside is that every shipped reference
//!    file parses without error.
//! 3. Validation logic is shared with the JSON parser via the helpers
//!    in [`crate::meta`].

use crate::error::OsfError;
use crate::meta::{
    Channel, FileInfo, Info, MetaBlock, REMOVED_CHANNEL_FIELDS, SpectrumType, parse_channel_type,
    parse_data_type, validate_size_of_length_value,
};
use log::{debug, warn};
use quick_xml::Reader;
use quick_xml::events::{BytesStart, Event};
use std::collections::HashMap;

/// Parse an OSF4 metablock body into a [`MetaBlock`].
///
/// `bytes` must contain exactly the metablock — no magic-header line
/// and no following block-stream bytes.
///
/// # Errors
///
/// Returns [`OsfError::Xml`] for low-level XML parser failures,
/// [`OsfError::InvalidMetablock`] for missing or malformed required
/// fields, [`OsfError::RemovedInSpec2026_05_04`] if a removed datatype
/// is referenced.
pub fn parse_metablock_xml(bytes: &[u8]) -> Result<MetaBlock, OsfError> {
    let body = String::from_utf8_lossy(bytes);
    let mut reader = Reader::from_str(&body);
    let config = reader.config_mut();
    config.trim_text(true);

    let mut buf = Vec::new();
    let mut state = ParseState::TopLevel;
    let mut metablock = MetaBlock::default();
    metablock.file_info.version = 4;

    loop {
        buf.clear();
        let event = reader
            .read_event_into(&mut buf)
            .map_err(|e| OsfError::Xml(format!("{e}")))?;

        match (event, &mut state) {
            (Event::Eof, _) => break,
            (Event::Decl(_) | Event::Comment(_) | Event::Text(_), _) => {}

            (Event::Start(start), ParseState::TopLevel) => match start.name().as_ref() {
                b"optimeas" => {
                    metablock.file_info = parse_optimeas_attrs(&start, metablock.file_info)?;
                    state = ParseState::InOptimeas;
                }
                other => {
                    return Err(OsfError::InvalidMetablock(format!(
                        "OSF4 root element must be <optimeas>, got <{}>",
                        String::from_utf8_lossy(other)
                    )));
                }
            },

            (Event::Start(start), ParseState::InOptimeas) => match start.name().as_ref() {
                b"channels" => state = ParseState::InChannels,
                b"infos" => state = ParseState::InInfos,
                other => {
                    let name = String::from_utf8_lossy(other).into_owned();
                    debug!("OSF4 unknown child <{name}> of <optimeas>; skipped");
                    drop(start);
                    skip_to_end_named(&mut reader, &name)?;
                }
            },

            (Event::Empty(start), ParseState::InOptimeas) => match start.name().as_ref() {
                b"channels" | b"infos" => {} // empty wrapper, nothing inside
                other => debug!(
                    "OSF4 unknown empty <{}/> under <optimeas>; skipped",
                    String::from_utf8_lossy(other)
                ),
            },

            (Event::Start(start) | Event::Empty(start), ParseState::InChannels) => {
                match start.name().as_ref() {
                    b"channel" => {
                        let chan = parse_channel_attrs(&start, metablock.channels.len())?;
                        metablock.channels.push(chan);
                    }
                    other => {
                        debug!(
                            "OSF4 unknown child <{}> inside <channels>; skipped",
                            String::from_utf8_lossy(other)
                        );
                    }
                }
            }

            (Event::Start(start) | Event::Empty(start), ParseState::InInfos) => {
                match start.name().as_ref() {
                    b"info" => {
                        let info = parse_info_attrs(&start)?;
                        metablock.infos.push(info);
                    }
                    other => {
                        debug!(
                            "OSF4 unknown child <{}> inside <infos>; skipped",
                            String::from_utf8_lossy(other)
                        );
                    }
                }
            }

            (Event::End(end), state_ref) => match end.name().as_ref() {
                b"channels" | b"infos" => *state_ref = ParseState::InOptimeas,
                b"optimeas" => *state_ref = ParseState::TopLevel,
                _ => {}
            },

            _ => {}
        }
    }

    Ok(metablock)
}

#[derive(Debug)]
enum ParseState {
    TopLevel,
    InOptimeas,
    InChannels,
    InInfos,
}

fn parse_optimeas_attrs(start: &BytesStart<'_>, mut info: FileInfo) -> Result<FileInfo, OsfError> {
    let attrs = collect_attrs(start)?;

    info.created_utc = attrs.get("created_utc").cloned();
    info.creator = attrs.get("creator").cloned();
    info.tag = attrs.get("tag").cloned();
    info.reason = attrs.get("reason").cloned();
    info.comment = attrs.get("comment").cloned();
    info.namespace_sep = attrs.get("namespacesep").cloned();
    info.created_at_latitude = parse_optional_f64(&attrs, "created_at_latitude")?;
    info.created_at_longitude = parse_optional_f64(&attrs, "created_at_longitude")?;
    info.created_at_altitude = parse_optional_f64(&attrs, "created_at_altitude")?;

    for (short, long, slot) in [
        ("latitude", "created_at_latitude", &mut info.created_at_latitude),
        (
            "longitude",
            "created_at_longitude",
            &mut info.created_at_longitude,
        ),
        ("altitude", "created_at_altitude", &mut info.created_at_altitude),
    ] {
        if slot.is_none() {
            if let Some(raw) = attrs.get(short) {
                let v = raw.parse::<f64>().map_err(|e| {
                    OsfError::InvalidMetablock(format!(
                        "<optimeas {short}={raw:?}> is not a valid float: {e}"
                    ))
                })?;
                debug!(
                    "OSF4 optimeas/{short} accepted as alternative for {long}; \
                     writers should emit the spec form"
                );
                *slot = Some(v);
            }
        }
    }

    let known = [
        "creator",
        "created_utc",
        "tag",
        "reason",
        "comment",
        "namespacesep",
        "created_at_latitude",
        "created_at_longitude",
        "created_at_altitude",
        "latitude",
        "longitude",
        "altitude",
    ];
    for key in attrs.keys() {
        if !known.contains(&key.as_str()) {
            debug!("OSF4 <optimeas {key:?}> attribute is not known to this build; ignored");
        }
    }

    Ok(info)
}

fn parse_channel_attrs(start: &BytesStart<'_>, position: usize) -> Result<Channel, OsfError> {
    let attrs = collect_attrs(start)?;

    let raw_index = attrs.get("index").ok_or_else(|| {
        OsfError::InvalidMetablock(format!(
            "OSF4 <channel> at position {position} is missing required attribute index"
        ))
    })?;
    let index: u16 = raw_index.parse().map_err(|_| {
        OsfError::InvalidMetablock(format!(
            "OSF4 <channel> at position {position} has index={raw_index:?} out of range"
        ))
    })?;

    let name = attrs
        .get("name")
        .cloned()
        .ok_or_else(|| {
            OsfError::InvalidMetablock(format!(
                "OSF4 <channel index={index}> is missing required attribute name"
            ))
        })?;

    let channel_type_raw = attrs.get("channeltype").ok_or_else(|| {
        OsfError::InvalidMetablock(format!(
            "OSF4 <channel name={name:?}> is missing required attribute channeltype"
        ))
    })?;
    let channel_type = parse_channel_type(channel_type_raw)?;

    let data_type_raw = attrs.get("datatype").ok_or_else(|| {
        OsfError::InvalidMetablock(format!(
            "OSF4 <channel name={name:?}> is missing required attribute datatype"
        ))
    })?;
    let data_type = parse_data_type(data_type_raw)?;

    let raw_size = attrs.get("sizeoflengthvalue").ok_or_else(|| {
        OsfError::InvalidMetablock(format!(
            "OSF4 <channel name={name:?}> is missing required attribute sizeoflengthvalue"
        ))
    })?;
    let size_parsed: u32 = raw_size.parse().map_err(|_| {
        OsfError::InvalidMetablock(format!(
            "OSF4 <channel name={name:?}> has sizeoflengthvalue={raw_size:?} (not numeric)"
        ))
    })?;
    let size_of_length_value = validate_size_of_length_value(size_parsed)?;

    let time_increment_ns = parse_optional_i64(&attrs, "timeincrement")?;
    let mime_type = attrs.get("mimetype").cloned();
    let physical_unit = attrs.get("physicalunit").cloned();
    let physical_dimension = attrs.get("physicaldimension").cloned();
    let display_name = attrs.get("displayname").cloned();
    let comment = attrs.get("comment").cloned();
    let reference = attrs.get("reference").cloned();

    let spectrum_type = attrs
        .get("spectrumtype")
        .map(|s| SpectrumType::from_wire(s.as_str()));

    for removed in REMOVED_CHANNEL_FIELDS {
        if attrs.contains_key(*removed) {
            warn!(
                "OSF4 <channel name={name:?}> carries deprecated attribute {removed:?} \
                 (removed in spec revision 2026-05-04); ignored"
            );
        }
    }

    let known = [
        "index",
        "name",
        "channeltype",
        "datatype",
        "sizeoflengthvalue",
        "timeincrement",
        "mimetype",
        "physicalunit",
        "physicaldimension",
        "displayname",
        "comment",
        "reference",
        "spectrumtype",
    ];
    for key in attrs.keys() {
        let k = key.as_str();
        if !known.contains(&k) && !REMOVED_CHANNEL_FIELDS.contains(&k) {
            debug!(
                "OSF4 <channel name={name:?}> attribute {key:?} is not known to this build; ignored"
            );
        }
    }

    Ok(Channel {
        index,
        name,
        reference,
        channel_type,
        data_type,
        time_increment_ns,
        size_of_length_value,
        mime_type,
        spectrum_type,
        physical_unit,
        physical_dimension,
        display_name,
        comment,
    })
}

fn parse_info_attrs(start: &BytesStart<'_>) -> Result<Info, OsfError> {
    let attrs = collect_attrs(start)?;
    let name = attrs
        .get("name")
        .cloned()
        .ok_or_else(|| OsfError::InvalidMetablock("OSF4 <info> is missing name".into()))?;
    let data_type_raw = attrs.get("datatype").map_or("string", String::as_str);
    let data_type = parse_data_type(data_type_raw)?;
    let value = attrs.get("value").cloned().unwrap_or_default();
    let physical_unit = attrs.get("physicalunit").cloned();
    Ok(Info {
        name,
        value,
        data_type,
        physical_unit,
    })
}

fn collect_attrs(start: &BytesStart<'_>) -> Result<HashMap<String, String>, OsfError> {
    let mut out = HashMap::new();
    for attr in start.attributes() {
        let attr = attr.map_err(|e| OsfError::Xml(format!("attribute parse error: {e}")))?;
        let key = String::from_utf8_lossy(attr.key.as_ref()).into_owned();
        let value = attr
            .unescape_value()
            .map_err(|e| OsfError::Xml(format!("attribute unescape error: {e}")))?
            .into_owned();
        out.insert(key, value);
    }
    Ok(out)
}

fn parse_optional_f64(attrs: &HashMap<String, String>, key: &str) -> Result<Option<f64>, OsfError> {
    attrs
        .get(key)
        .map(|s| {
            s.parse::<f64>().map_err(|e| {
                OsfError::InvalidMetablock(format!("<optimeas {key}={s:?}> is not a float: {e}"))
            })
        })
        .transpose()
}

fn parse_optional_i64(attrs: &HashMap<String, String>, key: &str) -> Result<Option<i64>, OsfError> {
    attrs
        .get(key)
        .map(|s| {
            s.parse::<i64>().map_err(|e| {
                OsfError::InvalidMetablock(format!("attribute {key}={s:?} is not an int: {e}"))
            })
        })
        .transpose()
}

fn skip_to_end_named(reader: &mut Reader<&[u8]>, name: &str) -> Result<(), OsfError> {
    let target = name.as_bytes();
    let mut buf = Vec::new();
    loop {
        buf.clear();
        let event = reader
            .read_event_into(&mut buf)
            .map_err(|e| OsfError::Xml(format!("{e}")))?;
        match event {
            Event::End(end) if end.name().as_ref() == target => return Ok(()),
            Event::Eof => {
                return Err(OsfError::InvalidMetablock(format!(
                    "OSF4 unexpected EOF while skipping <{name}>"
                )));
            }
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::{ChannelType, DataType};

    fn minimal() -> &'static [u8] {
        br#"<?xml version="1.0" encoding="UTF-8"?>
<optimeas creator="test" created_utc="2026-05-05T00:00:00Z">
  <channels count="1">
    <channel index="0" name="a" channeltype="scalar" datatype="double" sizeoflengthvalue="2"/>
  </channels>
</optimeas>"#
    }

    #[test]
    fn parses_minimal_metablock() {
        let mb = parse_metablock_xml(minimal()).unwrap();
        assert_eq!(mb.file_info.version, 4);
        assert_eq!(mb.file_info.creator.as_deref(), Some("test"));
        assert_eq!(mb.channels.len(), 1);
        assert_eq!(mb.channels[0].name, "a");
        assert_eq!(mb.channels[0].data_type, DataType::Double);
        assert_eq!(mb.channels[0].channel_type, ChannelType::Scalar);
    }

    #[test]
    fn deprecated_scale_offset_are_ignored() {
        let body = br#"<?xml version="1.0"?>
<optimeas>
  <channels count="1">
    <channel index="0" name="a" channeltype="scalar" datatype="double"
             sizeoflengthvalue="4" scale="1" offset="0"/>
  </channels>
</optimeas>"#;
        let mb = parse_metablock_xml(body).unwrap();
        assert_eq!(mb.channels.len(), 1);
        assert_eq!(mb.channels[0].size_of_length_value, 4);
    }

    #[test]
    fn gpsdata_datatype_is_rejected() {
        let body = br#"<?xml version="1.0"?>
<optimeas>
  <channels count="1">
    <channel index="0" name="a" channeltype="scalar" datatype="gpsdata"
             sizeoflengthvalue="4"/>
  </channels>
</optimeas>"#;
        let err = parse_metablock_xml(body).unwrap_err();
        assert!(matches!(err, OsfError::RemovedInSpec2026_05_04 { .. }), "got {err:?}");
    }

    #[test]
    fn short_gps_spelling_is_accepted() {
        let body = br#"<?xml version="1.0"?>
<optimeas latitude="48.1374" longitude="11.5755" altitude="519">
  <channels count="0"/>
</optimeas>"#;
        let mb = parse_metablock_xml(body).unwrap();
        assert_eq!(mb.file_info.created_at_latitude, Some(48.1374));
        assert_eq!(mb.file_info.created_at_longitude, Some(11.5755));
        assert_eq!(mb.file_info.created_at_altitude, Some(519.0));
    }

    #[test]
    fn invalid_size_of_length_value_is_rejected() {
        let body = br#"<?xml version="1.0"?>
<optimeas>
  <channels count="1">
    <channel index="0" name="a" channeltype="scalar" datatype="double"
             sizeoflengthvalue="3"/>
  </channels>
</optimeas>"#;
        let err = parse_metablock_xml(body).unwrap_err();
        assert!(matches!(err, OsfError::InvalidMetablock(_)), "got {err:?}");
    }

    #[test]
    fn unknown_channel_attribute_is_ignored() {
        let body = br#"<?xml version="1.0"?>
<optimeas>
  <channels count="1">
    <channel index="0" name="a" channeltype="scalar" datatype="double"
             sizeoflengthvalue="2" cannode="0" some_future_attr="x"/>
  </channels>
</optimeas>"#;
        let mb = parse_metablock_xml(body).unwrap();
        assert_eq!(mb.channels.len(), 1);
        assert_eq!(mb.channels[0].name, "a");
    }

    #[test]
    fn parses_multiple_channels_with_infos() {
        let body = br#"<?xml version="1.0"?>
<optimeas creator="x">
  <channels count="2">
    <channel index="0" name="a" channeltype="scalar" datatype="int32"
             sizeoflengthvalue="2"/>
    <channel index="1" name="b" channeltype="scalar" datatype="double"
             sizeoflengthvalue="4" timeincrement="1000000"/>
  </channels>
  <infos>
    <info name="machine" datatype="string" value="press42"/>
  </infos>
</optimeas>"#;
        let mb = parse_metablock_xml(body).unwrap();
        assert_eq!(mb.channels.len(), 2);
        assert_eq!(mb.channels[1].time_increment_ns, Some(1_000_000));
        assert_eq!(mb.infos.len(), 1);
        assert_eq!(mb.infos[0].name, "machine");
        assert_eq!(mb.infos[0].value, "press42");
    }
}
