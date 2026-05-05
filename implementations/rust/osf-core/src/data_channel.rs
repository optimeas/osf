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

//! Typed in-memory channel model.
//!
//! Where [`crate::block::Block`] is the per-block raw view from the
//! [`crate::reader::BlockReader`], this module is the per-channel
//! aggregated view: equidistant samples grouped by segment, timestamped
//! samples in parallel timestamp + value vectors, and string / binary
//! samples in their own variant. The Manager layer
//! ([`crate::manager::DataManager`]) builds these structs from a block
//! stream so applications can ignore the on-disk block boundaries.
//!
//! Three variants because the storage layouts genuinely differ:
//!
//! | Variant            | Storage                                          |
//! |--------------------|--------------------------------------------------|
//! | `Equidistant`      | flat `NumericValues` + `Vec<Segment>` indices    |
//! | `Timestamped`      | parallel `Vec<i64>` + `NumericValues`            |
//! | `Variable`         | `Vec<i64>` + `Vec<String>` xor `Vec<Vec<u8>>`    |
//!
//! Equidistant channels reconstruct timestamps lazily during iteration
//! using the per-segment `(start_timestamp_ns, sample_rate_hz)` —
//! reproducing the layout of `OSF.Data.Channels` from the Delphi
//! reference, where every `bcStartData` opens a new segment.

use crate::block::GpsLocation;
use crate::meta::SpectrumType;
use crate::types::{ChannelType, DataType};

/// Top-level enum over the three channel storage layouts.
#[derive(Debug, Clone, PartialEq)]
pub enum Channel {
    /// Equidistant numeric channel — samples + segments.
    Equidistant(EquidistantChannel),
    /// Timestamped numeric channel — parallel timestamp + value vectors.
    Timestamped(TimestampedChannel),
    /// Timestamped string or binary channel — variable-length values.
    Variable(VariableChannel),
}

/// Equidistant numeric channel. `bcStartData` opens a new segment;
/// `bcContinuedData` extends the most recent one.
#[derive(Debug, Clone, PartialEq)]
pub struct EquidistantChannel {
    /// Channel index from the metablock.
    pub index: u16,
    /// Fully qualified channel name.
    pub name: String,
    /// Datatype of the samples — every variant of [`NumericValues`]
    /// except `GpsLocation` is allowed (per spec, equidistant blocks
    /// never carry GPS data, but the type is shared with timestamped
    /// channels).
    pub data_type: DataType,
    /// Optional physical unit string (e.g. `°C`).
    pub physical_unit: Option<String>,
    /// Optional display name.
    pub display_name: Option<String>,
    /// Secondary channel-definition fields preserved from the
    /// metablock.
    pub channel_def: ChannelMeta,

    /// Flat sample storage: every segment's samples appended head-to-tail.
    pub(crate) samples: NumericValues,
    /// One entry per `bcStartData`; `start_index..start_index+sample_count`
    /// indexes into [`Self::samples`].
    pub(crate) segments: Vec<Segment>,
}

/// Timestamped numeric channel. Every sample carries an absolute
/// timestamp; `bcAbsTimeStampData` blocks append directly,
/// `bcContinuedRelStampData` deltas are converted to absolute on read.
#[derive(Debug, Clone, PartialEq)]
pub struct TimestampedChannel {
    /// Channel index from the metablock.
    pub index: u16,
    /// Fully qualified channel name.
    pub name: String,
    /// Datatype of the samples.
    pub data_type: DataType,
    /// Optional physical unit string.
    pub physical_unit: Option<String>,
    /// Optional display name.
    pub display_name: Option<String>,
    /// Secondary channel-definition fields preserved from the
    /// metablock.
    pub channel_def: ChannelMeta,

    /// Absolute timestamps in nanoseconds since the Unix epoch (UTC),
    /// in stream order — same length as [`Self::values`].
    pub(crate) timestamps_ns: Vec<i64>,
    /// Sample values, parallel to [`Self::timestamps_ns`].
    pub(crate) values: NumericValues,
}

