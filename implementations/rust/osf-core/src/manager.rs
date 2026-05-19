// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! Manager layer — assembles typed in-memory channels from the block
//! stream.
//!
//! This module is the second API tier on top of [`crate::reader::BlockReader`].
//! Where the reader yields per-block raw views, the manager groups
//! blocks by channel and produces [`crate::Channel`] values that hide
//! the on-disk block boundaries: equidistant samples flat with their
//! segments, timestamped samples with parallel timestamp / value
//! vectors, string / binary samples as `(timestamp, value)` pairs.
//!
//! The public [`DataManager`] struct lands in a follow-up commit; this
//! commit lays down the private builder logic.

use crate::block::{
    Block, BlockKind, NumericPayload, RelTimestampedPayload, TimestampedPayload,
};
use crate::compression::{self, MaybeCompressed};
use crate::data_channel::{
    Channel, ChannelMeta, EquidistantChannel, NumericValues, Segment, TimestampedChannel,
    VariableChannel,
};
use crate::error::OsfError;
use crate::header::parse_magic_header;
use crate::meta::MetaBlock;
use crate::reader::BlockReader;
use crate::stats::ReaderStats;
use crate::types::{ChannelType, DataType};
use log::warn;
use std::collections::HashMap;
use std::fs::File;
use std::io::Read;
use std::path::Path;

// -----------------------------------------------------------
// DataManager — public API on top of build_channels.
// -----------------------------------------------------------

/// High-level read-only view of an OSF file: parsed metablock,
/// `ReaderStats`, and the typed channel list.
///
/// Construct with [`DataManager::load_from_file`] for the convenience
/// case (open by path, BufReader internally), or
/// [`DataManager::load_from_reader`] for streaming sources.
pub struct DataManager {
    /// The parsed metablock, kept verbatim so applications can read
    /// file-level metadata (creator, created_utc, infos, …) without
    /// re-opening the file.
    pub meta: MetaBlock,
    /// Telemetry from the [`BlockReader`] — file/section sizes,
    /// elapsed time, per-channel sample counts and timing.
    pub stats: ReaderStats,
    channels: Vec<Channel>,
    by_name: HashMap<String, usize>,
    by_index: HashMap<u16, usize>,
}

impl DataManager {
    /// Open `path`, parse the magic header and metablock, drive the
    /// [`BlockReader`] to completion, and assemble the typed channel
    /// list.
    ///
    /// Channel-by-name access is the documented entry point per
    /// DECISIONS §10.
    ///
    /// # Errors
    ///
    /// Forwards errors from the magic-header parser, the metablock
    /// parser, the block reader, and the manager-layer builder.
    pub fn load_from_file<P: AsRef<Path>>(path: P) -> Result<Self, OsfError> {
        let file = File::open(path.as_ref())?;
        let file_size = file.metadata().ok().map(|m| m.len());
        let stream = compression::detect_and_wrap(file)?;
        Self::load_from_stream_with_size(stream, file_size)
    }

    /// Construct from any `Read`. The reader is fed through the OSFZ
    /// detection layer, so callers can hand over a compressed stream
    /// (e.g. from a network socket) just as well as an uncompressed
    /// one and the manager will Decompress transparently.
    ///
    /// # Errors
    ///
    /// Forwards errors from the magic-header parser, the metablock
    /// parser, the block reader, and the manager-layer builder.
    pub fn load_from_reader<R: Read>(reader: R) -> Result<Self, OsfError> {
        let stream = compression::detect_and_wrap(reader)?;
        Self::load_from_stream_with_size(stream, None)
    }

    fn load_from_stream_with_size<R: Read>(
        mut stream: MaybeCompressed<R>,
        file_size: Option<u64>,
    ) -> Result<Self, OsfError> {
        let compression_format = stream.detected_format().into();
        let was_compressed = stream.is_compressed();

        let header = parse_magic_header(&mut stream)?;
        let metablock_size_bytes = header.metablock_len;
        let mut body = vec![0u8; header.metablock_len as usize];
        stream.read_exact(&mut body)?;
        let meta = crate::parse_metablock(header.version, &body)?;

        let mut block_reader = BlockReader::new(stream, &meta);
        if let Some(size) = file_size {
            block_reader = block_reader.with_file_size(size);
        }

        let (channels, by_name, by_index) = build_channels(&meta, &mut block_reader)?;

        let mut stats = block_reader.stats();
        // Header size is unknown here without a counting reader; the
        // streaming entry point therefore reports it as 0. Metablock
        // size is exact.
        stats.metablock_size_bytes = metablock_size_bytes;
        stats.compressed = was_compressed;
        stats.compression_format = compression_format;

        Ok(Self {
            meta,
            stats,
            channels,
            by_name,
            by_index,
        })
    }

    /// Read-only view of all channels in metablock order.
    #[must_use]
    pub fn channels(&self) -> &[Channel] {
        &self.channels
    }

    /// Look up a channel by its fully qualified name.
    ///
    /// **Mandatory access form** per DECISIONS §10.
    #[must_use]
    pub fn channel(&self, name: &str) -> Option<&Channel> {
        self.by_name.get(name).map(|&i| &self.channels[i])
    }

    /// Look up a channel by its on-disk index (the integer `index`
    /// attribute from the metablock). Optional access form per
    /// DECISIONS §10.
    #[must_use]
    pub fn channel_by_index(&self, index: u16) -> Option<&Channel> {
        self.by_index.get(&index).map(|&i| &self.channels[i])
    }

