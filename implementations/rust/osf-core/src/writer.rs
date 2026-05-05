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

use crate::block::GpsLocation;
use crate::data_channel::NumericValues;
use crate::error::OsfError;
use crate::meta::SpectrumType;
use crate::types::{ChannelType, DataType};
use log::debug;
use std::collections::HashMap;

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
/// Fields are consumed by the block-writing phase that lands in a
/// follow-up commit; the lint allow is removed there.
#[allow(dead_code)]
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
