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

//! OSF5 block writer.
//!
//! Two API tiers, symmetric to the read side:
//!
//! - [`WriterBuilder`] — low-level accumulator. Add channels with
//!   [`WriterBuilder::add_channel`], push samples with one of the
//!   per-data-type `add_*` methods, then call `write_to_file` /
//!   `write_to`.
//! - `write_to_file(&DataManager, path)` — convenience that serialises
//!   an existing [`crate::DataManager`] back to disk. Always emits
//!   OSF5 (DECISIONS §6) regardless of the source format.
//!
//! Constraints encoded in the API and the implementation:
//!
//! - **OSF5 only** — no OSF4 writer. DECISIONS §6.
//! - **Block mode only** — the builder collects everything in memory
//!   and emits at the end. Streaming write is reserved for embedded
//!   implementations. DECISIONS §7.
//! - **No OSFZ** — writers never produce a compressed file.
//!   DECISIONS §12.
//! - **No trailer / no magic trailer** — OSF5 dropped both.
//! - **`bcStartData` numeric only**: equidistant blocks support `float`
//!   and `double` only per spec rev 2026-05-04.
//! - **String / binary trailing 0x00**: `bcAbsTimeStampData` payloads
//!   for these types end with the spec-mandated null terminator;
//!   handled centrally in [`crate::binary_write`].

use crate::binary_write;
use crate::block::GpsLocation;
use crate::data_channel::NumericValues;
use crate::error::OsfError;
use crate::meta::SpectrumType;
use crate::types::{ChannelType, DataType};
use log::debug;
use serde_json::{Map, Value, json};
use std::collections::HashMap;
use std::fs::File;
use std::io::{BufWriter, Write};
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

/// Magic-header identifier emitted by this writer.
const OSF5_IDENTIFIER: &str = "OSF5";

/// Crate version reported in the `creator` field if the caller did
/// not provide one.
const DEFAULT_CREATOR: &str = concat!("osf-core/", env!("CARGO_PKG_VERSION"));

/// Default `tag` per DECISIONS §13.
const DEFAULT_TAG: &str = "default";

/// Definition of one channel as it will appear in the metablock.
///
/// Use `..Default::default()` to fill in the optional fields:
///
/// ```
/// # use osf_core::writer::ChannelDef;
/// # use osf_core::{ChannelType, DataType};
/// let def = ChannelDef {
///     name: "Sensor/Temperature".into(),
///     data_type: DataType::Double,
///     channel_type: ChannelType::Scalar,
///     physical_unit: Some("\u{00B0}C".into()),
///     ..Default::default()
/// };
/// ```
#[derive(Debug, Clone, PartialEq)]
pub struct ChannelDef {
    /// Fully qualified channel name (e.g. `"Sensor/Temperature"`).
    pub name: String,
    /// Datatype that every block on this channel must carry.
    pub data_type: DataType,
    /// Channel-type spelling — usually `Scalar`.
    pub channel_type: ChannelType,
    /// Length-prefix width on disk. Must be 2 or 4. The writer auto-
    /// bumps to 4 if a single block would otherwise exceed 65 535
    /// bytes; the caller can override by setting `4` up front.
    pub size_of_length_value: u8,
    /// Optional physical unit string (`°C`, `bar`, …).
    pub physical_unit: Option<String>,
    /// Optional physical dimension (`temperature`, …).
    pub physical_dimension: Option<String>,
    /// Optional display name.
    pub display_name: Option<String>,
    /// MIME type for `binary` channels (e.g. `image/jpeg`).
    pub mime_type: Option<String>,
    /// Spectrum subtype — only relevant for spectrum channels.
    pub spectrum_type: Option<SpectrumType>,
    /// Free-form reference identifier.
    pub reference: Option<String>,
    /// Free-form comment.
    pub comment: Option<String>,
    /// Optional sample-rate hint in nanoseconds. The actual sample
    /// rate that drives equidistant block layout comes from the
    /// per-segment `sample_rate_hz`.
    pub time_increment_ns: Option<i64>,
}

impl Default for ChannelDef {
    fn default() -> Self {
        Self {
            name: String::new(),
            data_type: DataType::Double,
            channel_type: ChannelType::Scalar,
            size_of_length_value: 2,
            physical_unit: None,
            physical_dimension: None,
            display_name: None,
            mime_type: None,
            spectrum_type: None,
            reference: None,
            comment: None,
            time_increment_ns: None,
        }
    }
}

/// File-level metadata to embed in the metablock. Mirrors the read
/// side's [`crate::FileInfo`] but only the writer-controllable fields;
/// `created_utc` is filled in at write time, not by the builder.
#[derive(Debug, Clone, Default)]
pub(crate) struct FileInfoDraft {
    pub creator: Option<String>,
    pub tag: Option<String>,
    pub reason: Option<String>,
    pub created_at_latitude: Option<f64>,
    pub created_at_longitude: Option<f64>,
    pub created_at_altitude: Option<f64>,
    pub namespace_sep: Option<String>,
    pub comment: Option<String>,
}

/// One equidistant segment held by the builder until write time.
#[derive(Debug, Clone)]
pub(crate) struct EquidistantSegmentDraft {
    pub start_timestamp_ns: i64,
    pub sample_rate_hz: f64,
    pub values: NumericValues,
}

/// Storage for a single channel inside [`WriterBuilder`]. The variant
/// is locked in by the first `add_*` call on the channel; subsequent
/// calls of the wrong family fail with
/// [`OsfError::ChannelMixedBlockTypes`].
#[derive(Debug, Clone)]
pub(crate) enum ChannelData {
    Empty,
    Equidistant {
        segments: Vec<EquidistantSegmentDraft>,
    },
    Timestamped {
        timestamps_ns: Vec<i64>,
        values: NumericValues,
    },
    Variable {
        timestamps_ns: Vec<i64>,
        strings: Option<Vec<String>>,
        binaries: Option<Vec<Vec<u8>>>,
    },
}

/// Low-level OSF5 writer. Build the file by adding channels and
/// samples, then call [`Self::write_to_file`] to emit.
#[derive(Debug, Default)]
pub struct WriterBuilder {
    pub(crate) file_info: FileInfoDraft,
    pub(crate) channels: Vec<ChannelDef>,
    pub(crate) channel_data: Vec<ChannelData>,
    pub(crate) name_to_index: HashMap<String, u16>,
}

impl WriterBuilder {
    /// Start a new builder with empty file metadata and no channels.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Set the `creator` string (typically `"app:version"`).
    #[must_use]
    pub fn creator(mut self, value: impl Into<String>) -> Self {
        self.file_info.creator = Some(value.into());
        self
    }

    /// Set the `tag` field. Defaults to `"default"` at write time if
    /// not supplied.
    #[must_use]
    pub fn tag(mut self, value: impl Into<String>) -> Self {
        self.file_info.tag = Some(value.into());
        self
    }

    /// Set the `reason` field. Defaults to an empty string at write
    /// time if not supplied.
    #[must_use]
    pub fn reason(mut self, value: impl Into<String>) -> Self {
        self.file_info.reason = Some(value.into());
        self
    }

    /// Set the recording location (latitude / longitude / altitude in
    /// decimal degrees and meters).
    #[must_use]
    pub fn location(mut self, lat: f64, lon: f64, alt: f64) -> Self {
        self.file_info.created_at_latitude = Some(lat);
        self.file_info.created_at_longitude = Some(lon);
        self.file_info.created_at_altitude = Some(alt);
        self
    }

    /// Override the namespace separator. Defaults to `.` if not set.
    #[must_use]
    pub fn namespace_sep(mut self, value: impl Into<String>) -> Self {
        self.file_info.namespace_sep = Some(value.into());
        self
    }

    /// Set the free-form `comment` field.
    #[must_use]
    pub fn comment(mut self, value: impl Into<String>) -> Self {
        self.file_info.comment = Some(value.into());
        self
    }

