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
use crate::error::OsfError;
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

/// One sample yielded by `samples_with_time`. Generic over the value
/// representation (`NumericValueRef<'_>` for numeric / GPS channels,
/// `VariableValueRef<'_>` for string / binary channels).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Sample<T> {
    /// Absolute timestamp in nanoseconds since the Unix epoch (UTC).
    pub timestamp_ns: i64,
    /// Sample value.
    pub value: T,
}

/// Borrowed view of one numeric (or GPS) sample. Copy-sized scalars
/// are passed by value; the 24-byte [`GpsLocation`] is borrowed to
/// avoid copying it on every iteration step.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum NumericValueRef<'a> {
    /// `bool` sample.
    Bool(bool),
    /// Signed 8-bit integer.
    Int8(i8),
    /// Signed 16-bit integer.
    Int16(i16),
    /// Signed 32-bit integer.
    Int32(i32),
    /// Signed 64-bit integer.
    Int64(i64),
    /// Unsigned 8-bit integer.
    UInt8(u8),
    /// Unsigned 16-bit integer.
    UInt16(u16),
    /// Unsigned 32-bit integer.
    UInt32(u32),
    /// Unsigned 64-bit integer.
    UInt64(u64),
    /// IEEE-754 single-precision float.
    Float(f32),
    /// IEEE-754 double-precision float.
    Double(f64),
    /// 24-byte GPS-location struct, borrowed from the channel.
    GpsLocation(&'a GpsLocation),
}

/// Borrowed view of one string-or-binary sample.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum VariableValueRef<'a> {
    /// UTF-8 string sample.
    String(&'a str),
    /// Opaque byte payload.
    Binary(&'a [u8]),
}

/// Project one element of a [`NumericValues`] storage at the given
/// index into a [`NumericValueRef`]. Lifetime is elided to the input
/// reference so the result can outlive the immediate call site.
fn numeric_value_ref_at(samples: &NumericValues, idx: usize) -> NumericValueRef<'_> {
    match samples {
        NumericValues::Bool(v) => NumericValueRef::Bool(v[idx]),
        NumericValues::Int8(v) => NumericValueRef::Int8(v[idx]),
        NumericValues::Int16(v) => NumericValueRef::Int16(v[idx]),
        NumericValues::Int32(v) => NumericValueRef::Int32(v[idx]),
        NumericValues::Int64(v) => NumericValueRef::Int64(v[idx]),
        NumericValues::UInt8(v) => NumericValueRef::UInt8(v[idx]),
        NumericValues::UInt16(v) => NumericValueRef::UInt16(v[idx]),
        NumericValues::UInt32(v) => NumericValueRef::UInt32(v[idx]),
        NumericValues::UInt64(v) => NumericValueRef::UInt64(v[idx]),
        NumericValues::Float(v) => NumericValueRef::Float(v[idx]),
        NumericValues::Double(v) => NumericValueRef::Double(v[idx]),
        NumericValues::GpsLocation(v) => NumericValueRef::GpsLocation(&v[idx]),
    }
}

/// Compute the timestamp of sample `i` within a segment given its
/// start time and sample rate. Returns the segment start when the
/// rate is non-positive (defensive — would only happen on a
/// malformed file).
fn segment_timestamp(seg: &Segment, i: usize) -> i64 {
    if seg.sample_rate_hz > 0.0 && i > 0 {
        let offset = ((i as f64) * 1.0e9 / seg.sample_rate_hz) as i64;
        seg.start_timestamp_ns.saturating_add(offset)
    } else {
        seg.start_timestamp_ns
    }
}

// -----------------------------------------------------------
// EquidistantChannel: segments + flat-access + iteration.
// -----------------------------------------------------------

impl EquidistantChannel {
    /// Read-only view of this channel's segments.
    #[must_use]
    pub fn segments(&self) -> &[Segment] {
        &self.segments
    }

    /// Read-only view of the flat sample storage (segments stitched
    /// head-to-tail).
    #[must_use]
    pub fn values(&self) -> &NumericValues {
        &self.samples
    }