    /// Iterate over all channels in metablock order.
    pub fn iter(&self) -> impl Iterator<Item = &Channel> + '_ {
        self.channels.iter()
    }

    /// Iterate over channels whose data type matches `data_type`.
    /// Convenience wrapper over `iter()` plus a closure.
    pub fn channels_by_data_type(
        &self,
        data_type: DataType,
    ) -> impl Iterator<Item = &Channel> + '_ {
        self.channels
            .iter()
            .filter(move |c| c.data_type() == data_type)
    }
}



/// Internal per-channel builder. Driven by `apply_block` and finalised
/// into a [`Channel`] (or dropped, for `Unsupported`).
struct ChannelBuilder {
    index: u16,
    name: String,
    data_type: DataType,
    physical_unit: Option<String>,
    display_name: Option<String>,
    mime_type: Option<String>,
    channel_def: ChannelMeta,
    state: BuilderState,
    /// Last absolute timestamp observed on this channel — required to
    /// anchor `bcContinuedRelStampData` blocks, which carry deltas.
    last_timestamp_ns: Option<i64>,
}

/// Per-channel state. The variant is locked in by the first typed
/// block of the channel; later blocks of a different family produce
/// [`OsfError::ChannelMixedBlockTypes`].
enum BuilderState {
    /// Numeric channel that has not yet seen a typed block. Resolves
    /// to `Equidistant` on the first `bcStartData` or to `Timestamped`
    /// on the first `bcAbsTimeStampData`.
    Pending,
    /// Equidistant numeric channel — flat samples plus segment list.
    Equidistant {
        samples: NumericValues,
        segments: Vec<Segment>,
    },
    /// Timestamped numeric (or GPS) channel.
    Timestamped {
        timestamps_ns: Vec<i64>,
        values: NumericValues,
    },
    /// Timestamped string / binary channel — pre-determined from the
    /// metablock since the variant is unambiguous from the data type.
    Variable {
        timestamps_ns: Vec<i64>,
        string_values: Option<Vec<String>>,
        binary_values: Option<Vec<Vec<u8>>>,
    },
    /// Channel marked `DataType::Unsupported` or
    /// `ChannelType::Unsupported` — the reader skips its blocks
    /// entirely; the builder drops out of the final channel list.
    Unsupported,
}

/// Three-tuple returned by [`build_channels`]: the channel list in
/// metablock order, plus name → slot and index → slot lookup tables.
pub(crate) type BuildChannelsOutput = (Vec<Channel>, HashMap<String, usize>, HashMap<u16, usize>);

/// Build the typed-channel list for a `MetaBlock` by streaming through
/// a block iterator (typically a [`crate::reader::BlockReader`] but
/// any iterator over `Result<Block, OsfError>` works — the unit tests
/// inject mock sequences).
///
/// Channels are returned in metablock order. Channels whose
/// `ChannelType` or `DataType` is `Unsupported` are dropped; the
/// reader has already skipped their blocks. The HashMap returned
/// alongside maps the canonical channel name to its slot in the Vec.
///
/// # Errors
///
/// Forwards block-stream errors plus the manager-layer variants:
/// `ChannelMixedBlockTypes`, `ContinuedDataWithoutStart`,
/// `RelStampWithoutAnchor`, `DataTypeMismatch`.
pub(crate) fn build_channels<I>(meta: &MetaBlock, blocks: I) -> Result<BuildChannelsOutput, OsfError>
where
    I: IntoIterator<Item = Result<Block, OsfError>>,
{
    let mut builders: HashMap<u16, ChannelBuilder> = HashMap::with_capacity(meta.channels.len());
    let mut order: Vec<u16> = Vec::with_capacity(meta.channels.len());

    for chan in &meta.channels {
        let initial_state = initial_state_for(&chan.channel_type, &chan.data_type);
        builders.insert(
            chan.index,
            ChannelBuilder {
                index: chan.index,
                name: chan.name.clone(),
                data_type: chan.data_type.clone(),
                physical_unit: chan.physical_unit.clone(),
                display_name: chan.display_name.clone(),
                mime_type: chan.mime_type.clone(),
                channel_def: ChannelMeta {
                    channel_type: chan.channel_type.clone(),
                    size_of_length_value: chan.size_of_length_value,
                    time_increment_ns: chan.time_increment_ns,
                    reference: chan.reference.clone(),
                    physical_dimension: chan.physical_dimension.clone(),
                    comment: chan.comment.clone(),
                    spectrum_type: chan.spectrum_type,
                },
                state: initial_state,
                last_timestamp_ns: None,
            },
        );
        order.push(chan.index);
    }

    for block in blocks {
        let block = block?;
        let Some(builder) = builders.get_mut(&block.channel_index) else {
            // The reader produces a hard UnknownChannelIndex earlier;
            // this branch defends the manager against custom iterators
            // that bypass the reader.
            return Err(OsfError::UnknownChannelIndex(block.channel_index));
        };
        apply_block(builder, block.kind)?;
    }

    let mut channels: Vec<Channel> = Vec::with_capacity(order.len());
    let mut by_name: HashMap<String, usize> = HashMap::with_capacity(order.len());
    let mut by_index: HashMap<u16, usize> = HashMap::with_capacity(order.len());

    for index in order {
        let Some(builder) = builders.remove(&index) else {
            continue;
        };
        let Some(channel) = builder.finalize() else {
            continue;
        };
        let slot = channels.len();
        if by_name.insert(channel.name().to_string(), slot).is_some() {
            warn!(
                "duplicate channel name {:?} at indices — first definition kept",
                channel.name()
            );
        }
        by_index.insert(channel.index(), slot);
        channels.push(channel);
    }

    Ok((channels, by_name, by_index))
}