    /// Register a channel definition. Returns the channel index that
    /// must be passed to subsequent `add_*` calls.
    ///
    /// # Errors
    ///
    /// - [`OsfError::InvalidMetablock`] when `size_of_length_value` is
    ///   not 2 or 4, or when the data type is `Unsupported` /
    ///   `ByteArray` (`ByteArray` is a read-side alias only — writers
    ///   emit `Binary`).
    pub fn add_channel(&mut self, def: ChannelDef) -> Result<u16, OsfError> {
        if !matches!(def.size_of_length_value, 2 | 4) {
            return Err(OsfError::InvalidMetablock(format!(
                "channel {:?}: size_of_length_value must be 2 or 4, got {}",
                def.name, def.size_of_length_value
            )));
        }
        if matches!(def.data_type, DataType::Unsupported(_)) {
            return Err(OsfError::InvalidMetablock(format!(
                "channel {:?}: data type Unsupported(_) cannot be written",
                def.name
            )));
        }
        if matches!(def.data_type, DataType::ByteArray) {
            return Err(OsfError::InvalidMetablock(format!(
                "channel {:?}: bytearray is a read-side alias; \
                 writers must declare data type as Binary",
                def.name
            )));
        }
        if matches!(def.channel_type, ChannelType::Unsupported(_)) {
            return Err(OsfError::InvalidMetablock(format!(
                "channel {:?}: channel type Unsupported(_) cannot be written",
                def.name
            )));
        }

        let index_usize = self.channels.len();
        let index = u16::try_from(index_usize).map_err(|_| {
            OsfError::InvalidMetablock(format!(
                "writer reached the {} channel limit",
                u16::MAX as usize + 1
            ))
        })?;

        self.name_to_index.insert(def.name.clone(), index);
        self.channels.push(def);
        self.channel_data.push(ChannelData::Empty);
        Ok(index)
    }

    /// Number of channels declared so far.
    #[must_use]
    pub fn channel_count(&self) -> usize {
        self.channels.len()
    }

    /// Resolve a channel index by name.
    #[must_use]
    pub fn channel_index(&self, name: &str) -> Option<u16> {
        self.name_to_index.get(name).copied()
    }

    /// Serialise the file to `path`. Always emits OSF5 (DECISIONS §6).
    ///
    /// # Errors
    ///
    /// - [`OsfError::WriterEmpty`] when no channels were declared.
    /// - [`OsfError::Io`] for any write failure.
    /// - [`OsfError::InvalidBlock`] for internal consistency checks.
    pub fn write_to_file(self, path: impl AsRef<Path>) -> Result<(), OsfError> {
        let file = File::create(path.as_ref())?;
        let writer = BufWriter::new(file);
        self.write_to(writer)
    }

    /// Serialise the file to any `Write` sink. Used by
    /// [`Self::write_to_file`] and exposed for callers that need to
    /// produce OSF5 to a memory buffer or a network socket.
    ///
    /// # Errors
    ///
    /// As [`Self::write_to_file`].
    pub fn write_to<W: Write>(mut self, mut writer: W) -> Result<(), OsfError> {
        if self.channels.is_empty() {
            return Err(OsfError::WriterEmpty);
        }

        autobump_size_of_length_value(&mut self.channels, &self.channel_data);

        let metablock = build_metablock_json(&self.file_info, &self.channels)?;
        write_magic_header(&mut writer, metablock.len() as u64)?;
        writer.write_all(&metablock)?;
        write_data_blocks(&mut writer, &self.channels, &self.channel_data)?;
        writer.flush()?;
        Ok(())
    }
}

/// Pre-pass before writing: variable-length channels (`string`,
/// `binary`) whose largest sample would not fit into a `u16` length
/// field are bumped up to `size_of_length_value = 4`. Numeric
/// channels are not bumped — the block writer splits them into
/// multiple blocks instead.
fn autobump_size_of_length_value(channels: &mut [ChannelDef], data: &[ChannelData]) {
    for (i, def) in channels.iter_mut().enumerate() {
        if def.size_of_length_value == 4 {
            continue;
        }
        let needed = match &data[i] {
            ChannelData::Variable {
                strings, binaries, ..
            } => {
                let s_max = strings.as_ref().map_or(0, |v| {
                    v.iter().map(String::len).max().unwrap_or(0)
                });
                let b_max = binaries.as_ref().map_or(0, |v| {
                    v.iter().map(Vec::len).max().unwrap_or(0)
                });
                let sample_bytes = s_max.max(b_max);
                // Variable layout for one sample per block:
                // [control][u32 N=1][i64 ts][bytes][0x00] = 14 + sample_bytes
                14 + sample_bytes
            }
            _ => 0,
        };
        if needed > MAX_BLOCK_PAYLOAD_U16 {
            debug!(
                "channel {i} {:?}: auto-bumping size_of_length_value 2 -> 4 \
                 (a single sample would need {needed} bytes)",
                def.name
            );
            def.size_of_length_value = 4;
        }
    }
}

/// Write the magic-header line: `OSF5 <metablock_len>\n`.
fn write_magic_header<W: Write>(writer: &mut W, metablock_len: u64) -> Result<(), OsfError> {
    let line = format!("{OSF5_IDENTIFIER} {metablock_len}\n");
    writer.write_all(line.as_bytes())?;
    Ok(())
}

/// Maximum payload bytes (control byte + body) that fit into a
/// `u16` length field.
const MAX_BLOCK_PAYLOAD_U16: usize = u16::MAX as usize;

/// Soft cap for `u32` length fields — a single block of ~2 GB is
/// already enormous; pinning it just below `i32::MAX` avoids
/// platform-dependent overflow on the body length conversion.
const MAX_BLOCK_PAYLOAD_U32: usize = (i32::MAX as usize) - 1024;

/// Control byte values per spec rev 2026-05-04.
const CONTROL_CONTINUED_DATA: u8 = 0x05;
const CONTROL_START_DATA: u8 = 0x06;
const CONTROL_ABS_TIMESTAMP: u8 = 0x08;
const MULTI_SAMPLE_FLAG: u8 = 0x80;

/// Top-level dispatch: walks every channel and writes its data blocks.
fn write_data_blocks<W: Write>(
    writer: &mut W,
    channels: &[ChannelDef],
    channel_data: &[ChannelData],
) -> Result<(), OsfError> {
    for (index, def) in channels.iter().enumerate() {
        let channel = u16::try_from(index).expect("channel index ≤ u16::MAX validated earlier");
        match &channel_data[index] {
            ChannelData::Empty => {}
            ChannelData::Equidistant { segments } => {
                for segment in segments {
                    write_equidistant_segment(writer, channel, def, segment)?;
                }
            }
            ChannelData::Timestamped {
                timestamps_ns,
                values,
            } => {
                write_abs_timestamp_numeric(writer, channel, def, timestamps_ns, values)?;
            }
            ChannelData::Variable {
                timestamps_ns,
                strings,
                binaries,
            } => {
                write_abs_timestamp_variable(writer, channel, def, timestamps_ns, strings, binaries)?;
            }
        }
    }
    Ok(())
}

/// Maximum payload size in bytes for the channel's `size_of_length_value`.
fn max_block_payload(size_of_length_value: u8) -> usize {
    match size_of_length_value {
        2 => MAX_BLOCK_PAYLOAD_U16,
        4 => MAX_BLOCK_PAYLOAD_U32,
        _ => MAX_BLOCK_PAYLOAD_U16,
    }
}

/// Bytes per sample on disk for the given numeric data type.
fn numeric_byte_size(dt: &DataType) -> Result<usize, OsfError> {
    Ok(match dt {
        DataType::Bool | DataType::Int8 | DataType::UInt8 => 1,
        DataType::Int16 | DataType::UInt16 => 2,
        DataType::Int32 | DataType::UInt32 | DataType::Float => 4,
        DataType::Int64 | DataType::UInt64 | DataType::Double => 8,
        DataType::GpsLocation => 24,
        DataType::String | DataType::Binary | DataType::ByteArray => {
            return Err(OsfError::InvalidBlock(
                "string / binary do not have a fixed sample size".into(),
            ));
        }
        DataType::Unsupported(_) => {
            return Err(OsfError::InvalidBlock(
                "Unsupported data type cannot be written".into(),
            ));
        }
    })
}

/// Write the `[u16 channel][len][payload]` framing once the body of a
/// block has been built into a `Vec<u8>`.
fn write_block<W: Write>(
    writer: &mut W,
    channel: u16,
    size_of_length_value: u8,
    payload: &[u8],
) -> Result<(), OsfError> {
    binary_write::write_u16(writer, channel)?;
    let len = u32::try_from(payload.len()).map_err(|_| {
        OsfError::InvalidBlock(format!(
            "channel {channel}: block payload {} bytes overflows u32 length field",
            payload.len()
        ))
    })?;
    if size_of_length_value == 2 && payload.len() > MAX_BLOCK_PAYLOAD_U16 {
        return Err(OsfError::InvalidBlock(format!(
            "channel {channel}: block payload {} bytes exceeds u16 length field; \
             increase size_of_length_value to 4",
            payload.len()
        )));
    }
    binary_write::write_length_field(writer, size_of_length_value, len)?;
    writer.write_all(payload)?;
    Ok(())
}