/// Timestamped channel for string and binary data. Variable-length
/// payloads land in either `string_values` (when `data_type` is
/// `String`) or `binary_values` (when `data_type` is `Binary`); the
/// other field stays `None`.
#[derive(Debug, Clone, PartialEq)]
pub struct VariableChannel {
    /// Channel index from the metablock.
    pub index: u16,
    /// Fully qualified channel name.
    pub name: String,
    /// Datatype — exactly one of [`DataType::String`] or
    /// [`DataType::Binary`].
    pub data_type: DataType,
    /// Optional physical unit string (rare for string / binary).
    pub physical_unit: Option<String>,
    /// Optional display name.
    pub display_name: Option<String>,
    /// MIME type for binary channels (e.g. `image/jpeg`).
    pub mime_type: Option<String>,
    /// Secondary channel-definition fields preserved from the
    /// metablock.
    pub channel_def: ChannelMeta,

    /// Absolute timestamps in nanoseconds since the Unix epoch (UTC).
    pub(crate) timestamps_ns: Vec<i64>,
    /// String samples; `Some` for `data_type == String`, `None`
    /// otherwise.
    pub(crate) string_values: Option<Vec<String>>,
    /// Binary samples; `Some` for `data_type == Binary`, `None`
    /// otherwise.
    pub(crate) binary_values: Option<Vec<Vec<u8>>>,
}

/// One equidistant segment within an [`EquidistantChannel`]. Spec rev
/// 2026-05-04 makes multiple segments per channel explicit — every
/// `bcStartData` block opens a new one with its own absolute start
/// time and sample rate.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Segment {
    /// Absolute start timestamp of this segment in nanoseconds.
    pub start_timestamp_ns: i64,
    /// Sample rate in Hz, valid until the next segment of this
    /// channel.
    pub sample_rate_hz: f64,
    /// First sample of this segment in the channel's flat
    /// `NumericValues` vector.
    pub start_index: usize,
    /// Number of samples belonging to this segment.
    pub sample_count: usize,
}

/// Secondary channel-definition fields preserved from the metablock so
/// downstream code can introspect them without keeping a back-reference
/// to the [`crate::MetaBlock`].
#[derive(Debug, Clone, Default, PartialEq)]
pub struct ChannelMeta {
    /// Original on-disk channel-type spelling (`scalar`, etc.).
    pub channel_type: ChannelType,
    /// Length-prefix width on disk (2 or 4 bytes per spec).
    pub size_of_length_value: u8,
    /// Sample period in nanoseconds, if the metablock declared one.
    pub time_increment_ns: Option<i64>,
    /// Free-form reference identifier.
    pub reference: Option<String>,
    /// Physical dimension (e.g. `temperature`).
    pub physical_dimension: Option<String>,
    /// Free-form comment.
    pub comment: Option<String>,
    /// Spectrum subtype, if the channel carries spectral data.
    pub spectrum_type: Option<SpectrumType>,
}

/// Numeric (or `gpslocation`) sample storage for a single channel.
/// One variant per supported numeric data type plus `GpsLocation`,
/// each holding a flat `Vec`. `Equidistant` channels never carry the
/// `GpsLocation` variant per spec, but the type is shared with
/// timestamped channels which do.
#[derive(Debug, Clone, PartialEq)]
pub enum NumericValues {
    /// `bool` samples.
    Bool(Vec<bool>),
    /// Signed 8-bit integer samples.
    Int8(Vec<i8>),
    /// Signed 16-bit integer samples.
    Int16(Vec<i16>),
    /// Signed 32-bit integer samples.
    Int32(Vec<i32>),
    /// Signed 64-bit integer samples.
    Int64(Vec<i64>),
    /// Unsigned 8-bit integer samples.
    UInt8(Vec<u8>),
    /// Unsigned 16-bit integer samples.
    UInt16(Vec<u16>),
    /// Unsigned 32-bit integer samples.
    UInt32(Vec<u32>),
    /// Unsigned 64-bit integer samples.
    UInt64(Vec<u64>),
    /// IEEE-754 single-precision float samples.
    Float(Vec<f32>),
    /// IEEE-754 double-precision float samples.
    Double(Vec<f64>),
    /// 24-byte GPS-location samples — only in timestamped channels.
    GpsLocation(Vec<GpsLocation>),
}