fn initial_state_for(channel_type: &ChannelType, data_type: &DataType) -> BuilderState {
    if matches!(channel_type, ChannelType::Unsupported(_))
        || matches!(data_type, DataType::Unsupported(_))
    {
        return BuilderState::Unsupported;
    }
    match data_type {
        DataType::String => BuilderState::Variable {
            timestamps_ns: Vec::new(),
            string_values: Some(Vec::new()),
            binary_values: None,
        },
        DataType::Binary | DataType::ByteArray => BuilderState::Variable {
            timestamps_ns: Vec::new(),
            string_values: None,
            binary_values: Some(Vec::new()),
        },
        _ => BuilderState::Pending,
    }
}

fn apply_block(builder: &mut ChannelBuilder, kind: BlockKind) -> Result<(), OsfError> {
    match kind {
        BlockKind::Skipped { .. } => Ok(()),
        BlockKind::StartData {
            start_timestamp_ns,
            sample_rate_hz,
            samples,
        } => apply_start(builder, start_timestamp_ns, sample_rate_hz, samples),
        BlockKind::ContinuedData { samples } => apply_continued(builder, samples),
        BlockKind::AbsTimestampData { samples } => apply_abs_timestamped(builder, samples),
        BlockKind::ContinuedRelStampData { samples } => apply_rel_timestamped(builder, samples),
    }
}

fn apply_start(
    builder: &mut ChannelBuilder,
    start_timestamp_ns: i64,
    sample_rate_hz: f64,
    payload: NumericPayload,
) -> Result<(), OsfError> {
    let payload_dt = numeric_payload_data_type(&payload);
    if payload_dt != builder.data_type {
        return Err(OsfError::DataTypeMismatch {
            channel: builder.index,
            expected: builder.data_type.clone(),
            got: payload_dt,
        });
    }

    match &mut builder.state {
        BuilderState::Pending => {
            let mut samples = NumericValues::empty_for(&builder.data_type).ok_or_else(|| {
                OsfError::InvalidBlock(format!(
                    "channel {} declared {:?} cannot hold equidistant samples",
                    builder.index, builder.data_type
                ))
            })?;
            extend_numeric(&mut samples, payload, builder.index)?;
            let sample_count = samples.len();
            let segment = Segment {
                start_timestamp_ns,
                sample_rate_hz,
                start_index: 0,
                sample_count,
            };
            builder.state = BuilderState::Equidistant {
                samples,
                segments: vec![segment],
            };
            update_last_ts_from_segment(builder, start_timestamp_ns, sample_rate_hz, sample_count);
            Ok(())
        }
        BuilderState::Equidistant { samples, segments } => {
            let start_index = samples.len();
            extend_numeric(samples, payload, builder.index)?;
            let appended = samples.len() - start_index;
            segments.push(Segment {
                start_timestamp_ns,
                sample_rate_hz,
                start_index,
                sample_count: appended,
            });
            update_last_ts_from_segment(builder, start_timestamp_ns, sample_rate_hz, appended);
            Ok(())
        }
        BuilderState::Timestamped { .. } | BuilderState::Variable { .. } => {
            Err(OsfError::ChannelMixedBlockTypes {
                index: builder.index,
            })
        }
        BuilderState::Unsupported => Ok(()),
    }
}

fn apply_continued(
    builder: &mut ChannelBuilder,
    payload: NumericPayload,
) -> Result<(), OsfError> {
    let payload_dt = numeric_payload_data_type(&payload);
    if payload_dt != builder.data_type {
        return Err(OsfError::DataTypeMismatch {
            channel: builder.index,
            expected: builder.data_type.clone(),
            got: payload_dt,
        });
    }
    let index = builder.index;
    let segment_info = match &mut builder.state {
        BuilderState::Pending => {
            return Err(OsfError::ContinuedDataWithoutStart { index });
        }
        BuilderState::Equidistant { samples, segments } => {
            let last = segments
                .last_mut()
                .ok_or(OsfError::ContinuedDataWithoutStart { index })?;
            let start_index = samples.len();
            extend_numeric(samples, payload, index)?;
            let appended = samples.len() - start_index;
            last.sample_count += appended;
            (last.start_timestamp_ns, last.sample_rate_hz, last.sample_count)
        }
        BuilderState::Timestamped { .. } | BuilderState::Variable { .. } => {
            return Err(OsfError::ChannelMixedBlockTypes { index });
        }
        BuilderState::Unsupported => return Ok(()),
    };
    let (start_ts, rate, count) = segment_info;
    update_last_ts_from_segment(builder, start_ts, rate, count);
    Ok(())
}