// -----------------------------------------------------------
// Equidistant: bcStartData (first chunk) + bcContinuedData (rest).
// -----------------------------------------------------------

fn write_equidistant_segment<W: Write>(
    writer: &mut W,
    channel: u16,
    def: &ChannelDef,
    segment: &EquidistantSegmentDraft,
) -> Result<(), OsfError> {
    let sample_size = numeric_byte_size(&def.data_type)?;
    let total = segment.values.len();
    let max_payload = max_block_payload(def.size_of_length_value);

    // bcStartData multi-sample fixed overhead:
    //   1 (control) + 8 (i64 ts) + 8 (f64 rate) + 4 (u32 N) = 21
    let start_overhead = 1 + 8 + 8 + 4;
    let max_samples_start = (max_payload - start_overhead) / sample_size;
    let max_samples_start = max_samples_start.max(1);

    // bcContinuedData multi-sample fixed overhead:
    //   1 (control) + 4 (u32 N) = 5
    let continued_overhead = 1 + 4;
    let max_samples_continued = (max_payload - continued_overhead) / sample_size;
    let max_samples_continued = max_samples_continued.max(1);

    let first_chunk = total.min(max_samples_start);
    write_start_data_block(
        writer,
        channel,
        def.size_of_length_value,
        segment.start_timestamp_ns,
        segment.sample_rate_hz,
        &segment.values,
        0,
        first_chunk,
    )?;

    let mut written = first_chunk;
    while written < total {
        let chunk = (total - written).min(max_samples_continued);
        write_continued_data_block(
            writer,
            channel,
            def.size_of_length_value,
            &segment.values,
            written,
            chunk,
        )?;
        written += chunk;
    }
    Ok(())
}

#[allow(clippy::too_many_arguments)] // 8 fields are all genuinely needed for one block
fn write_start_data_block<W: Write>(
    writer: &mut W,
    channel: u16,
    size_of_length_value: u8,
    start_timestamp_ns: i64,
    sample_rate_hz: f64,
    values: &NumericValues,
    start: usize,
    count: usize,
) -> Result<(), OsfError> {
    let multi = count != 1;
    let mut payload = Vec::with_capacity(32);
    binary_write::write_u8(
        &mut payload,
        if multi {
            CONTROL_START_DATA | MULTI_SAMPLE_FLAG
        } else {
            CONTROL_START_DATA
        },
    )?;
    binary_write::write_i64(&mut payload, start_timestamp_ns)?;
    binary_write::write_f64(&mut payload, sample_rate_hz)?;
    if multi {
        binary_write::write_u32(&mut payload, count as u32)?;
    }
    write_numeric_values_slice(&mut payload, values, start, count)?;
    write_block(writer, channel, size_of_length_value, &payload)
}

fn write_continued_data_block<W: Write>(
    writer: &mut W,
    channel: u16,
    size_of_length_value: u8,
    values: &NumericValues,
    start: usize,
    count: usize,
) -> Result<(), OsfError> {
    let multi = count != 1;
    let mut payload = Vec::with_capacity(16);
    binary_write::write_u8(
        &mut payload,
        if multi {
            CONTROL_CONTINUED_DATA | MULTI_SAMPLE_FLAG
        } else {
            CONTROL_CONTINUED_DATA
        },
    )?;
    if multi {
        binary_write::write_u32(&mut payload, count as u32)?;
    }
    write_numeric_values_slice(&mut payload, values, start, count)?;
    write_block(writer, channel, size_of_length_value, &payload)
}

// -----------------------------------------------------------
// Timestamped numeric: bcAbsTimeStampData with multi-sample bit set.
// -----------------------------------------------------------

fn write_abs_timestamp_numeric<W: Write>(
    writer: &mut W,
    channel: u16,
    def: &ChannelDef,
    timestamps_ns: &[i64],
    values: &NumericValues,
) -> Result<(), OsfError> {
    let sample_size = numeric_byte_size(&def.data_type)?;
    let total = timestamps_ns.len();
    if total == 0 {
        return Ok(());
    }

    let max_payload = max_block_payload(def.size_of_length_value);
    // [control][u32 N][N × (i64 ts + value)]
    // overhead = 1 + 4 = 5; per-sample = 8 + sample_size.
    let per_sample = 8 + sample_size;
    let overhead = 1 + 4;
    let max_samples = ((max_payload - overhead) / per_sample).max(1);

    let mut written = 0;
    while written < total {
        let chunk = (total - written).min(max_samples);
        let mut payload = Vec::with_capacity(overhead + chunk * per_sample);
        binary_write::write_u8(
            &mut payload,
            CONTROL_ABS_TIMESTAMP | MULTI_SAMPLE_FLAG,
        )?;
        binary_write::write_u32(&mut payload, chunk as u32)?;
        write_timestamped_numeric_run(
            &mut payload,
            timestamps_ns,
            values,
            written,
            chunk,
        )?;
        write_block(writer, channel, def.size_of_length_value, &payload)?;
        written += chunk;
    }
    Ok(())
}

// -----------------------------------------------------------
// Timestamped variable: one sample per block; bcAbsTimeStampData with
// the multi-sample bit set and N = 1, payload ends with 0x00.
// -----------------------------------------------------------

fn write_abs_timestamp_variable<W: Write>(
    writer: &mut W,
    channel: u16,
    def: &ChannelDef,
    timestamps_ns: &[i64],
    strings: &Option<Vec<String>>,
    binaries: &Option<Vec<Vec<u8>>>,
) -> Result<(), OsfError> {
    debug_assert!(strings.is_some() ^ binaries.is_some());
    let count = timestamps_ns.len();
    if count == 0 {
        return Ok(());
    }

    if let Some(ss) = strings {
        for (ts, s) in timestamps_ns.iter().zip(ss.iter()) {
            write_variable_one_string(writer, channel, def, *ts, s)?;
        }
    } else if let Some(bs) = binaries {
        for (ts, b) in timestamps_ns.iter().zip(bs.iter()) {
            write_variable_one_binary(writer, channel, def, *ts, b)?;
        }
    }
    Ok(())
}

fn write_variable_one_string<W: Write>(
    writer: &mut W,
    channel: u16,
    def: &ChannelDef,
    timestamp_ns: i64,
    s: &str,
) -> Result<(), OsfError> {
    let payload_len = variable_payload_size(s.len());
    check_variable_block_fits(channel, def.size_of_length_value, payload_len, s.len())?;
    let mut payload = Vec::with_capacity(payload_len);
    write_variable_header(&mut payload, timestamp_ns)?;
    binary_write::write_string_with_terminator(&mut payload, s)?;
    write_block(writer, channel, def.size_of_length_value, &payload)
}

fn write_variable_one_binary<W: Write>(
    writer: &mut W,
    channel: u16,
    def: &ChannelDef,
    timestamp_ns: i64,
    bytes: &[u8],
) -> Result<(), OsfError> {
    let payload_len = variable_payload_size(bytes.len());
    check_variable_block_fits(channel, def.size_of_length_value, payload_len, bytes.len())?;
    let mut payload = Vec::with_capacity(payload_len);
    write_variable_header(&mut payload, timestamp_ns)?;
    binary_write::write_binary_with_terminator(&mut payload, bytes)?;
    write_block(writer, channel, def.size_of_length_value, &payload)
}

/// Total payload bytes for a single-sample variable block:
/// `[control][u32 N=1][i64 ts][bytes][0x00]`.
fn variable_payload_size(sample_bytes: usize) -> usize {
    1 + 4 + 8 + sample_bytes + 1
}

fn write_variable_header<W: Write>(w: &mut W, timestamp_ns: i64) -> Result<(), OsfError> {
    binary_write::write_u8(w, CONTROL_ABS_TIMESTAMP | MULTI_SAMPLE_FLAG)?;
    binary_write::write_u32(w, 1)?;
    binary_write::write_i64(w, timestamp_ns)?;
    Ok(())
}

fn check_variable_block_fits(
    channel: u16,
    size_of_length_value: u8,
    payload_len: usize,
    sample_bytes: usize,
) -> Result<(), OsfError> {
    if size_of_length_value == 2 && payload_len > MAX_BLOCK_PAYLOAD_U16 {
        return Err(OsfError::InvalidBlock(format!(
            "channel {channel}: variable sample {sample_bytes} bytes exceeds u16 block limit \
             (auto-bump should have caught this; raise size_of_length_value to 4)"
        )));
    }
    Ok(())
}

// -----------------------------------------------------------
// Per-sample writers — dispatch over NumericValues variants.
// -----------------------------------------------------------

