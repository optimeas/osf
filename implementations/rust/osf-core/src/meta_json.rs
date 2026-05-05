// Copyright 2026 Optimeas GmbH
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! OSF5 JSON metablock parser.
//!
//! On disk an OSF5 metablock is a single JSON object wrapped in an
//! `osf` envelope:
//!
//! ```json
//! {"osf":{
//!   "format":"osf5","version":5,
//!   "file":{...},
//!   "channels":[{...},{...}],
//!   "infos":[{...}]
//! }}
//! ```
//!
//! The parser is intentionally hand-rolled on top of `serde_json::Value`
//! rather than driven by `#[derive(Deserialize)]`. Reasons:
//!
//! 1. The wire spelling diverges from the public Rust API
//!    (`channeltype` vs. `channel_type`, …); custom mapping is needed
//!    either way.
//! 2. Forward-compatibility (DECISIONS §16, ChannelType::Unsupported)
//!    rules out `serde(deny_unknown_fields)`.
//! 3. Removed-in-spec values must produce a clear error, and deprecated
//!    fields must produce a `log::warn!` rather than be silently
//!    discarded — both behaviours are easier to express directly than
//!    through serde adapters.

use crate::error::OsfError;
use crate::meta::{
    Channel, FileInfo, Info, MetaBlock, REMOVED_CHANNEL_FIELDS, SpectrumType, parse_channel_type,
    parse_data_type, validate_size_of_length_value,
};
use log::{debug, warn};
use serde_json::{Map, Value};

/// Parse an OSF5 metablock body into a [`MetaBlock`].
///
/// `bytes` must contain exactly the metablock — no magic-header line
/// and no following block-stream bytes.
///
/// # Errors
///
/// Returns [`OsfError::Json`] if the body is not valid JSON,
/// [`OsfError::InvalidMetablock`] if a required structural field is
/// missing or malformed, [`OsfError::RemovedInSpec2026_05_04`] if a
/// removed datatype is referenced.
pub fn parse_metablock_json(bytes: &[u8]) -> Result<MetaBlock, OsfError> {
    let root: Value = serde_json::from_slice(bytes)?;

    let outer = root.as_object().ok_or_else(|| {
        OsfError::InvalidMetablock("OSF5 root must be a JSON object".into())
    })?;

    let osf = outer.get("osf").and_then(Value::as_object).ok_or_else(|| {
        OsfError::InvalidMetablock("OSF5 root is missing the \"osf\" envelope".into())
    })?;

    if let Some(format) = osf.get("format").and_then(Value::as_str) {
        if format != "osf5" {
            warn!(
                "OSF5 metablock declares format={format:?}; expected \"osf5\". \
                 parsing as OSF5 anyway because the magic header was OSF5."
            );
        }
    }

    let mut file_info = parse_file_info(osf)?;
    file_info.version = 5;

    let channels = parse_channels(osf)?;
    let infos = parse_infos(osf)?;

    log_unknown_top_level_fields(osf);

    Ok(MetaBlock {
        file_info,
        channels,
        infos,
    })
}

fn parse_file_info(osf: &Map<String, Value>) -> Result<FileInfo, OsfError> {
    let file_obj = match osf.get("file") {
        Some(Value::Object(map)) => map,
        Some(_) => {
            return Err(OsfError::InvalidMetablock(
                "OSF5 \"file\" must be an object".into(),
            ));
        }
        None => {
            return Ok(FileInfo::default());
        }
    };

    let mut info = FileInfo {
        created_utc: file_obj.get("created_utc").and_then(value_to_string),
        creator: file_obj.get("creator").and_then(value_to_string),
        reason: file_obj.get("reason").and_then(value_to_string),
        tag: file_obj.get("tag").and_then(value_to_string),
        comment: file_obj.get("comment").and_then(value_to_string),
        namespace_sep: file_obj.get("namespacesep").and_then(value_to_string),
        created_at_latitude: file_obj.get("created_at_latitude").and_then(Value::as_f64),
        created_at_longitude: file_obj.get("created_at_longitude").and_then(Value::as_f64),
        created_at_altitude: file_obj.get("created_at_altitude").and_then(Value::as_f64),
        ..FileInfo::default()
    };

    // OSFGenerator-style short spelling that some early OSF5 emitters
    // produced. Spec form is `created_at_*`; we accept and normalise.
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
            if let Some(v) = file_obj.get(short).and_then(Value::as_f64) {
                debug!(
                    "OSF5 file.{short} accepted as alternative for {long}; \
                     writers should emit the spec form"
                );
                *slot = Some(v);
            }
        }
    }

    let known_file_fields = [
        "created_utc",
        "creator",
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
    for key in file_obj.keys() {
        if !known_file_fields.contains(&key.as_str()) {
            debug!("OSF5 file.{key:?} is not known to this build; ignored");
        }
    }

    Ok(info)
}