    /// Iterate over every sample with a reconstructed absolute
    /// timestamp.
    ///
    /// Within a segment, sample `i` lands at `segment.start +
    /// i * (1e9 / sample_rate_hz)`. Time gaps **between** consecutive
    /// segments are not interpolated — every segment starts at its
    /// own `start_timestamp_ns`.
    pub fn samples_with_time(&self) -> impl Iterator<Item = Sample<NumericValueRef<'_>>> + '_ {
        let samples = &self.samples;
        self.segments.iter().flat_map(move |seg| {
            (0..seg.sample_count).map(move |i| Sample {
                timestamp_ns: segment_timestamp(seg, i),
                value: numeric_value_ref_at(samples, seg.start_index + i),
            })
        })
    }
}

// -----------------------------------------------------------
// TimestampedChannel: parallel timestamp + value vectors.
// -----------------------------------------------------------

impl TimestampedChannel {
    /// Read-only view of the absolute timestamps.
    #[must_use]
    pub fn timestamps_ns(&self) -> &[i64] {
        &self.timestamps_ns
    }

    /// Read-only view of the parallel value storage.
    #[must_use]
    pub fn values(&self) -> &NumericValues {
        &self.values
    }

    /// Iterate over every sample as a `(timestamp, value)` pair.
    pub fn samples_with_time(&self) -> impl Iterator<Item = Sample<NumericValueRef<'_>>> + '_ {
        let values = &self.values;
        self.timestamps_ns
            .iter()
            .enumerate()
            .map(move |(i, &ts)| Sample {
                timestamp_ns: ts,
                value: numeric_value_ref_at(values, i),
            })
    }
}

// -----------------------------------------------------------
// VariableChannel: parallel timestamp + string/binary vectors.
// -----------------------------------------------------------

impl VariableChannel {
    /// Read-only view of the absolute timestamps.
    #[must_use]
    pub fn timestamps_ns(&self) -> &[i64] {
        &self.timestamps_ns
    }

    /// Borrow the channel's string samples. Returns
    /// [`OsfError::DataTypeAccessMismatch`] if the channel holds
    /// binary data instead.
    ///
    /// # Errors
    ///
    /// `DataTypeAccessMismatch` when this is not a string channel.
    pub fn as_strings(&self) -> Result<&[String], OsfError> {
        match &self.string_values {
            Some(v) => Ok(v),
            None => Err(OsfError::DataTypeAccessMismatch {
                channel: self.index,
                requested: DataType::String,
                actual: self.data_type.clone(),
            }),
        }
    }

    /// Borrow the channel's binary samples. Returns
    /// [`OsfError::DataTypeAccessMismatch`] if the channel holds
    /// string data instead.
    ///
    /// # Errors
    ///
    /// `DataTypeAccessMismatch` when this is not a binary channel.
    pub fn as_binaries(&self) -> Result<&[Vec<u8>], OsfError> {
        match &self.binary_values {
            Some(v) => Ok(v),
            None => Err(OsfError::DataTypeAccessMismatch {
                channel: self.index,
                requested: DataType::Binary,
                actual: self.data_type.clone(),
            }),
        }
    }

    /// Iterate over every sample as a `(timestamp, value)` pair where
    /// `value` borrows either a `&str` or a `&[u8]` depending on the
    /// channel's data type.
    pub fn samples_with_time(
        &self,
    ) -> impl Iterator<Item = Sample<VariableValueRef<'_>>> + '_ {
        let strings = self.string_values.as_deref();
        let binaries = self.binary_values.as_deref();
        self.timestamps_ns
            .iter()
            .enumerate()
            .map(move |(i, &ts)| {
                let value = if let Some(s) = strings {
                    VariableValueRef::String(s[i].as_str())
                } else if let Some(b) = binaries {
                    VariableValueRef::Binary(b[i].as_slice())
                } else {
                    // Constructor invariant: exactly one of the two is
                    // Some. If neither is, the channel was built
                    // incorrectly.
                    unreachable!("VariableChannel must have either string_values or binary_values")
                };
                Sample {
                    timestamp_ns: ts,
                    value,
                }
            })
    }
}

// -----------------------------------------------------------
// Flat-access helpers.
// -----------------------------------------------------------