fn write_numeric_values_slice<W: Write>(
    w: &mut W,
    values: &NumericValues,
    start: usize,
    count: usize,
) -> Result<(), OsfError> {
    match values {
        NumericValues::Bool(v) => {
            for &x in &v[start..start + count] {
                binary_write::write_bool(w, x)?;
            }
        }
        NumericValues::Int8(v) => {
            for &x in &v[start..start + count] {
                binary_write::write_i8(w, x)?;
            }
        }
        NumericValues::Int16(v) => {
            for &x in &v[start..start + count] {
                binary_write::write_i16(w, x)?;
            }
        }
        NumericValues::Int32(v) => {
            for &x in &v[start..start + count] {
                binary_write::write_i32(w, x)?;
            }
        }
        NumericValues::Int64(v) => {
            for &x in &v[start..start + count] {
                binary_write::write_i64(w, x)?;
            }
        }
        NumericValues::UInt8(v) => {
            for &x in &v[start..start + count] {
                binary_write::write_u8(w, x)?;
            }
        }
        NumericValues::UInt16(v) => {
            for &x in &v[start..start + count] {
                binary_write::write_u16(w, x)?;
            }
        }
        NumericValues::UInt32(v) => {
            for &x in &v[start..start + count] {
                binary_write::write_u32(w, x)?;
            }
        }
        NumericValues::UInt64(v) => {
            for &x in &v[start..start + count] {
                binary_write::write_u64(w, x)?;
            }
        }
        NumericValues::Float(v) => {
            for &x in &v[start..start + count] {
                binary_write::write_f32(w, x)?;
            }
        }
        NumericValues::Double(v) => {
            for &x in &v[start..start + count] {
                binary_write::write_f64(w, x)?;
            }
        }
        NumericValues::GpsLocation(v) => {
            for g in &v[start..start + count] {
                binary_write::write_f64(w, g.latitude)?;
                binary_write::write_f64(w, g.longitude)?;
                binary_write::write_f64(w, g.altitude)?;
            }
        }
    }
    Ok(())
}

fn write_timestamped_numeric_run<W: Write>(
    w: &mut W,
    timestamps_ns: &[i64],
    values: &NumericValues,
    start: usize,
    count: usize,
) -> Result<(), OsfError> {
    match values {
        NumericValues::Bool(v) => {
            for i in start..start + count {
                binary_write::write_i64(w, timestamps_ns[i])?;
                binary_write::write_bool(w, v[i])?;
            }
        }
        NumericValues::Int8(v) => {
            for i in start..start + count {
                binary_write::write_i64(w, timestamps_ns[i])?;
                binary_write::write_i8(w, v[i])?;
            }
        }
        NumericValues::Int16(v) => {
            for i in start..start + count {
                binary_write::write_i64(w, timestamps_ns[i])?;
                binary_write::write_i16(w, v[i])?;
            }
        }
        NumericValues::Int32(v) => {
            for i in start..start + count {
                binary_write::write_i64(w, timestamps_ns[i])?;
                binary_write::write_i32(w, v[i])?;
            }
        }
        NumericValues::Int64(v) => {
            for i in start..start + count {
                binary_write::write_i64(w, timestamps_ns[i])?;
                binary_write::write_i64(w, v[i])?;
            }
        }
        NumericValues::UInt8(v) => {
            for i in start..start + count {
                binary_write::write_i64(w, timestamps_ns[i])?;
                binary_write::write_u8(w, v[i])?;
            }
        }
        NumericValues::UInt16(v) => {
            for i in start..start + count {
                binary_write::write_i64(w, timestamps_ns[i])?;
                binary_write::write_u16(w, v[i])?;
            }
        }
        NumericValues::UInt32(v) => {
            for i in start..start + count {
                binary_write::write_i64(w, timestamps_ns[i])?;
                binary_write::write_u32(w, v[i])?;
            }
        }
        NumericValues::UInt64(v) => {
            for i in start..start + count {
                binary_write::write_i64(w, timestamps_ns[i])?;
                binary_write::write_u64(w, v[i])?;
            }
        }
        NumericValues::Float(v) => {
            for i in start..start + count {
                binary_write::write_i64(w, timestamps_ns[i])?;
                binary_write::write_f32(w, v[i])?;
            }
        }
        NumericValues::Double(v) => {
            for i in start..start + count {
                binary_write::write_i64(w, timestamps_ns[i])?;
                binary_write::write_f64(w, v[i])?;
            }
        }
        NumericValues::GpsLocation(v) => {
            for i in start..start + count {
                binary_write::write_i64(w, timestamps_ns[i])?;
                binary_write::write_f64(w, v[i].latitude)?;
                binary_write::write_f64(w, v[i].longitude)?;
                binary_write::write_f64(w, v[i].altitude)?;
            }
        }
    }
    Ok(())
}

// -----------------------------------------------------------
// Metablock JSON serialisation.
// -----------------------------------------------------------

/// Build the OSF5 metablock JSON body. The output is pretty-printed
/// (`serde_json::to_vec_pretty`) so files remain reasonably
/// human-readable in a hex-viewer and small enough that we don't pay
/// a measurable size penalty in normal usage.
fn build_metablock_json(
    file_info: &FileInfoDraft,
    channels: &[ChannelDef],
) -> Result<Vec<u8>, OsfError> {
    let mut file_obj = Map::new();
    file_obj.insert("created_utc".into(), Value::String(format_utc_now()));
    file_obj.insert(
        "creator".into(),
        Value::String(
            file_info
                .creator
                .clone()
                .unwrap_or_else(|| DEFAULT_CREATOR.to_string()),
        ),
    );
    file_obj.insert(
        "tag".into(),
        Value::String(
            file_info
                .tag
                .clone()
                .unwrap_or_else(|| DEFAULT_TAG.to_string()),
        ),
    );
    if let Some(reason) = &file_info.reason {
        file_obj.insert("reason".into(), Value::String(reason.clone()));
    }
    if let Some(sep) = &file_info.namespace_sep {
        file_obj.insert("namespacesep".into(), Value::String(sep.clone()));
    }
    if let Some(comment) = &file_info.comment {
        file_obj.insert("comment".into(), Value::String(comment.clone()));
    }
    if let Some(lat) = file_info.created_at_latitude {
        file_obj.insert("created_at_latitude".into(), json!(lat));
    }
    if let Some(lon) = file_info.created_at_longitude {
        file_obj.insert("created_at_longitude".into(), json!(lon));
    }
    if let Some(alt) = file_info.created_at_altitude {
        file_obj.insert("created_at_altitude".into(), json!(alt));
    }

    let channels_arr: Vec<Value> = channels
        .iter()
        .enumerate()
        .map(|(i, def)| channel_def_to_json(i as u16, def))
        .collect();

    let envelope = json!({
        "osf": {
            "format": "osf5",
            "version": 5,
            "file": Value::Object(file_obj),
            "channels": Value::Array(channels_arr),
        }
    });

    serde_json::to_vec_pretty(&envelope).map_err(OsfError::Json)
}

fn channel_def_to_json(index: u16, def: &ChannelDef) -> Value {
    let mut obj = Map::new();
    obj.insert("index".into(), json!(index));
    obj.insert("name".into(), Value::String(def.name.clone()));
    obj.insert(
        "channeltype".into(),
        Value::String(channel_type_to_wire(&def.channel_type).to_string()),
    );
    obj.insert(
        "datatype".into(),
        Value::String(data_type_to_wire(&def.data_type).to_string()),
    );
    obj.insert("sizeoflengthvalue".into(), json!(def.size_of_length_value));

    if let Some(t) = def.time_increment_ns {
        obj.insert("timeincrement".into(), json!(t));
    }
    if let Some(s) = &def.physical_unit {
        obj.insert("physicalunit".into(), Value::String(s.clone()));
    }
    if let Some(s) = &def.physical_dimension {
        obj.insert("physicaldimension".into(), Value::String(s.clone()));
    }
    if let Some(s) = &def.display_name {
        obj.insert("displayname".into(), Value::String(s.clone()));
    }
    if let Some(s) = &def.mime_type {
        obj.insert("mimetype".into(), Value::String(s.clone()));
    }
    if let Some(s) = &def.reference {
        obj.insert("reference".into(), Value::String(s.clone()));
    }
    if let Some(s) = &def.comment {
        obj.insert("comment".into(), Value::String(s.clone()));
    }
    if let Some(spec) = def.spectrum_type {
        obj.insert(
            "spectrumtype".into(),
            Value::String(spectrum_type_to_wire(spec).to_string()),
        );
    }

    Value::Object(obj)
}