fn parse_channels(osf: &Map<String, Value>) -> Result<Vec<Channel>, OsfError> {
    let arr = match osf.get("channels") {
        Some(Value::Array(arr)) => arr,
        Some(_) => {
            return Err(OsfError::InvalidMetablock(
                "OSF5 \"channels\" must be an array".into(),
            ));
        }
        None => return Ok(Vec::new()),
    };

    let mut channels = Vec::with_capacity(arr.len());
    for (i, value) in arr.iter().enumerate() {
        let obj = value.as_object().ok_or_else(|| {
            OsfError::InvalidMetablock(format!(
                "OSF5 channels[{i}] must be an object, got {kind}",
                kind = value_kind(value)
            ))
        })?;
        channels.push(parse_channel(obj, i)?);
    }
    Ok(channels)
}

fn parse_channel(obj: &Map<String, Value>, position: usize) -> Result<Channel, OsfError> {
    let raw_index = obj
        .get("index")
        .and_then(Value::as_i64)
        .ok_or_else(|| {
            OsfError::InvalidMetablock(format!(
                "channel at position {position} is missing required field \"index\""
            ))
        })?;
    if !(0..=i64::from(u16::MAX)).contains(&raw_index) {
        return Err(OsfError::InvalidMetablock(format!(
            "channel at position {position} has index={raw_index} out of range 0..={}",
            u16::MAX
        )));
    }
    let index = raw_index as u16;

    let name = obj
        .get("name")
        .and_then(Value::as_str)
        .ok_or_else(|| {
            OsfError::InvalidMetablock(format!(
                "channel at index {index} is missing required field \"name\""
            ))
        })?
        .to_string();

    let channel_type_raw = obj.get("channeltype").and_then(Value::as_str).ok_or_else(|| {
        OsfError::InvalidMetablock(format!(
            "channel {name:?} is missing required field \"channeltype\""
        ))
    })?;
    let channel_type = parse_channel_type(channel_type_raw)?;

    let data_type_raw = obj.get("datatype").and_then(Value::as_str).ok_or_else(|| {
        OsfError::InvalidMetablock(format!(
            "channel {name:?} is missing required field \"datatype\""
        ))
    })?;
    let data_type = parse_data_type(data_type_raw)?;

    let raw_size = obj.get("sizeoflengthvalue").and_then(Value::as_u64).ok_or_else(|| {
        OsfError::InvalidMetablock(format!(
            "channel {name:?} is missing required field \"sizeoflengthvalue\""
        ))
    })?;
    let size_of_length_value = validate_size_of_length_value(u32::try_from(raw_size).map_err(
        |_| OsfError::InvalidMetablock(format!(
            "channel {name:?} sizeoflengthvalue={raw_size} out of range"
        )),
    )?)?;

    let time_increment_ns = obj.get("timeincrement").and_then(Value::as_i64);

    let mime_type = obj.get("mimetype").and_then(value_to_string);
    let physical_unit = obj.get("physicalunit").and_then(value_to_string);
    let physical_dimension = obj.get("physicaldimension").and_then(value_to_string);
    let display_name = obj.get("displayname").and_then(value_to_string);
    let comment = obj.get("comment").and_then(value_to_string);
    let reference = obj.get("reference").and_then(value_to_string);

    let spectrum_type = obj
        .get("spectrumtype")
        .and_then(Value::as_str)
        .map(SpectrumType::from_wire);

    for removed in REMOVED_CHANNEL_FIELDS {
        if obj.contains_key(*removed) {
            warn!(
                "channel {name:?} carries deprecated field {removed:?} \
                 (removed in spec revision 2026-05-04); ignored"
            );
        }
    }

    let known_channel_fields = [
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
    for key in obj.keys() {
        let k = key.as_str();
        if !known_channel_fields.contains(&k) && !REMOVED_CHANNEL_FIELDS.contains(&k) {
            debug!("channel {name:?}: field {key:?} is not known to this build; ignored");
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

fn parse_infos(osf: &Map<String, Value>) -> Result<Vec<Info>, OsfError> {
    let arr = match osf.get("infos") {
        Some(Value::Array(arr)) => arr,
        Some(_) => {
            return Err(OsfError::InvalidMetablock(
                "OSF5 \"infos\" must be an array".into(),
            ));
        }
        None => return Ok(Vec::new()),
    };

    let mut infos = Vec::with_capacity(arr.len());
    for (i, value) in arr.iter().enumerate() {
        let obj = value.as_object().ok_or_else(|| {
            OsfError::InvalidMetablock(format!("OSF5 infos[{i}] must be an object"))
        })?;

        let name = obj
            .get("name")
            .and_then(Value::as_str)
            .ok_or_else(|| {
                OsfError::InvalidMetablock(format!("OSF5 infos[{i}] is missing \"name\""))
            })?
            .to_string();

        let data_type_raw = obj.get("datatype").and_then(Value::as_str).unwrap_or("string");
        let data_type = parse_data_type(data_type_raw)?;

        let value_str = match obj.get("value") {
            Some(Value::String(s)) => s.clone(),
            Some(other) => other.to_string(),
            None => String::new(),
        };

        let physical_unit = obj.get("physicalunit").and_then(value_to_string);

        infos.push(Info {
            name,
            value: value_str,
            data_type,
            physical_unit,
        });
    }
    Ok(infos)
}

fn log_unknown_top_level_fields(osf: &Map<String, Value>) {
    let known = ["format", "version", "file", "channels", "infos"];
    for key in osf.keys() {
        if !known.contains(&key.as_str()) {
            debug!("OSF5 metablock top-level field {key:?} is not known to this build; ignored");
        }
    }
}

fn value_to_string(v: &Value) -> Option<String> {
    match v {
        Value::String(s) => Some(s.clone()),
        Value::Null => None,
        other => Some(other.to_string()),
    }
}

fn value_kind(v: &Value) -> &'static str {
    match v {
        Value::Null => "null",
        Value::Bool(_) => "bool",
        Value::Number(_) => "number",
        Value::String(_) => "string",
        Value::Array(_) => "array",
        Value::Object(_) => "object",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::{ChannelType, DataType};

    fn minimal() -> &'static [u8] {
        br#"{
          "osf": {
            "format": "osf5",
            "version": 5,
            "file": { "creator": "test" },
            "channels": [
              { "index": 0, "name": "a", "channeltype": "scalar",
                "datatype": "double", "sizeoflengthvalue": 2 }
            ]
          }
        }"#
    }

    #[test]
    fn parses_minimal_metablock() {
        let mb = parse_metablock_json(minimal()).unwrap();
        assert_eq!(mb.file_info.version, 5);
        assert_eq!(mb.file_info.creator.as_deref(), Some("test"));
        assert_eq!(mb.channels.len(), 1);
        assert_eq!(mb.channels[0].name, "a");
        assert_eq!(mb.channels[0].data_type, DataType::Double);
        assert_eq!(mb.channels[0].channel_type, ChannelType::Scalar);
        assert_eq!(mb.channels[0].size_of_length_value, 2);
    }

    #[test]
    fn deprecated_scale_field_is_ignored_with_warning() {
        let body = br#"{
          "osf": { "format":"osf5","version":5,
            "file": {},
            "channels": [
              { "index": 0, "name": "a", "channeltype": "scalar",
                "datatype": "double", "sizeoflengthvalue": 2,
                "scale": 1.0, "offset": 0.0 }
            ]
          }
        }"#;
        let mb = parse_metablock_json(body).unwrap();
        assert_eq!(mb.channels.len(), 1);
        // No assertion on logs — they are observable via env_logger when
        // the test is run with RUST_LOG=warn cargo test -- --nocapture.
    }

    #[test]
    fn gpsdata_datatype_is_rejected() {
        let body = br#"{
          "osf": { "format":"osf5","version":5,
            "file": {},
            "channels": [
              { "index": 0, "name": "a", "channeltype": "scalar",
                "datatype": "gpsdata", "sizeoflengthvalue": 4 }
            ]
          }
        }"#;
        let err = parse_metablock_json(body).unwrap_err();
        assert!(matches!(err, OsfError::RemovedInSpec2026_05_04 { .. }), "got {err:?}");
    }

    #[test]
    fn unknown_top_level_field_is_ignored() {
        let body = br#"{
          "osf": { "format":"osf5","version":5,
            "file":{}, "channels":[],
            "future_extension": "ignored"
          }
        }"#;
        let mb = parse_metablock_json(body).unwrap();
        assert!(mb.channels.is_empty());
    }

    #[test]
    fn missing_osf_envelope_is_rejected() {
        let body = br#"{ "format":"osf5","version":5,"file":{},"channels":[] }"#;
        let err = parse_metablock_json(body).unwrap_err();
        assert!(matches!(err, OsfError::InvalidMetablock(_)), "got {err:?}");
    }

    #[test]
    fn invalid_size_of_length_value_is_rejected() {
        let body = br#"{
          "osf": { "format":"osf5","version":5,
            "file": {},
            "channels": [
              { "index":0,"name":"a","channeltype":"scalar",
                "datatype":"double","sizeoflengthvalue":3 }
            ]
          }
        }"#;
        let err = parse_metablock_json(body).unwrap_err();
        assert!(matches!(err, OsfError::InvalidMetablock(_)), "got {err:?}");
    }

    #[test]
    fn timeincrement_zero_is_kept_explicitly() {
        let body = br#"{
          "osf": { "format":"osf5","version":5,
            "file": {},
            "channels": [
              { "index":0,"name":"a","channeltype":"scalar",
                "datatype":"double","sizeoflengthvalue":2,
                "timeincrement":0 }
            ]
          }
        }"#;
        let mb = parse_metablock_json(body).unwrap();
        assert_eq!(mb.channels[0].time_increment_ns, Some(0));
    }
}