impl NumericValues {
    /// Number of samples held.
    #[must_use]
    pub fn len(&self) -> usize {
        match self {
            Self::Bool(v) => v.len(),
            Self::Int8(v) => v.len(),
            Self::Int16(v) => v.len(),
            Self::Int32(v) => v.len(),
            Self::Int64(v) => v.len(),
            Self::UInt8(v) => v.len(),
            Self::UInt16(v) => v.len(),
            Self::UInt32(v) => v.len(),
            Self::UInt64(v) => v.len(),
            Self::Float(v) => v.len(),
            Self::Double(v) => v.len(),
            Self::GpsLocation(v) => v.len(),
        }
    }

    /// True when no samples are held.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// The [`DataType`] this storage holds.
    #[must_use]
    pub fn data_type(&self) -> DataType {
        match self {
            Self::Bool(_) => DataType::Bool,
            Self::Int8(_) => DataType::Int8,
            Self::Int16(_) => DataType::Int16,
            Self::Int32(_) => DataType::Int32,
            Self::Int64(_) => DataType::Int64,
            Self::UInt8(_) => DataType::UInt8,
            Self::UInt16(_) => DataType::UInt16,
            Self::UInt32(_) => DataType::UInt32,
            Self::UInt64(_) => DataType::UInt64,
            Self::Float(_) => DataType::Float,
            Self::Double(_) => DataType::Double,
            Self::GpsLocation(_) => DataType::GpsLocation,
        }
    }

    /// Build an empty storage matching the given data type. Returns
    /// `None` for variable-length types (`String`, `Binary`,
    /// `ByteArray`) or any `Unsupported`.
    #[must_use]
    pub fn empty_for(dt: &DataType) -> Option<Self> {
        Some(match dt {
            DataType::Bool => Self::Bool(Vec::new()),
            DataType::Int8 => Self::Int8(Vec::new()),
            DataType::Int16 => Self::Int16(Vec::new()),
            DataType::Int32 => Self::Int32(Vec::new()),
            DataType::Int64 => Self::Int64(Vec::new()),
            DataType::UInt8 => Self::UInt8(Vec::new()),
            DataType::UInt16 => Self::UInt16(Vec::new()),
            DataType::UInt32 => Self::UInt32(Vec::new()),
            DataType::UInt64 => Self::UInt64(Vec::new()),
            DataType::Float => Self::Float(Vec::new()),
            DataType::Double => Self::Double(Vec::new()),
            DataType::GpsLocation => Self::GpsLocation(Vec::new()),
            DataType::String
            | DataType::Binary
            | DataType::ByteArray
            | DataType::Unsupported(_) => return None,
        })
    }
}

// -----------------------------------------------------------
// Common accessors on Channel.
// -----------------------------------------------------------

impl Channel {
    /// Channel index from the metablock.
    #[must_use]
    pub fn index(&self) -> u16 {
        match self {
            Self::Equidistant(c) => c.index,
            Self::Timestamped(c) => c.index,
            Self::Variable(c) => c.index,
        }
    }

    /// Channel name.
    #[must_use]
    pub fn name(&self) -> &str {
        match self {
            Self::Equidistant(c) => &c.name,
            Self::Timestamped(c) => &c.name,
            Self::Variable(c) => &c.name,
        }
    }

    /// Datatype of the samples in this channel.
    #[must_use]
    pub fn data_type(&self) -> DataType {
        match self {
            Self::Equidistant(c) => c.data_type.clone(),
            Self::Timestamped(c) => c.data_type.clone(),
            Self::Variable(c) => c.data_type.clone(),
        }
    }

    /// Optional physical-unit string.
    #[must_use]
    pub fn physical_unit(&self) -> Option<&str> {
        match self {
            Self::Equidistant(c) => c.physical_unit.as_deref(),
            Self::Timestamped(c) => c.physical_unit.as_deref(),
            Self::Variable(c) => c.physical_unit.as_deref(),
        }
    }