fn apply_abs_timestamped(
    builder: &mut ChannelBuilder,
    payload: TimestampedPayload,
) -> Result<(), OsfError> {
    let payload_dt = timestamped_payload_data_type(&payload);
    if payload_dt != builder.data_type {
        return Err(OsfError::DataTypeMismatch {
            channel: builder.index,
            expected: builder.data_type.clone(),
            got: payload_dt,
        });
    }
    match &mut builder.state {
        BuilderState::Pending => {
            let (timestamps_ns, values) =
                init_timestamped_from_payload(&builder.data_type, payload, builder.index)?;
            builder.last_timestamp_ns = timestamps_ns.last().copied();
            builder.state = BuilderState::Timestamped {
                timestamps_ns,
                values,
            };
            Ok(())
        }
        BuilderState::Timestamped {
            timestamps_ns,
            values,
        } => {
            let appended_count =
                extend_timestamped(timestamps_ns, values, payload, builder.index)?;
            if appended_count > 0 {
                builder.last_timestamp_ns = timestamps_ns.last().copied();
            }
            Ok(())
        }
        BuilderState::Variable {
            timestamps_ns,
            string_values,
            binary_values,
        } => {
            let appended_count = extend_variable(
                timestamps_ns,
                string_values,
                binary_values,
                &builder.data_type,
                payload,
                builder.index,
            )?;
            if appended_count > 0 {
                builder.last_timestamp_ns = timestamps_ns.last().copied();
            }
            Ok(())
        }
        BuilderState::Equidistant { .. } => Err(OsfError::ChannelMixedBlockTypes {
            index: builder.index,
        }),
        BuilderState::Unsupported => Ok(()),
    }
}

fn apply_rel_timestamped(
    builder: &mut ChannelBuilder,
    payload: RelTimestampedPayload,
) -> Result<(), OsfError> {
    let payload_dt = rel_timestamped_payload_data_type(&payload);
    if payload_dt != builder.data_type {
        return Err(OsfError::DataTypeMismatch {
            channel: builder.index,
            expected: builder.data_type.clone(),
            got: payload_dt,
        });
    }

    let anchor = builder
        .last_timestamp_ns
        .ok_or(OsfError::RelStampWithoutAnchor {
            index: builder.index,
        })?;

    match &mut builder.state {
        BuilderState::Timestamped {
            timestamps_ns,
            values,
        } => {
            let new_anchor =
                extend_rel_timestamped(timestamps_ns, values, anchor, payload, builder.index)?;
            builder.last_timestamp_ns = Some(new_anchor);
            Ok(())
        }
        BuilderState::Pending | BuilderState::Equidistant { .. } | BuilderState::Variable { .. } => {
            // Variable channels carry string/binary, which the reader
            // never produces in a RelTimestampedPayload (it only has
            // numeric variants); reaching this branch with a Variable
            // state implies a corrupt block stream.
            Err(OsfError::ChannelMixedBlockTypes {
                index: builder.index,
            })
        }
        BuilderState::Unsupported => Ok(()),
    }
}

fn update_last_ts_from_segment(
    builder: &mut ChannelBuilder,
    start_timestamp_ns: i64,
    sample_rate_hz: f64,
    sample_count: usize,
) {
    if sample_count == 0 {
        return;
    }
    let last = if sample_rate_hz > 0.0 {
        let offset = (((sample_count - 1) as f64) * 1.0e9 / sample_rate_hz) as i64;
        start_timestamp_ns.saturating_add(offset)
    } else {
        start_timestamp_ns
    };
    builder.last_timestamp_ns = Some(last);
}

impl ChannelBuilder {
    fn finalize(self) -> Option<Channel> {
        let ChannelBuilder {
            index,
            name,
            data_type,
            physical_unit,
            display_name,
            mime_type,
            channel_def,
            state,
            ..
        } = self;
        match state {
            BuilderState::Unsupported => None,
            BuilderState::Pending => {
                // Never received a typed block; emit an empty channel
                // matching the metablock's data-type group.
                if matches!(data_type, DataType::String) {
                    Some(Channel::Variable(VariableChannel {
                        index,
                        name,
                        data_type,
                        physical_unit,
                        display_name,
                        mime_type,
                        channel_def,
                        timestamps_ns: Vec::new(),
                        string_values: Some(Vec::new()),
                        binary_values: None,
                    }))
                } else if matches!(data_type, DataType::Binary | DataType::ByteArray) {
                    Some(Channel::Variable(VariableChannel {
                        index,
                        name,
                        data_type,
                        physical_unit,
                        display_name,
                        mime_type,
                        channel_def,
                        timestamps_ns: Vec::new(),
                        string_values: None,
                        binary_values: Some(Vec::new()),
                    }))
                } else {
                    NumericValues::empty_for(&data_type).map(|samples| {
                        Channel::Equidistant(EquidistantChannel {
                            index,
                            name,
                            data_type,
                            physical_unit,
                            display_name,
                            channel_def,
                            samples,
                            segments: Vec::new(),
                        })
                    })
                }
            }
            BuilderState::Equidistant { samples, segments } => {
                Some(Channel::Equidistant(EquidistantChannel {
                    index,
                    name,
                    data_type,
                    physical_unit,
                    display_name,
                    channel_def,
                    samples,
                    segments,
                }))
            }
            BuilderState::Timestamped {
                timestamps_ns,
                values,
            } => Some(Channel::Timestamped(TimestampedChannel {
                index,
                name,
                data_type,
                physical_unit,
                display_name,
                channel_def,
                timestamps_ns,
                values,
            })),
            BuilderState::Variable {
                timestamps_ns,
                string_values,
                binary_values,
            } => Some(Channel::Variable(VariableChannel {
                index,
                name,
                data_type,
                physical_unit,
                display_name,
                mime_type,
                channel_def,
                timestamps_ns,
                string_values,
                binary_values,
            })),
        }
    }
}

// -----------------------------------------------------------
// Payload-to-storage helpers.
// -----------------------------------------------------------