fn channel_type_to_wire(ct: &ChannelType) -> &'static str {
    match ct {
        ChannelType::Scalar => "scalar",
        ChannelType::Equidistant => "equidistant",
        ChannelType::Timestamped => "timestamped",
        ChannelType::Unsupported(_) => "scalar", // rejected at add_channel; defensive
    }
}

fn data_type_to_wire(dt: &DataType) -> &'static str {
    match dt {
        DataType::Bool => "bool",
        DataType::Int8 => "int8",
        DataType::Int16 => "int16",
        DataType::Int32 => "int32",
        DataType::Int64 => "int64",
        DataType::UInt8 => "uint8",
        DataType::UInt16 => "uint16",
        DataType::UInt32 => "uint32",
        DataType::UInt64 => "uint64",
        DataType::Float => "float",
        DataType::Double => "double",
        DataType::String => "string",
        DataType::Binary | DataType::ByteArray => "binary",
        DataType::GpsLocation => "gpslocation",
        // add_channel rejects Unsupported up front so this branch
        // should be unreachable, but we render something sensible
        // rather than panic.
        DataType::Unsupported(_) => "double",
    }
}

fn spectrum_type_to_wire(st: SpectrumType) -> &'static str {
    match st {
        SpectrumType::Amplitude => "amplitude",
        SpectrumType::RealImag => "realimag",
        SpectrumType::AmpPhaseRad => "ampphaserad",
        SpectrumType::AmpPhaseDeg => "ampphasedeg",
    }
}

// -----------------------------------------------------------
// `created_utc` helper: format current UTC time without chrono.
// -----------------------------------------------------------

/// Format the current UTC time as `YYYY-MM-DDTHH:MM:SSZ`.
///
/// Uses Howard Hinnant's days-from-civil algorithm
/// (<https://howardhinnant.github.io/date_algorithms.html>) — public
/// domain, no chrono dependency. Sub-second precision is dropped
/// because the spec writer never needed it; if a future revision
/// does, the format can be extended additively.
fn format_utc_now() -> String {
    let secs = match SystemTime::now().duration_since(UNIX_EPOCH) {
        Ok(d) => d.as_secs() as i64,
        Err(e) => -(e.duration().as_secs() as i64),
    };
    format_unix_seconds_utc(secs)
}

/// Pure function used by `format_utc_now` and tests.
fn format_unix_seconds_utc(secs: i64) -> String {
    let day = secs.div_euclid(86_400);
    let tod = secs.rem_euclid(86_400);
    let h = (tod / 3600) as u32;
    let m = ((tod % 3600) / 60) as u32;
    let s = (tod % 60) as u32;
    let (y, mo, d) = days_from_civil(day);
    format!("{y:04}-{mo:02}-{d:02}T{h:02}:{m:02}:{s:02}Z")
}

/// Howard Hinnant's days-from-civil — converts days since
/// 1970-01-01 (Unix epoch) into a `(year, month, day)` tuple. Valid
/// for any 32-bit-day input; we use it on i64 days, which covers
/// roughly ±25 million years.
fn days_from_civil(days: i64) -> (i32, u32, u32) {
    let z = days + 719_468;
    let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    let y = if m <= 2 { y + 1 } else { y };
    (y as i32, m, d)
}

// -----------------------------------------------------------
// Equidistant: f32 and f64 only (spec rev 2026-05-04 limitation).
// -----------------------------------------------------------

impl WriterBuilder {
    /// Append an equidistant segment of `f64` samples to the given
    /// channel.
    ///
    /// # Errors
    ///
    /// - [`OsfError::ChannelNotFound`] if `channel` is out of range.
    /// - [`OsfError::DataTypeMismatch`] if the channel's declared
    ///   data type is not `Double`.
    /// - [`OsfError::ChannelMixedBlockTypes`] if the channel already
    ///   holds timestamped or variable data.
    /// - [`OsfError::InvalidBlock`] if `sample_rate_hz <= 0`.
    pub fn add_equidistant_segment_f64(
        &mut self,
        channel: u16,
        start_ns: i64,
        rate_hz: f64,
        values: &[f64],
    ) -> Result<(), OsfError> {
        self.add_equidistant_segment_inner(
            channel,
            DataType::Double,
            start_ns,
            rate_hz,
            NumericValues::Double(values.to_vec()),
        )
    }

    /// Append an equidistant segment of `f32` samples to the given
    /// channel.
    ///
    /// # Errors
    ///
    /// As [`Self::add_equidistant_segment_f64`].
    pub fn add_equidistant_segment_f32(
        &mut self,
        channel: u16,
        start_ns: i64,
        rate_hz: f64,
        values: &[f32],
    ) -> Result<(), OsfError> {
        self.add_equidistant_segment_inner(
            channel,
            DataType::Float,
            start_ns,
            rate_hz,
            NumericValues::Float(values.to_vec()),
        )
    }

    fn add_equidistant_segment_inner(
        &mut self,
        channel: u16,
        expected: DataType,
        start_ns: i64,
        rate_hz: f64,
        values: NumericValues,
    ) -> Result<(), OsfError> {
        let def = self.channel_def(channel)?;
        if !matches!(def.data_type, DataType::Float | DataType::Double) {
            return Err(OsfError::InvalidBlock(format!(
                "channel {channel}: equidistant blocks support only float and \
                 double per spec rev 2026-05-04 (channel declares {:?})",
                def.data_type
            )));
        }
        if def.data_type != expected {
            return Err(OsfError::DataTypeMismatch {
                channel,
                expected: def.data_type.clone(),
                got: expected,
            });
        }
        if rate_hz.is_nan() || rate_hz <= 0.0 {
            return Err(OsfError::InvalidBlock(format!(
                "channel {channel}: equidistant sample_rate_hz must be > 0, got {rate_hz}"
            )));
        }

        let data = &mut self.channel_data[channel as usize];
        match data {
            ChannelData::Empty => {
                *data = ChannelData::Equidistant {
                    segments: vec![EquidistantSegmentDraft {
                        start_timestamp_ns: start_ns,
                        sample_rate_hz: rate_hz,
                        values,
                    }],
                };
            }
            ChannelData::Equidistant { segments } => {
                segments.push(EquidistantSegmentDraft {
                    start_timestamp_ns: start_ns,
                    sample_rate_hz: rate_hz,
                    values,
                });
            }
            ChannelData::Timestamped { .. } | ChannelData::Variable { .. } => {
                return Err(OsfError::ChannelMixedBlockTypes { index: channel });
            }
        }
        Ok(())
    }
}

// -----------------------------------------------------------
// Timestamped numeric: one method per data type, generated by macro.
// -----------------------------------------------------------

macro_rules! impl_add_timestamped_numeric {
    ($method:ident, $rust_ty:ty, $variant:ident, $expected_dt:expr, $type_name:literal) => {
        impl WriterBuilder {
            #[doc = concat!(
                "Append `", $type_name, "` samples to a timestamped channel.\n\n",
                "# Errors\n\n",
                "- [`OsfError::ChannelNotFound`] when the channel does not exist.\n",
                "- [`OsfError::DataTypeMismatch`] when the channel's declared data type ",
                "is not the matching variant.\n",
                "- [`OsfError::ChannelMixedBlockTypes`] when the channel already holds ",
                "equidistant or variable data.\n",
                "- [`OsfError::InvalidBlock`] when timestamp and value slices have ",
                "different lengths."
            )]
            pub fn $method(
                &mut self,
                channel: u16,
                timestamps_ns: &[i64],
                values: &[$rust_ty],
            ) -> Result<(), OsfError> {
                self.append_timestamped_numeric(
                    channel,
                    $expected_dt,
                    timestamps_ns,
                    values.len(),
                    |storage| {
                        if let NumericValues::$variant(target) = storage {
                            target.extend_from_slice(values);
                        }
                    },
                    || NumericValues::$variant(values.to_vec()),
                )
            }
        }
    };
}