    /// Optional display name.
    #[must_use]
    pub fn display_name(&self) -> Option<&str> {
        match self {
            Self::Equidistant(c) => c.display_name.as_deref(),
            Self::Timestamped(c) => c.display_name.as_deref(),
            Self::Variable(c) => c.display_name.as_deref(),
        }
    }

    /// Total number of samples in this channel (sum across all
    /// segments for equidistant channels).
    #[must_use]
    pub fn sample_count(&self) -> usize {
        match self {
            Self::Equidistant(c) => c.samples.len(),
            Self::Timestamped(c) => c.values.len(),
            Self::Variable(c) => c
                .string_values
                .as_ref()
                .map(Vec::len)
                .or_else(|| c.binary_values.as_ref().map(Vec::len))
                .unwrap_or(0),
        }
    }

    /// True when [`Self::sample_count`] is zero.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.sample_count() == 0
    }

    /// Common channel-definition metadata.
    #[must_use]
    pub fn channel_def(&self) -> &ChannelMeta {
        match self {
            Self::Equidistant(c) => &c.channel_def,
            Self::Timestamped(c) => &c.channel_def,
            Self::Variable(c) => &c.channel_def,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn numeric_values_data_type_is_consistent() {
        for (vals, expected) in [
            (NumericValues::Int32(vec![1, 2]), DataType::Int32),
            (NumericValues::Double(vec![1.0]), DataType::Double),
            (NumericValues::GpsLocation(Vec::new()), DataType::GpsLocation),
        ] {
            assert_eq!(vals.data_type(), expected);
        }
    }

    #[test]
    fn empty_for_returns_none_for_variable_and_unsupported() {
        assert!(NumericValues::empty_for(&DataType::String).is_none());
        assert!(NumericValues::empty_for(&DataType::Binary).is_none());
        assert!(NumericValues::empty_for(&DataType::ByteArray).is_none());
        assert!(
            NumericValues::empty_for(&DataType::Unsupported("future".into())).is_none()
        );
        assert!(NumericValues::empty_for(&DataType::Double).is_some());
    }

    fn make_eq_channel(samples: NumericValues, segments: Vec<Segment>) -> EquidistantChannel {
        EquidistantChannel {
            index: 0,
            name: "test".into(),
            data_type: samples.data_type(),
            physical_unit: None,
            display_name: None,
            channel_def: ChannelMeta::default(),
            samples,
            segments,
        }
    }

    #[test]
    fn channel_common_accessors_work_for_each_variant() {
        let eq = make_eq_channel(
            NumericValues::Double(vec![1.0, 2.0, 3.0]),
            vec![Segment {
                start_timestamp_ns: 0,
                sample_rate_hz: 1.0,
                start_index: 0,
                sample_count: 3,
            }],
        );
        let chan = Channel::Equidistant(eq);
        assert_eq!(chan.index(), 0);
        assert_eq!(chan.name(), "test");
        assert_eq!(chan.data_type(), DataType::Double);
        assert_eq!(chan.sample_count(), 3);
        assert!(!chan.is_empty());

        let ts = TimestampedChannel {
            index: 1,
            name: "ts".into(),
            data_type: DataType::Int32,
            physical_unit: Some("V".into()),
            display_name: None,
            channel_def: ChannelMeta::default(),
            timestamps_ns: vec![10, 20],
            values: NumericValues::Int32(vec![1, 2]),
        };
        let chan = Channel::Timestamped(ts);
        assert_eq!(chan.physical_unit(), Some("V"));
        assert_eq!(chan.sample_count(), 2);

        let var = VariableChannel {
            index: 2,
            name: "msg".into(),
            data_type: DataType::String,
            physical_unit: None,
            display_name: None,
            mime_type: None,
            channel_def: ChannelMeta::default(),
            timestamps_ns: vec![100],
            string_values: Some(vec!["hello".into()]),
            binary_values: None,
        };
        let chan = Channel::Variable(var);
        assert_eq!(chan.sample_count(), 1);
        assert_eq!(chan.data_type(), DataType::String);
    }
}