fn numeric_payload_data_type(p: &NumericPayload) -> DataType {
    match p {
        NumericPayload::Bool(_) => DataType::Bool,
        NumericPayload::Int8(_) => DataType::Int8,
        NumericPayload::Int16(_) => DataType::Int16,
        NumericPayload::Int32(_) => DataType::Int32,
        NumericPayload::Int64(_) => DataType::Int64,
        NumericPayload::UInt8(_) => DataType::UInt8,
        NumericPayload::UInt16(_) => DataType::UInt16,
        NumericPayload::UInt32(_) => DataType::UInt32,
        NumericPayload::UInt64(_) => DataType::UInt64,
        NumericPayload::Float(_) => DataType::Float,
        NumericPayload::Double(_) => DataType::Double,
    }
}

fn timestamped_payload_data_type(p: &TimestampedPayload) -> DataType {
    match p {
        TimestampedPayload::Bool(_) => DataType::Bool,
        TimestampedPayload::Int8(_) => DataType::Int8,
        TimestampedPayload::Int16(_) => DataType::Int16,
        TimestampedPayload::Int32(_) => DataType::Int32,
        TimestampedPayload::Int64(_) => DataType::Int64,
        TimestampedPayload::UInt8(_) => DataType::UInt8,
        TimestampedPayload::UInt16(_) => DataType::UInt16,
        TimestampedPayload::UInt32(_) => DataType::UInt32,
        TimestampedPayload::UInt64(_) => DataType::UInt64,
        TimestampedPayload::Float(_) => DataType::Float,
        TimestampedPayload::Double(_) => DataType::Double,
        TimestampedPayload::String(_) => DataType::String,
        TimestampedPayload::Binary(_) => DataType::Binary,
        TimestampedPayload::GpsLocation(_) => DataType::GpsLocation,
    }
}

fn rel_timestamped_payload_data_type(p: &RelTimestampedPayload) -> DataType {
    match p {
        RelTimestampedPayload::Bool(_) => DataType::Bool,
        RelTimestampedPayload::Int8(_) => DataType::Int8,
        RelTimestampedPayload::Int16(_) => DataType::Int16,
        RelTimestampedPayload::Int32(_) => DataType::Int32,
        RelTimestampedPayload::Int64(_) => DataType::Int64,
        RelTimestampedPayload::UInt8(_) => DataType::UInt8,
        RelTimestampedPayload::UInt16(_) => DataType::UInt16,
        RelTimestampedPayload::UInt32(_) => DataType::UInt32,
        RelTimestampedPayload::UInt64(_) => DataType::UInt64,
        RelTimestampedPayload::Float(_) => DataType::Float,
        RelTimestampedPayload::Double(_) => DataType::Double,
    }
}

/// Append the contents of `payload` onto `target`. The two storages
/// must agree on the data-type variant — the caller has already
/// verified this via `numeric_payload_data_type` against the channel
/// metadata, so the catch-all branch indicates a programmer error
/// rather than a file error.
fn extend_numeric(
    target: &mut NumericValues,
    payload: NumericPayload,
    channel: u16,
) -> Result<(), OsfError> {
    let target_dt = target.data_type();
    let payload_dt = numeric_payload_data_type(&payload);
    match (target, payload) {
        (NumericValues::Bool(t), NumericPayload::Bool(p)) => t.extend(p),
        (NumericValues::Int8(t), NumericPayload::Int8(p)) => t.extend(p),
        (NumericValues::Int16(t), NumericPayload::Int16(p)) => t.extend(p),
        (NumericValues::Int32(t), NumericPayload::Int32(p)) => t.extend(p),
        (NumericValues::Int64(t), NumericPayload::Int64(p)) => t.extend(p),
        (NumericValues::UInt8(t), NumericPayload::UInt8(p)) => t.extend(p),
        (NumericValues::UInt16(t), NumericPayload::UInt16(p)) => t.extend(p),
        (NumericValues::UInt32(t), NumericPayload::UInt32(p)) => t.extend(p),
        (NumericValues::UInt64(t), NumericPayload::UInt64(p)) => t.extend(p),
        (NumericValues::Float(t), NumericPayload::Float(p)) => t.extend(p),
        (NumericValues::Double(t), NumericPayload::Double(p)) => t.extend(p),
        _ => {
            return Err(OsfError::DataTypeMismatch {
                channel,
                expected: target_dt,
                got: payload_dt,
            });
        }
    }
    Ok(())
}