impl_add_timestamped_numeric!(
    add_timestamped_samples_bool,
    bool,
    Bool,
    DataType::Bool,
    "bool"
);
impl_add_timestamped_numeric!(
    add_timestamped_samples_i8,
    i8,
    Int8,
    DataType::Int8,
    "int8"
);
impl_add_timestamped_numeric!(
    add_timestamped_samples_i16,
    i16,
    Int16,
    DataType::Int16,
    "int16"
);
impl_add_timestamped_numeric!(
    add_timestamped_samples_i32,
    i32,
    Int32,
    DataType::Int32,
    "int32"
);
impl_add_timestamped_numeric!(
    add_timestamped_samples_i64,
    i64,
    Int64,
    DataType::Int64,
    "int64"
);
impl_add_timestamped_numeric!(
    add_timestamped_samples_u8,
    u8,
    UInt8,
    DataType::UInt8,
    "uint8"
);
impl_add_timestamped_numeric!(
    add_timestamped_samples_u16,
    u16,
    UInt16,
    DataType::UInt16,
    "uint16"
);
impl_add_timestamped_numeric!(
    add_timestamped_samples_u32,
    u32,
    UInt32,
    DataType::UInt32,
    "uint32"
);
impl_add_timestamped_numeric!(
    add_timestamped_samples_u64,
    u64,
    UInt64,
    DataType::UInt64,
    "uint64"
);
impl_add_timestamped_numeric!(
    add_timestamped_samples_f32,
    f32,
    Float,
    DataType::Float,
    "float"
);
impl_add_timestamped_numeric!(
    add_timestamped_samples_f64,
    f64,
    Double,
    DataType::Double,
    "double"
);
impl_add_timestamped_numeric!(
    add_timestamped_gps_samples,
    GpsLocation,
    GpsLocation,
    DataType::GpsLocation,
    "gpslocation"
);

// -----------------------------------------------------------
// Variable: string and binary, written one sample per block.
// -----------------------------------------------------------

impl WriterBuilder {
    /// Append timestamped `string` samples.
    ///
    /// # Errors
    ///
    /// - [`OsfError::ChannelNotFound`] when the channel does not exist.
    /// - [`OsfError::DataTypeMismatch`] when the channel was declared
    ///   with a non-`String` data type.
    /// - [`OsfError::ChannelMixedBlockTypes`] when the channel already
    ///   holds equidistant, timestamped-numeric, or binary data.
    /// - [`OsfError::InvalidBlock`] when timestamp and value slices
    ///   have different lengths.
    pub fn add_string_samples(
        &mut self,
        channel: u16,
        timestamps_ns: &[i64],
        values: &[String],
    ) -> Result<(), OsfError> {
        self.append_variable(
            channel,
            DataType::String,
            timestamps_ns,
            values.len(),
            VariableInsert::Strings(values),
        )
    }

    /// Append timestamped `binary` samples.
    ///
    /// # Errors
    ///
    /// As [`Self::add_string_samples`], but for the `Binary` variant.
    pub fn add_binary_samples(
        &mut self,
        channel: u16,
        timestamps_ns: &[i64],
        values: &[Vec<u8>],
    ) -> Result<(), OsfError> {
        self.append_variable(
            channel,
            DataType::Binary,
            timestamps_ns,
            values.len(),
            VariableInsert::Binaries(values),
        )
    }
}