macro_rules! impl_numeric_flat_access {
    ($variant:ident, $rust_ty:ty, $method:ident, $dt:expr) => {
        impl EquidistantChannel {
            #[doc = concat!("Clone the flat sample vector when this channel holds `",
                            stringify!($variant), "` samples.\n\n# Errors\n\n",
                            "[`OsfError::DataTypeAccessMismatch`] when the stored datatype \
                             does not match.")]
            pub fn $method(&self) -> Result<Vec<$rust_ty>, OsfError> {
                match &self.samples {
                    NumericValues::$variant(v) => Ok(v.clone()),
                    other => Err(OsfError::DataTypeAccessMismatch {
                        channel: self.index,
                        requested: $dt,
                        actual: other.data_type(),
                    }),
                }
            }
        }
        impl TimestampedChannel {
            #[doc = concat!("Pair every timestamp with its `",
                            stringify!($variant), "` value.\n\n# Errors\n\n",
                            "[`OsfError::DataTypeAccessMismatch`] when the stored datatype \
                             does not match.")]
            pub fn $method(&self) -> Result<Vec<(i64, $rust_ty)>, OsfError> {
                match &self.values {
                    NumericValues::$variant(v) => Ok(self
                        .timestamps_ns
                        .iter()
                        .copied()
                        .zip(v.iter().copied())
                        .collect()),
                    other => Err(OsfError::DataTypeAccessMismatch {
                        channel: self.index,
                        requested: $dt,
                        actual: other.data_type(),
                    }),
                }
            }
        }
    };
}

impl_numeric_flat_access!(Bool, bool, as_bools_flat, DataType::Bool);
impl_numeric_flat_access!(Int8, i8, as_int8_flat, DataType::Int8);
impl_numeric_flat_access!(Int16, i16, as_int16_flat, DataType::Int16);
impl_numeric_flat_access!(Int32, i32, as_int32_flat, DataType::Int32);
impl_numeric_flat_access!(Int64, i64, as_int64_flat, DataType::Int64);
impl_numeric_flat_access!(UInt8, u8, as_uint8_flat, DataType::UInt8);
impl_numeric_flat_access!(UInt16, u16, as_uint16_flat, DataType::UInt16);
impl_numeric_flat_access!(UInt32, u32, as_uint32_flat, DataType::UInt32);
impl_numeric_flat_access!(UInt64, u64, as_uint64_flat, DataType::UInt64);
impl_numeric_flat_access!(Float, f32, as_floats_flat, DataType::Float);
impl_numeric_flat_access!(Double, f64, as_doubles_flat, DataType::Double);

impl EquidistantChannel {
    /// Clone the flat sample vector when this channel holds
    /// `gpslocation` samples (rare — equidistant blocks are normally
    /// numeric).
    ///
    /// # Errors
    ///
    /// [`OsfError::DataTypeAccessMismatch`] when the stored datatype
    /// is not `GpsLocation`.
    pub fn as_gps_flat(&self) -> Result<Vec<GpsLocation>, OsfError> {
        match &self.samples {
            NumericValues::GpsLocation(v) => Ok(v.clone()),
            other => Err(OsfError::DataTypeAccessMismatch {
                channel: self.index,
                requested: DataType::GpsLocation,
                actual: other.data_type(),
            }),
        }
    }
}