/// Build a fresh `(timestamps, values)` pair from a TimestampedPayload
/// for the `Pending → Timestamped` transition.
fn init_timestamped_from_payload(
    expected: &DataType,
    payload: TimestampedPayload,
    channel: u16,
) -> Result<(Vec<i64>, NumericValues), OsfError> {
    let mismatch = |got: DataType| OsfError::DataTypeMismatch {
        channel,
        expected: expected.clone(),
        got,
    };

    macro_rules! split {
        ($variant:ident, $ty:ty) => {{
            if !matches!(expected, DataType::$variant) {
                return Err(mismatch(DataType::$variant));
            }
        }};
    }

    let (timestamps, values) = match payload {
        TimestampedPayload::Bool(p) => {
            split!(Bool, bool);
            split_pairs(p, NumericValues::Bool)
        }
        TimestampedPayload::Int8(p) => {
            split!(Int8, i8);
            split_pairs(p, NumericValues::Int8)
        }
        TimestampedPayload::Int16(p) => {
            split!(Int16, i16);
            split_pairs(p, NumericValues::Int16)
        }
        TimestampedPayload::Int32(p) => {
            split!(Int32, i32);
            split_pairs(p, NumericValues::Int32)
        }
        TimestampedPayload::Int64(p) => {
            split!(Int64, i64);
            split_pairs(p, NumericValues::Int64)
        }
        TimestampedPayload::UInt8(p) => {
            split!(UInt8, u8);
            split_pairs(p, NumericValues::UInt8)
        }
        TimestampedPayload::UInt16(p) => {
            split!(UInt16, u16);
            split_pairs(p, NumericValues::UInt16)
        }
        TimestampedPayload::UInt32(p) => {
            split!(UInt32, u32);
            split_pairs(p, NumericValues::UInt32)
        }
        TimestampedPayload::UInt64(p) => {
            split!(UInt64, u64);
            split_pairs(p, NumericValues::UInt64)
        }
        TimestampedPayload::Float(p) => {
            split!(Float, f32);
            split_pairs(p, NumericValues::Float)
        }
        TimestampedPayload::Double(p) => {
            split!(Double, f64);
            split_pairs(p, NumericValues::Double)
        }
        TimestampedPayload::GpsLocation(p) => {
            split!(GpsLocation, _);
            split_pairs(p, NumericValues::GpsLocation)
        }
        TimestampedPayload::String(_) | TimestampedPayload::Binary(_) => {
            // Variable-length cases never reach init_timestamped_from_payload —
            // those flow through the Variable state machine.
            return Err(OsfError::InvalidBlock(format!(
                "channel {channel}: unexpected variable-length payload \
                 routed through numeric timestamped init"
            )));
        }
    };
    Ok((timestamps, values))
}

fn split_pairs<T: Copy, F>(pairs: Vec<(i64, T)>, wrap: F) -> (Vec<i64>, NumericValues)
where
    F: FnOnce(Vec<T>) -> NumericValues,
{
    let mut timestamps = Vec::with_capacity(pairs.len());
    let mut values = Vec::with_capacity(pairs.len());
    for (ts, v) in pairs {
        timestamps.push(ts);
        values.push(v);
    }
    (timestamps, wrap(values))
}

/// Append a TimestampedPayload onto an existing Timestamped channel.
fn extend_timestamped(
    timestamps: &mut Vec<i64>,
    values: &mut NumericValues,
    payload: TimestampedPayload,
    channel: u16,
) -> Result<usize, OsfError> {
    let payload_dt = timestamped_payload_data_type(&payload);
    let target_dt = values.data_type();
    if payload_dt != target_dt {
        return Err(OsfError::DataTypeMismatch {
            channel,
            expected: target_dt,
            got: payload_dt,
        });
    }

    macro_rules! extend_arm {
        ($variant:ident, $payload:expr) => {{
            let p = $payload;
            let count = p.len();
            if let NumericValues::$variant(target) = values {
                for (ts, v) in p {
                    timestamps.push(ts);
                    target.push(v);
                }
            } else {
                unreachable!(
                    "data-type check above guarantees NumericValues::{}",
                    stringify!($variant)
                );
            }
            count
        }};
    }

    let count = match payload {
        TimestampedPayload::Bool(p) => extend_arm!(Bool, p),
        TimestampedPayload::Int8(p) => extend_arm!(Int8, p),
        TimestampedPayload::Int16(p) => extend_arm!(Int16, p),
        TimestampedPayload::Int32(p) => extend_arm!(Int32, p),
        TimestampedPayload::Int64(p) => extend_arm!(Int64, p),
        TimestampedPayload::UInt8(p) => extend_arm!(UInt8, p),
        TimestampedPayload::UInt16(p) => extend_arm!(UInt16, p),
        TimestampedPayload::UInt32(p) => extend_arm!(UInt32, p),
        TimestampedPayload::UInt64(p) => extend_arm!(UInt64, p),
        TimestampedPayload::Float(p) => extend_arm!(Float, p),
        TimestampedPayload::Double(p) => extend_arm!(Double, p),
        TimestampedPayload::GpsLocation(p) => extend_arm!(GpsLocation, p),
        TimestampedPayload::String(_) | TimestampedPayload::Binary(_) => {
            return Err(OsfError::DataTypeMismatch {
                channel,
                expected: target_dt,
                got: payload_dt,
            });
        }
    };
    Ok(count)
}

/// Append a TimestampedPayload onto a Variable channel
/// (string / binary).
fn extend_variable(
    timestamps: &mut Vec<i64>,
    string_values: &mut Option<Vec<String>>,
    binary_values: &mut Option<Vec<Vec<u8>>>,
    expected: &DataType,
    payload: TimestampedPayload,
    channel: u16,
) -> Result<usize, OsfError> {
    let mismatch = |got: DataType| OsfError::DataTypeMismatch {
        channel,
        expected: expected.clone(),
        got,
    };
    match (expected, payload) {
        (DataType::String, TimestampedPayload::String(pairs)) => {
            let count = pairs.len();
            let target = string_values.as_mut().expect("string channel must have storage");
            for (ts, s) in pairs {
                timestamps.push(ts);
                target.push(s);
            }
            Ok(count)
        }
        (DataType::Binary | DataType::ByteArray, TimestampedPayload::Binary(pairs)) => {
            let count = pairs.len();
            let target = binary_values.as_mut().expect("binary channel must have storage");
            for (ts, b) in pairs {
                timestamps.push(ts);
                target.push(b);
            }
            Ok(count)
        }
        (_, p) => Err(mismatch(timestamped_payload_data_type(&p))),
    }
}