enum VariableInsert<'a> {
    Strings(&'a [String]),
    Binaries(&'a [Vec<u8>]),
}

// -----------------------------------------------------------
// Internal helpers.
// -----------------------------------------------------------

impl WriterBuilder {
    /// Borrow the channel definition at `index` or return
    /// [`OsfError::ChannelNotFound`].
    fn channel_def(&self, index: u16) -> Result<&ChannelDef, OsfError> {
        self.channels
            .get(index as usize)
            .ok_or_else(|| OsfError::ChannelNotFound {
                name: format!("(index {index})"),
            })
    }

    fn append_timestamped_numeric(
        &mut self,
        channel: u16,
        expected: DataType,
        timestamps_ns: &[i64],
        values_len: usize,
        extend_values: impl FnOnce(&mut NumericValues),
        fresh_storage: impl FnOnce() -> NumericValues,
    ) -> Result<(), OsfError> {
        let def = self.channel_def(channel)?;
        if def.data_type != expected {
            return Err(OsfError::DataTypeMismatch {
                channel,
                expected: def.data_type.clone(),
                got: expected,
            });
        }
        if timestamps_ns.len() != values_len {
            return Err(OsfError::InvalidBlock(format!(
                "channel {channel}: timestamp len {} != values len {}",
                timestamps_ns.len(),
                values_len
            )));
        }

        let data = &mut self.channel_data[channel as usize];
        match data {
            ChannelData::Empty => {
                let storage = fresh_storage();
                *data = ChannelData::Timestamped {
                    timestamps_ns: timestamps_ns.to_vec(),
                    values: storage,
                };
            }
            ChannelData::Timestamped {
                timestamps_ns: ts,
                values: v,
            } => {
                ts.extend_from_slice(timestamps_ns);
                extend_values(v);
            }
            ChannelData::Equidistant { .. } | ChannelData::Variable { .. } => {
                return Err(OsfError::ChannelMixedBlockTypes { index: channel });
            }
        }
        Ok(())
    }

    fn append_variable(
        &mut self,
        channel: u16,
        expected: DataType,
        timestamps_ns: &[i64],
        values_len: usize,
        insert: VariableInsert<'_>,
    ) -> Result<(), OsfError> {
        let channel_dt = {
            let def = self.channel_def(channel)?;
            if def.data_type != expected {
                return Err(OsfError::DataTypeMismatch {
                    channel,
                    expected: def.data_type.clone(),
                    got: expected,
                });
            }
            def.data_type.clone()
        };
        if timestamps_ns.len() != values_len {
            return Err(OsfError::InvalidBlock(format!(
                "channel {channel}: timestamp len {} != values len {}",
                timestamps_ns.len(),
                values_len
            )));
        }

        let data = &mut self.channel_data[channel as usize];
        match data {
            ChannelData::Empty => {
                let (strings, binaries) = match &insert {
                    VariableInsert::Strings(v) => (Some(v.to_vec()), None),
                    VariableInsert::Binaries(v) => (None, Some(v.to_vec())),
                };
                *data = ChannelData::Variable {
                    timestamps_ns: timestamps_ns.to_vec(),
                    strings,
                    binaries,
                };
            }
            ChannelData::Variable {
                timestamps_ns: ts,
                strings,
                binaries,
            } => match (insert, strings, binaries) {
                (VariableInsert::Strings(v), Some(target), _) => {
                    ts.extend_from_slice(timestamps_ns);
                    target.extend_from_slice(v);
                }
                (VariableInsert::Binaries(v), _, Some(target)) => {
                    ts.extend_from_slice(timestamps_ns);
                    target.extend_from_slice(v);
                }
                _ => {
                    debug!("channel {channel}: variable storage variant mismatch on append");
                    return Err(OsfError::DataTypeMismatch {
                        channel,
                        expected: channel_dt,
                        got: expected,
                    });
                }
            },
            ChannelData::Equidistant { .. } | ChannelData::Timestamped { .. } => {
                return Err(OsfError::ChannelMixedBlockTypes { index: channel });
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dbl_channel(name: &str) -> ChannelDef {
        ChannelDef {
            name: name.into(),
            data_type: DataType::Double,
            ..Default::default()
        }
    }

    #[test]
    fn default_channel_def_has_size_2_and_scalar() {
        let d = ChannelDef::default();
        assert_eq!(d.size_of_length_value, 2);
        assert_eq!(d.channel_type, ChannelType::Scalar);
        assert_eq!(d.data_type, DataType::Double);
    }

    #[test]
    fn add_channel_returns_index_in_order() {
        let mut b = WriterBuilder::new();
        let i0 = b.add_channel(dbl_channel("a")).unwrap();
        let i1 = b.add_channel(dbl_channel("b")).unwrap();
        assert_eq!(i0, 0);
        assert_eq!(i1, 1);
        assert_eq!(b.channel_count(), 2);
        assert_eq!(b.channel_index("a"), Some(0));
        assert_eq!(b.channel_index("missing"), None);
    }

    #[test]
    fn add_channel_rejects_invalid_size_of_length_value() {
        let mut b = WriterBuilder::new();
        let mut def = dbl_channel("a");
        def.size_of_length_value = 3;
        let err = b.add_channel(def).unwrap_err();
        assert!(matches!(err, OsfError::InvalidMetablock(_)), "got {err:?}");
    }

    #[test]
    fn add_channel_rejects_unsupported_data_type() {
        let mut b = WriterBuilder::new();
        let def = ChannelDef {
            name: "a".into(),
            data_type: DataType::Unsupported("future".into()),
            ..Default::default()
        };
        let err = b.add_channel(def).unwrap_err();
        assert!(matches!(err, OsfError::InvalidMetablock(_)), "got {err:?}");
    }

    #[test]
    fn add_channel_rejects_bytearray() {
        let mut b = WriterBuilder::new();
        let def = ChannelDef {
            name: "a".into(),
            data_type: DataType::ByteArray,
            ..Default::default()
        };
        let err = b.add_channel(def).unwrap_err();
        assert!(matches!(err, OsfError::InvalidMetablock(_)), "got {err:?}");
    }

    #[test]
    fn add_equidistant_segment_appends_to_segment_list() {
        let mut b = WriterBuilder::new();
        let i = b.add_channel(dbl_channel("a")).unwrap();
        b.add_equidistant_segment_f64(i, 0, 1000.0, &[1.0, 2.0, 3.0])
            .unwrap();
        b.add_equidistant_segment_f64(i, 1_000_000_000, 1000.0, &[10.0, 20.0])
            .unwrap();
        match &b.channel_data[i as usize] {
            ChannelData::Equidistant { segments } => {
                assert_eq!(segments.len(), 2);
                assert_eq!(segments[0].start_timestamp_ns, 0);
                assert_eq!(segments[1].start_timestamp_ns, 1_000_000_000);
                assert_eq!(segments[0].values.len(), 3);
                assert_eq!(segments[1].values.len(), 2);
            }
            other => panic!("expected Equidistant, got {other:?}"),
        }
    }

    #[test]
    fn add_equidistant_rejects_non_f32_f64() {
        let mut b = WriterBuilder::new();
        let i = b
            .add_channel(ChannelDef {
                name: "a".into(),
                data_type: DataType::Int32,
                ..Default::default()
            })
            .unwrap();
        // No add_equidistant_segment_i32 exists; we exercise the
        // f64 path which fails the f32/f64 spec check by virtue of
        // the channel declaring Int32.
        let err = b
            .add_equidistant_segment_f64(i, 0, 1000.0, &[1.0])
            .unwrap_err();
        assert!(matches!(err, OsfError::InvalidBlock(_)), "got {err:?}");
    }

    #[test]
    fn add_equidistant_rejects_zero_or_negative_rate() {
        let mut b = WriterBuilder::new();
        let i = b.add_channel(dbl_channel("a")).unwrap();
        let err = b.add_equidistant_segment_f64(i, 0, 0.0, &[1.0]).unwrap_err();
        assert!(matches!(err, OsfError::InvalidBlock(_)), "got {err:?}");
        let err = b
            .add_equidistant_segment_f64(i, 0, -1.0, &[1.0])
            .unwrap_err();
        assert!(matches!(err, OsfError::InvalidBlock(_)), "got {err:?}");
    }

    #[test]
    fn add_equidistant_rejects_unknown_channel() {
        let mut b = WriterBuilder::new();
        let err = b
            .add_equidistant_segment_f64(99, 0, 1.0, &[1.0])
            .unwrap_err();
        assert!(matches!(err, OsfError::ChannelNotFound { .. }), "got {err:?}");
    }

    #[test]
    fn add_timestamped_appends_and_locks_storage_variant() {
        let mut b = WriterBuilder::new();
        let i = b
            .add_channel(ChannelDef {
                name: "a".into(),
                data_type: DataType::Int32,
                ..Default::default()
            })
            .unwrap();
        b.add_timestamped_samples_i32(i, &[1, 2], &[10, 20]).unwrap();
        b.add_timestamped_samples_i32(i, &[3], &[30]).unwrap();
        match &b.channel_data[i as usize] {
            ChannelData::Timestamped {
                timestamps_ns,
                values: NumericValues::Int32(v),
            } => {
                assert_eq!(timestamps_ns, &vec![1i64, 2, 3]);
                assert_eq!(v, &vec![10, 20, 30]);
            }
            other => panic!("expected Timestamped/Int32, got {other:?}"),
        }
    }

    #[test]
    fn switching_storage_variants_is_mixed_block_types_error() {
        let mut b = WriterBuilder::new();
        let i = b.add_channel(dbl_channel("a")).unwrap();
        b.add_equidistant_segment_f64(i, 0, 1.0, &[1.0]).unwrap();
        let err = b
            .add_timestamped_samples_f64(i, &[1], &[1.0])
            .unwrap_err();
        assert!(
            matches!(err, OsfError::ChannelMixedBlockTypes { index: 0 }),
            "got {err:?}"
        );
    }

    #[test]
    fn timestamp_value_length_mismatch_is_invalid_block() {
        let mut b = WriterBuilder::new();
        let i = b.add_channel(dbl_channel("a")).unwrap();
        let err = b
            .add_timestamped_samples_f64(i, &[1, 2, 3], &[1.0])
            .unwrap_err();
        assert!(matches!(err, OsfError::InvalidBlock(_)), "got {err:?}");
    }

    #[test]
    fn data_type_mismatch_is_caught() {
        let mut b = WriterBuilder::new();
        let i = b
            .add_channel(ChannelDef {
                name: "a".into(),
                data_type: DataType::Int32,
                ..Default::default()
            })
            .unwrap();
        // Channel says Int32 but we call the Double variant.
        let err = b
            .add_timestamped_samples_f64(i, &[1], &[1.0])
            .unwrap_err();
        assert!(matches!(err, OsfError::DataTypeMismatch { .. }), "got {err:?}");
    }

    #[test]
    fn empty_builder_write_returns_writer_empty() {
        let b = WriterBuilder::new();
        let mut buf: Vec<u8> = Vec::new();
        let err = b.write_to(&mut buf).unwrap_err();
        assert!(matches!(err, OsfError::WriterEmpty), "got {err:?}");
    }

    #[test]
    fn metablock_only_write_produces_valid_header_and_json() {
        let mut b = WriterBuilder::new().creator("test:1").tag("preview");
        let _ = b.add_channel(dbl_channel("Sensor/Temperature")).unwrap();
        let mut buf: Vec<u8> = Vec::new();
        b.write_to(&mut buf).unwrap();

        // Header line: "OSF5 <len>\n"
        let newline = buf.iter().position(|&b| b == b'\n').unwrap();
        let header_line = std::str::from_utf8(&buf[..newline]).unwrap();
        let parts: Vec<&str> = header_line.split(' ').collect();
        assert_eq!(parts[0], "OSF5");
        let metablock_len: usize = parts[1].parse().unwrap();
        assert_eq!(buf.len(), newline + 1 + metablock_len, "header + metablock");

        // JSON envelope shape.
        let metablock_bytes = &buf[newline + 1..];
        let v: serde_json::Value = serde_json::from_slice(metablock_bytes).unwrap();
        let osf = &v["osf"];
        assert_eq!(osf["format"], "osf5");
        assert_eq!(osf["version"], 5);
        assert_eq!(osf["file"]["creator"], "test:1");
        assert_eq!(osf["file"]["tag"], "preview");
        assert!(osf["file"]["created_utc"].is_string());

        let channels = osf["channels"].as_array().unwrap();
        assert_eq!(channels.len(), 1);
        assert_eq!(channels[0]["index"], 0);
        assert_eq!(channels[0]["name"], "Sensor/Temperature");
        assert_eq!(channels[0]["channeltype"], "scalar");
        assert_eq!(channels[0]["datatype"], "double");
        assert_eq!(channels[0]["sizeoflengthvalue"], 2);
    }

    #[test]
    fn metablock_omits_optional_fields_when_unset() {
        let mut b = WriterBuilder::new();
        let _ = b.add_channel(dbl_channel("a")).unwrap();
        let mut buf: Vec<u8> = Vec::new();
        b.write_to(&mut buf).unwrap();
        let newline = buf.iter().position(|&b| b == b'\n').unwrap();
        let metablock_bytes = &buf[newline + 1..];
        let v: serde_json::Value = serde_json::from_slice(metablock_bytes).unwrap();
        let file_obj = v["osf"]["file"].as_object().unwrap();
        // creator falls back to a default; tag falls back to "default";
        // optional fields are omitted (not null).
        assert_eq!(file_obj["tag"], "default");
        assert!(!file_obj.contains_key("reason"));
        assert!(!file_obj.contains_key("comment"));
        assert!(!file_obj.contains_key("created_at_latitude"));
    }

    #[test]
    fn format_unix_seconds_utc_matches_known_dates() {
        // 0 = 1970-01-01T00:00:00Z
        assert_eq!(super::format_unix_seconds_utc(0), "1970-01-01T00:00:00Z");
        // 1574200200 = 2019-11-19T21:50:00Z (78600 s past midnight UTC)
        assert_eq!(
            super::format_unix_seconds_utc(1_574_200_200),
            "2019-11-19T21:50:00Z"
        );
        // 951696000 = 2000-02-28T00:00:00Z (one day before leap-day)
        assert_eq!(
            super::format_unix_seconds_utc(951_696_000),
            "2000-02-28T00:00:00Z"
        );
        // 951782400 = 2000-02-29T00:00:00Z (the leap day)
        assert_eq!(
            super::format_unix_seconds_utc(951_782_400),
            "2000-02-29T00:00:00Z"
        );
    }

    fn write_to_vec(b: WriterBuilder) -> Vec<u8> {
        let mut buf: Vec<u8> = Vec::new();
        b.write_to(&mut buf).unwrap();
        buf
    }

    fn read_back(bytes: &[u8]) -> crate::DataManager {
        crate::DataManager::load_from_reader(std::io::Cursor::new(bytes.to_vec())).unwrap()
    }

    #[test]
    fn equidistant_double_block_round_trips_through_reader() {
        let mut b = WriterBuilder::new().creator("test:1");
        let i = b.add_channel(dbl_channel("eq")).unwrap();
        let samples: Vec<f64> = (0..50).map(|x| x as f64 * 0.5).collect();
        b.add_equidistant_segment_f64(i, 1_000_000_000, 1000.0, &samples)
            .unwrap();

        let bytes = write_to_vec(b);
        let mgr = read_back(&bytes);

        assert_eq!(mgr.channels().len(), 1);
        let chan = mgr.channel("eq").unwrap();
        let crate::Channel::Equidistant(eq) = chan else {
            panic!("expected Equidistant");
        };
        assert_eq!(eq.segments().len(), 1);
        assert_eq!(eq.segments()[0].start_timestamp_ns, 1_000_000_000);
        assert!((eq.segments()[0].sample_rate_hz - 1000.0).abs() < 1e-9);
        assert_eq!(eq.as_doubles_flat().unwrap(), samples);
    }

    #[test]
    fn equidistant_two_segments_round_trip() {
        let mut b = WriterBuilder::new();
        let i = b.add_channel(dbl_channel("eq")).unwrap();
        b.add_equidistant_segment_f64(i, 0, 100.0, &[1.0, 2.0, 3.0])
            .unwrap();
        b.add_equidistant_segment_f64(i, 1_000_000_000, 200.0, &[4.0, 5.0])
            .unwrap();

        let bytes = write_to_vec(b);
        let mgr = read_back(&bytes);
        let crate::Channel::Equidistant(eq) = mgr.channel("eq").unwrap() else {
            panic!("expected Equidistant");
        };
        assert_eq!(eq.segments().len(), 2);
        assert_eq!(eq.segments()[1].start_timestamp_ns, 1_000_000_000);
        assert_eq!(eq.as_doubles_flat().unwrap(), vec![1.0, 2.0, 3.0, 4.0, 5.0]);
    }

    #[test]
    fn equidistant_block_splits_when_size_of_length_value_is_2() {
        // 100k samples × 8 bytes = 800k > 65535. With size=2 the writer
        // must split into multiple blocks.
        let mut b = WriterBuilder::new();
        let i = b.add_channel(dbl_channel("eq")).unwrap();
        let samples: Vec<f64> = (0..100_000).map(|x| x as f64).collect();
        b.add_equidistant_segment_f64(i, 0, 1000.0, &samples).unwrap();

        let bytes = write_to_vec(b);
        let mgr = read_back(&bytes);
        let crate::Channel::Equidistant(eq) = mgr.channel("eq").unwrap() else {
            panic!("expected Equidistant");
        };
        // The split is internal; the manager joins everything back
        // into a single segment because all blocks belong to the
        // same StartData group.
        assert_eq!(eq.segments().len(), 1);
        assert_eq!(eq.as_doubles_flat().unwrap(), samples);
    }

    #[test]
    fn timestamped_int32_round_trips() {
        let mut b = WriterBuilder::new();
        let i = b
            .add_channel(ChannelDef {
                name: "ts".into(),
                data_type: DataType::Int32,
                ..Default::default()
            })
            .unwrap();
        let timestamps: Vec<i64> = (0..200).map(|x| 1000 + x * 10).collect();
        let values: Vec<i32> = (0..200).collect();
        b.add_timestamped_samples_i32(i, &timestamps, &values).unwrap();

        let bytes = write_to_vec(b);
        let mgr = read_back(&bytes);
        let crate::Channel::Timestamped(ts) = mgr.channel("ts").unwrap() else {
            panic!("expected Timestamped");
        };
        let pairs = ts.as_int32_flat().unwrap();
        assert_eq!(pairs.len(), 200);
        for (i, (t, v)) in pairs.iter().enumerate() {
            assert_eq!(*t, 1000 + (i as i64) * 10);
            assert_eq!(*v, i as i32);
        }
    }

    #[test]
    fn variable_string_round_trips() {
        let mut b = WriterBuilder::new();
        let i = b
            .add_channel(ChannelDef {
                name: "msg".into(),
                data_type: DataType::String,
                size_of_length_value: 4,
                ..Default::default()
            })
            .unwrap();
        b.add_string_samples(
            i,
            &[100, 200, 300],
            &["alpha".into(), "beta".into(), "gamma".into()],
        )
        .unwrap();

        let bytes = write_to_vec(b);
        let mgr = read_back(&bytes);
        let crate::Channel::Variable(var) = mgr.channel("msg").unwrap() else {
            panic!("expected Variable");
        };
        assert_eq!(var.timestamps_ns(), &[100, 200, 300]);
        assert_eq!(
            var.as_strings().unwrap(),
            &["alpha".to_string(), "beta".into(), "gamma".into()]
        );
    }

    #[test]
    fn variable_binary_with_huge_sample_auto_bumps_size_of_length_value() {
        // Default size_of_length_value=2, but a 70 KB sample forces a
        // bump to 4. The metablock should reflect the bumped value.
        let mut b = WriterBuilder::new();
        let i = b
            .add_channel(ChannelDef {
                name: "img".into(),
                data_type: DataType::Binary,
                ..Default::default() // size_of_length_value = 2
            })
            .unwrap();
        let big = vec![0xAAu8; 70_000];
        b.add_binary_samples(i, &[42], std::slice::from_ref(&big)).unwrap();

        let bytes = write_to_vec(b);
        // Quick check: the metablock must declare size 4 for the bumped channel.
        let newline = bytes.iter().position(|&x| x == b'\n').unwrap();
        let metablock_bytes = &bytes[newline + 1..];
        // Metablock continues until first non-JSON byte; we just look
        // for the channel record.
        let head_str = String::from_utf8_lossy(&metablock_bytes[..600.min(metablock_bytes.len())]);
        assert!(
            head_str.contains("\"sizeoflengthvalue\": 4"),
            "auto-bump failed; metablock head: {head_str}"
        );

        let mgr = read_back(&bytes);
        let crate::Channel::Variable(var) = mgr.channel("img").unwrap() else {
            panic!("expected Variable");
        };
        assert_eq!(var.as_binaries().unwrap(), &[big]);
    }

    #[test]
    fn channel_def_serialises_optional_fields() {
        let def = ChannelDef {
            name: "Sensor/Pressure".into(),
            data_type: DataType::Float,
            channel_type: ChannelType::Scalar,
            size_of_length_value: 4,
            physical_unit: Some("bar".into()),
            mime_type: Some("application/octet-stream".into()),
            time_increment_ns: Some(1_000_000),
            ..Default::default()
        };
        let v = super::channel_def_to_json(7, &def);
        assert_eq!(v["index"], 7);
        assert_eq!(v["datatype"], "float");
        assert_eq!(v["sizeoflengthvalue"], 4);
        assert_eq!(v["physicalunit"], "bar");
        assert_eq!(v["mimetype"], "application/octet-stream");
        assert_eq!(v["timeincrement"], 1_000_000);
    }

    #[test]
    fn add_string_samples_locks_string_storage() {
        let mut b = WriterBuilder::new();
        let i = b
            .add_channel(ChannelDef {
                name: "msg".into(),
                data_type: DataType::String,
                size_of_length_value: 4,
                ..Default::default()
            })
            .unwrap();
        b.add_string_samples(i, &[100, 200], &["hi".into(), "bye".into()])
            .unwrap();
        match &b.channel_data[i as usize] {
            ChannelData::Variable {
                timestamps_ns,
                strings: Some(s),
                binaries: None,
            } => {
                assert_eq!(timestamps_ns, &vec![100i64, 200]);
                assert_eq!(s, &vec!["hi".to_string(), "bye".into()]);
            }
            other => panic!("expected Variable/String, got {other:?}"),
        }
        // A subsequent binary call on the same channel must fail.
        let err = b.add_binary_samples(i, &[300], &[vec![1, 2]]).unwrap_err();
        assert!(matches!(err, OsfError::DataTypeMismatch { .. }), "got {err:?}");
    }
}