impl TimestampedChannel {
    /// Pair every timestamp with its `gpslocation` value.
    ///
    /// # Errors
    ///
    /// [`OsfError::DataTypeAccessMismatch`] when the stored datatype
    /// is not `GpsLocation`.
    pub fn as_gps_flat(&self) -> Result<Vec<(i64, GpsLocation)>, OsfError> {
        match &self.values {
            NumericValues::GpsLocation(v) => Ok(self
                .timestamps_ns
                .iter()
                .copied()
                .zip(v.iter().copied())
                .collect()),
            other => Err(OsfError::DataTypeAccessMismatch {
                channel: self.index,
                requested: DataType::GpsLocation,
                actual: other.data_type(),
            }),
        }
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
    fn equidistant_samples_with_time_single_segment() {
        let chan = make_eq_channel(
            NumericValues::Double(vec![10.0, 20.0, 30.0, 40.0]),
            vec![Segment {
                start_timestamp_ns: 1_000_000_000,
                sample_rate_hz: 1.0,
                start_index: 0,
                sample_count: 4,
            }],
        );
        let pairs: Vec<(i64, f64)> = chan
            .samples_with_time()
            .map(|s| match s.value {
                NumericValueRef::Double(v) => (s.timestamp_ns, v),
                _ => panic!("expected Double"),
            })
            .collect();
        assert_eq!(pairs.len(), 4);
        assert_eq!(pairs[0], (1_000_000_000, 10.0));
        assert_eq!(pairs[1], (2_000_000_000, 20.0));
        assert_eq!(pairs[2], (3_000_000_000, 30.0));
        assert_eq!(pairs[3], (4_000_000_000, 40.0));
    }

    #[test]
    fn equidistant_samples_with_time_three_segments_no_interpolation() {
        let chan = make_eq_channel(
            NumericValues::Int32(vec![1, 2, 3, 100, 101, 200, 201]),
            vec![
                Segment {
                    start_timestamp_ns: 1_000,
                    sample_rate_hz: 1000.0,
                    start_index: 0,
                    sample_count: 3,
                },
                Segment {
                    start_timestamp_ns: 1_000_000_000,
                    sample_rate_hz: 1000.0,
                    start_index: 3,
                    sample_count: 2,
                },
                Segment {
                    start_timestamp_ns: 5_000_000_000,
                    sample_rate_hz: 1000.0,
                    start_index: 5,
                    sample_count: 2,
                },
            ],
        );
        let pairs: Vec<(i64, i32)> = chan
            .samples_with_time()
            .map(|s| match s.value {
                NumericValueRef::Int32(v) => (s.timestamp_ns, v),
                _ => panic!("expected Int32"),
            })
            .collect();
        assert_eq!(pairs.len(), 7);
        assert_eq!(pairs[0], (1_000, 1));
        // Each segment must start at its own start_timestamp_ns; gaps
        // between segments are NOT interpolated.
        assert_eq!(pairs[3], (1_000_000_000, 100));
        assert_eq!(pairs[5], (5_000_000_000, 200));
    }

    #[test]
    fn flat_access_mismatch_returns_typed_error() {
        let chan = make_eq_channel(
            NumericValues::Int32(vec![1, 2, 3]),
            vec![Segment {
                start_timestamp_ns: 0,
                sample_rate_hz: 1.0,
                start_index: 0,
                sample_count: 3,
            }],
        );
        let err = chan.as_doubles_flat().unwrap_err();
        match err {
            OsfError::DataTypeAccessMismatch {
                requested, actual, ..
            } => {
                assert_eq!(requested, DataType::Double);
                assert_eq!(actual, DataType::Int32);
            }
            other => panic!("expected DataTypeAccessMismatch, got {other:?}"),
        }
        // Matching access works.
        assert_eq!(chan.as_int32_flat().unwrap(), vec![1, 2, 3]);
    }

    #[test]
    fn empty_channel_iteration_yields_nothing() {
        let chan = make_eq_channel(NumericValues::Double(Vec::new()), Vec::new());
        assert_eq!(chan.samples_with_time().count(), 0);
        assert!(chan.samples.is_empty());
    }

    #[test]
    fn timestamped_samples_with_time_pairs_correctly() {
        let chan = TimestampedChannel {
            index: 0,
            name: "ts".into(),
            data_type: DataType::Int32,
            physical_unit: None,
            display_name: None,
            channel_def: ChannelMeta::default(),
            timestamps_ns: vec![100, 200, 300],
            values: NumericValues::Int32(vec![10, 20, 30]),
        };
        let pairs: Vec<(i64, i32)> = chan
            .samples_with_time()
            .map(|s| match s.value {
                NumericValueRef::Int32(v) => (s.timestamp_ns, v),
                _ => panic!("expected Int32"),
            })
            .collect();
        assert_eq!(pairs, vec![(100, 10), (200, 20), (300, 30)]);
        // Flat helper produces the same data.
        assert_eq!(
            chan.as_int32_flat().unwrap(),
            vec![(100, 10), (200, 20), (300, 30)]
        );
    }

    #[test]
    fn variable_string_iteration_borrows_values() {
        let chan = VariableChannel {
            index: 0,
            name: "msg".into(),
            data_type: DataType::String,
            physical_unit: None,
            display_name: None,
            mime_type: None,
            channel_def: ChannelMeta::default(),
            timestamps_ns: vec![1, 2],
            string_values: Some(vec!["hi".into(), "bye".into()]),
            binary_values: None,
        };
        let collected: Vec<(i64, &str)> = chan
            .samples_with_time()
            .map(|s| match s.value {
                VariableValueRef::String(v) => (s.timestamp_ns, v),
                _ => panic!("expected String"),
            })
            .collect();
        assert_eq!(collected, vec![(1, "hi"), (2, "bye")]);
        assert_eq!(chan.as_strings().unwrap(), &["hi".to_string(), "bye".into()]);
        // Asking for binaries on a string channel fails.
        assert!(matches!(
            chan.as_binaries(),
            Err(OsfError::DataTypeAccessMismatch { .. })
        ));
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