/// Append a RelTimestampedPayload onto a Timestamped channel,
/// converting deltas to absolute timestamps starting from `anchor`.
/// Returns the new last absolute timestamp.
fn extend_rel_timestamped(
    timestamps: &mut Vec<i64>,
    values: &mut NumericValues,
    anchor: i64,
    payload: RelTimestampedPayload,
    channel: u16,
) -> Result<i64, OsfError> {
    let payload_dt = rel_timestamped_payload_data_type(&payload);
    let target_dt = values.data_type();
    if payload_dt != target_dt {
        return Err(OsfError::DataTypeMismatch {
            channel,
            expected: target_dt,
            got: payload_dt,
        });
    }

    let mut last = anchor;

    macro_rules! extend_rel_arm {
        ($variant:ident, $payload:expr) => {{
            let p = $payload;
            if let NumericValues::$variant(target) = values {
                for (delta, v) in p {
                    last = last.saturating_add(i64::from(delta));
                    timestamps.push(last);
                    target.push(v);
                }
            } else {
                unreachable!(
                    "data-type check above guarantees NumericValues::{}",
                    stringify!($variant)
                );
            }
        }};
    }

    match payload {
        RelTimestampedPayload::Bool(p) => extend_rel_arm!(Bool, p),
        RelTimestampedPayload::Int8(p) => extend_rel_arm!(Int8, p),
        RelTimestampedPayload::Int16(p) => extend_rel_arm!(Int16, p),
        RelTimestampedPayload::Int32(p) => extend_rel_arm!(Int32, p),
        RelTimestampedPayload::Int64(p) => extend_rel_arm!(Int64, p),
        RelTimestampedPayload::UInt8(p) => extend_rel_arm!(UInt8, p),
        RelTimestampedPayload::UInt16(p) => extend_rel_arm!(UInt16, p),
        RelTimestampedPayload::UInt32(p) => extend_rel_arm!(UInt32, p),
        RelTimestampedPayload::UInt64(p) => extend_rel_arm!(UInt64, p),
        RelTimestampedPayload::Float(p) => extend_rel_arm!(Float, p),
        RelTimestampedPayload::Double(p) => extend_rel_arm!(Double, p),
    }

    Ok(last)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::block::Block;
    use crate::meta::{Channel as MetaChannelDef, FileInfo};

    fn meta_with_channels(channels: Vec<MetaChannelDef>) -> MetaBlock {
        MetaBlock {
            file_info: FileInfo::default(),
            channels,
            infos: Vec::new(),
        }
    }

    fn make_meta_channel(index: u16, data_type: DataType, size: u8) -> MetaChannelDef {
        MetaChannelDef {
            index,
            name: format!("ch{index}"),
            reference: None,
            channel_type: ChannelType::Scalar,
            data_type,
            time_increment_ns: None,
            size_of_length_value: size,
            mime_type: None,
            spectrum_type: None,
            physical_unit: None,
            physical_dimension: None,
            display_name: None,
            comment: None,
        }
    }

    fn block_start_double(channel_index: u16, ts: i64, rate: f64, samples: Vec<f64>) -> Block {
        Block {
            channel_index,
            kind: BlockKind::StartData {
                start_timestamp_ns: ts,
                sample_rate_hz: rate,
                samples: NumericPayload::Double(samples),
            },
        }
    }

    fn block_continued_double(channel_index: u16, samples: Vec<f64>) -> Block {
        Block {
            channel_index,
            kind: BlockKind::ContinuedData {
                samples: NumericPayload::Double(samples),
            },
        }
    }

    fn block_abs_int32(channel_index: u16, pairs: Vec<(i64, i32)>) -> Block {
        Block {
            channel_index,
            kind: BlockKind::AbsTimestampData {
                samples: TimestampedPayload::Int32(pairs),
            },
        }
    }

    #[test]
    fn one_start_plus_one_continued_yields_one_segment() {
        let meta = meta_with_channels(vec![make_meta_channel(0, DataType::Double, 2)]);
        let blocks = vec![
            Ok(block_start_double(0, 1_000, 1000.0, (0..100).map(|i| i as f64).collect())),
            Ok(block_continued_double(0, (100..300).map(|i| i as f64).collect())),
        ];
        let (channels, _by_name, _by_index) = build_channels(&meta, blocks).unwrap();
        assert_eq!(channels.len(), 1);
        match &channels[0] {
            Channel::Equidistant(c) => {
                assert_eq!(c.segments.len(), 1);
                assert_eq!(c.segments[0].sample_count, 300);
                assert_eq!(c.samples.len(), 300);
            }
            other => panic!("expected Equidistant, got {other:?}"),
        }
    }

    #[test]
    fn two_start_blocks_open_two_segments() {
        let meta = meta_with_channels(vec![make_meta_channel(0, DataType::Double, 2)]);
        let blocks = vec![
            Ok(block_start_double(0, 0, 1000.0, vec![1.0; 50])),
            Ok(block_start_double(0, 1_000_000_000, 2000.0, vec![2.0; 30])),
        ];
        let (channels, _, _) = build_channels(&meta, blocks).unwrap();
        match &channels[0] {
            Channel::Equidistant(c) => {
                assert_eq!(c.segments.len(), 2);
                assert_eq!(c.segments[0].sample_count, 50);
                assert_eq!(c.segments[0].start_index, 0);
                assert_eq!(c.segments[1].sample_count, 30);
                assert_eq!(c.segments[1].start_index, 50);
                assert_eq!(c.samples.len(), 80);
            }
            other => panic!("expected Equidistant, got {other:?}"),
        }
    }

    #[test]
    fn start_then_abs_timestamp_is_mixed_block_types_error() {
        let meta = meta_with_channels(vec![make_meta_channel(0, DataType::Double, 2)]);
        let blocks = vec![
            Ok(block_start_double(0, 0, 1000.0, vec![1.0; 10])),
            Ok(Block {
                channel_index: 0,
                kind: BlockKind::AbsTimestampData {
                    samples: TimestampedPayload::Double(vec![(100, 1.0)]),
                },
            }),
        ];
        let err = build_channels(&meta, blocks).unwrap_err();
        assert!(matches!(err, OsfError::ChannelMixedBlockTypes { index: 0 }), "got {err:?}");
    }

    #[test]
    fn continued_without_start_is_error() {
        let meta = meta_with_channels(vec![make_meta_channel(0, DataType::Double, 2)]);
        let blocks = vec![Ok(block_continued_double(0, vec![1.0]))];
        let err = build_channels(&meta, blocks).unwrap_err();
        assert!(
            matches!(err, OsfError::ContinuedDataWithoutStart { index: 0 }),
            "got {err:?}"
        );
    }

    #[test]
    fn unsupported_channel_does_not_appear_in_output() {
        let mut meta_channel = make_meta_channel(0, DataType::Double, 2);
        meta_channel.channel_type = ChannelType::Unsupported("vector".into());
        let meta = meta_with_channels(vec![
            meta_channel,
            make_meta_channel(1, DataType::Int32, 2),
        ]);
        let (channels, by_name, _) = build_channels(&meta, std::iter::empty()).unwrap();
        // Channel 0 is dropped; channel 1 stays even with no data.
        assert_eq!(channels.len(), 1);
        assert_eq!(channels[0].index(), 1);
        assert!(by_name.contains_key("ch1"));
        assert!(!by_name.contains_key("ch0"));
    }

    #[test]
    fn abs_timestamped_int32_builds_timestamped_channel() {
        let meta = meta_with_channels(vec![make_meta_channel(0, DataType::Int32, 2)]);
        let blocks = vec![
            Ok(block_abs_int32(0, vec![(100, 1), (200, 2), (300, 3)])),
            Ok(block_abs_int32(0, vec![(400, 4)])),
        ];
        let (channels, _, _) = build_channels(&meta, blocks).unwrap();
        match &channels[0] {
            Channel::Timestamped(c) => {
                assert_eq!(c.timestamps_ns, vec![100, 200, 300, 400]);
                if let NumericValues::Int32(v) = &c.values {
                    assert_eq!(v, &vec![1, 2, 3, 4]);
                } else {
                    panic!("expected Int32 storage");
                }
            }
            other => panic!("expected Timestamped, got {other:?}"),
        }
    }

    #[test]
    fn rel_stamp_after_abs_extends_with_cumulative_timestamps() {
        let meta = meta_with_channels(vec![make_meta_channel(0, DataType::Int32, 2)]);
        let blocks = vec![
            Ok(block_abs_int32(0, vec![(1_000, 10)])),
            Ok(Block {
                channel_index: 0,
                kind: BlockKind::ContinuedRelStampData {
                    samples: RelTimestampedPayload::Int32(vec![(50, 11), (50, 12)]),
                },
            }),
        ];
        let (channels, _, _) = build_channels(&meta, blocks).unwrap();
        match &channels[0] {
            Channel::Timestamped(c) => {
                assert_eq!(c.timestamps_ns, vec![1_000, 1_050, 1_100]);
            }
            other => panic!("expected Timestamped, got {other:?}"),
        }
    }

    #[test]
    fn rel_stamp_without_anchor_is_error() {
        let meta = meta_with_channels(vec![make_meta_channel(0, DataType::Int32, 2)]);
        let blocks = vec![Ok(Block {
            channel_index: 0,
            kind: BlockKind::ContinuedRelStampData {
                samples: RelTimestampedPayload::Int32(vec![(50, 1)]),
            },
        })];
        let err = build_channels(&meta, blocks).unwrap_err();
        assert!(
            matches!(err, OsfError::RelStampWithoutAnchor { index: 0 }),
            "got {err:?}"
        );
    }

    #[test]
    fn data_type_mismatch_is_caught() {
        // Channel says Int32 but block payload is Double.
        let meta = meta_with_channels(vec![make_meta_channel(0, DataType::Int32, 2)]);
        let blocks = vec![Ok(block_start_double(0, 0, 1000.0, vec![1.0]))];
        let err = build_channels(&meta, blocks).unwrap_err();
        assert!(matches!(err, OsfError::DataTypeMismatch { .. }), "got {err:?}");
    }

    #[test]
    fn variable_string_channel_collects_strings() {
        let meta = meta_with_channels(vec![make_meta_channel(0, DataType::String, 4)]);
        let blocks = vec![
            Ok(Block {
                channel_index: 0,
                kind: BlockKind::AbsTimestampData {
                    samples: TimestampedPayload::String(vec![(100, "hi".into())]),
                },
            }),
            Ok(Block {
                channel_index: 0,
                kind: BlockKind::AbsTimestampData {
                    samples: TimestampedPayload::String(vec![(200, "bye".into())]),
                },
            }),
        ];
        let (channels, _, _) = build_channels(&meta, blocks).unwrap();
        match &channels[0] {
            Channel::Variable(c) => {
                assert_eq!(c.timestamps_ns, vec![100, 200]);
                assert_eq!(
                    c.string_values.as_deref().unwrap(),
                    &["hi".to_string(), "bye".into()]
                );
                assert!(c.binary_values.is_none());
            }
            other => panic!("expected Variable, got {other:?}"),
        }
    }
}
