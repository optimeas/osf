// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! OSF block-stream reader.
//!
//! [`BlockReader`] consumes a `R: Read` whose cursor is positioned at
//! the first byte after the metablock and yields one [`Block`] per
//! `next()` call. The block-stream format is documented in
//! `docs/en/osf_general.md` §"Control byte" / "Data structure per
//! control type" and in `osf4.md` / `osf5.md`.
//!
//! Design choices for this layer:
//!
//! - **Iterator-only public API.** `for block in reader { … }` is the
//!   intended consumer pattern. No callback alternative.
//! - **Best-effort on truncation.** A file that ends mid-block —
//!   typical for embedded writers losing power — yields all blocks up
//!   to the last complete one and then `None`. The reader bumps
//!   `stats.blocks_truncated` from 0 to 1 (it is logically capped at 1
//!   because no useful block can follow a partial one) and stops.
//! - **Skip on unsupported.** Channels marked
//!   [`DataType::Unsupported`] or [`ChannelType::Unsupported`] do not
//!   abort the iteration; the reader consumes the payload bytes from
//!   the stream and emits a [`BlockKind::Skipped`] so downstream code
//!   keeps working.
//! - **Skipped payload capture is opt-in.** Default behaviour drops
//!   the bytes without allocation; specialists who need to look at
//!   deprecated `bcMessageEvent` blocks or unknown future types call
//!   [`BlockReader::with_capture_skipped_payload`] to keep them.
//!
//! A future zero-copy `RawBlockReader` could exist as a second layer
//! on top of the same on-disk parsing logic if profiling demands it;
//! it is not part of the current scope.

use crate::block::{
    Block, BlockKind, ControlKind, GpsLocation, NumericPayload, RelTimestampedPayload,
    SkipReason, TimestampedPayload, decode_control_byte,
};
use crate::error::OsfError;
use crate::header::OsfVersion;
use crate::meta::MetaBlock;
use crate::stats::{ChannelStats, ReaderStats};
use crate::types::{ChannelType, DataType};
use byteorder::{LittleEndian, ReadBytesExt};
use log::{debug, warn};
use std::collections::HashMap;
use std::io::Read;
use std::time::Instant;

/// Special channel index that introduces the optional info-data block
/// at the end of an OSF4 file. OSF5 no longer writes it but readers
/// must tolerate it.
const TRAILER_CHANNEL_INDEX: u16 = 0xFFFF;

/// Length of the magic trailer string written after the info block in
/// OSF4 files (`OSF_STREAM_END <pos>===…`). Padded to exactly 40 bytes.
const MAGIC_TRAILER_LEN: usize = 40;

/// Per-channel metadata the reader needs to decode a block: the
/// channel and data types so we know how to route the payload, plus
/// `size_of_length_value` so we know how wide the length prefix is on
/// the wire. Channel name is recorded in [`ChannelStats`] only.
#[derive(Debug, Clone)]
struct ChannelInfo {
    channel_type: ChannelType,
    data_type: DataType,
    size_of_length_value: u8,
}

/// Best-effort iterator over the block stream of an OSF file.
///
/// The reader takes ownership of an `R: Read` whose cursor is right
/// after the metablock, plus the parsed [`MetaBlock`] (so it can
/// resolve channel indices to their per-channel `data_type`,
/// `channel_type`, and `size_of_length_value`).
///
/// Construct with [`BlockReader::new`]. Optional builders:
///
/// - [`Self::with_capture_skipped_payload`] — keep raw bytes of skipped
///   blocks instead of dropping them.
/// - [`Self::with_file_size`] — record the originating file size so
///   downstream stats reporting can show it (not used by the reader
///   itself, but threaded through to [`crate::stats::ReaderStats`]
///   when the convenience wrapper [`crate::read_file`] is used).
pub struct BlockReader<R: Read> {
    inner: R,
    /// OSF file version derived from `meta.file_info.version`. Drives
    /// the version-deterministic null-terminator rule (spec rev
    /// 2026-05-24): OSF4 strips the last byte of every
    /// string/binary AbsTs payload, OSF5 leaves it alone.
    osf_version: OsfVersion,
    channels: HashMap<u16, ChannelInfo>,
    finished: bool,
    capture_skipped: bool,
    started: Instant,
    stats: ReaderStats,
}

impl<R: Read> BlockReader<R> {
    /// Build a reader from an open stream and the parsed metablock.
    ///
    /// The metablock is consumed only for its channel definitions; the
    /// reader keeps a `HashMap<u16, ChannelInfo>` indexed by channel
    /// index for fast lookup during iteration. Per-channel stats are
    /// pre-seeded with the channel name and zero counters.
    pub fn new(inner: R, meta: &MetaBlock) -> Self {
        // Spec rev 2026-05-24 — version-deterministic null-terminator
        // rule. `version == 4` activates the OSF4 strip path; every
        // other value (5, 0, unknown) defaults to OSF5 (no strip).
        // `MetaBlock::default()` yields `version: 0` which the test
        // helpers rely on for OSF5 behaviour.
        let osf_version = match meta.file_info.version {
            4 => OsfVersion::Osf4,
            _ => OsfVersion::Osf5,
        };
        let mut channels = HashMap::with_capacity(meta.channels.len());
        let mut stats = ReaderStats {
            channels_total: meta.channels.len(),
            ..ReaderStats::default()
        };
        for chan in &meta.channels {
            let unsupported = matches!(chan.data_type, DataType::Unsupported(_))
                || matches!(chan.channel_type, ChannelType::Unsupported(_));
            if unsupported {
                stats.channels_unsupported += 1;
            }
            channels.insert(
                chan.index,
                ChannelInfo {
                    channel_type: chan.channel_type.clone(),
                    data_type: chan.data_type.clone(),
                    size_of_length_value: chan.size_of_length_value,
                },
            );
            stats.per_channel.insert(
                chan.index,
                ChannelStats {
                    name: chan.name.clone(),
                    ..ChannelStats::default()
                },
            );
        }
        Self {
            inner,
            osf_version,
            channels,
            finished: false,
            capture_skipped: false,
            started: Instant::now(),
            stats,
        }
    }

    /// Opt in to capturing the raw payload bytes of skipped blocks.
    /// Default is `false` (zero allocation).
    #[must_use]
    pub fn with_capture_skipped_payload(mut self, enabled: bool) -> Self {
        self.capture_skipped = enabled;
        self
    }

    /// Record the file size that produced this stream so consumers
    /// (e.g. the `stats` example) can show it. The reader does not use
    /// the value internally.
    #[must_use]
    pub fn with_file_size(mut self, file_size_bytes: u64) -> Self {
        self.stats.file_size_bytes = Some(file_size_bytes);
        self
    }

    /// Read-only view of the running [`ReaderStats`]. The `elapsed`
    /// field is refreshed on each call.
    #[must_use]
    pub fn stats(&self) -> ReaderStats {
        let mut s = self.stats.clone();
        s.elapsed = self.started.elapsed();
        s.blocks_total = s.blocks_read
            + s.blocks_skipped_unsupported
            + s.blocks_skipped_deprecated_type
            + s.blocks_skipped_reserved_type;
        s.channels_with_data = s
            .per_channel
            .values()
            .filter(|c| c.blocks_read + c.blocks_skipped > 0)
            .count();
        s
    }

    /// Number of blocks the reader could not finish before the stream
    /// ended. Capped at 1 by construction.
    #[must_use]
    pub fn blocks_truncated(&self) -> u64 {
        self.stats.blocks_truncated
    }

    /// `true` if the reader has consumed the optional `0xFFFF`
    /// info-data block.
    #[must_use]
    pub fn trailer_seen(&self) -> bool {
        self.stats.trailer_seen
    }

    /// File size that was supplied via [`Self::with_file_size`], if
    /// any.
    #[must_use]
    pub fn file_size_bytes(&self) -> Option<u64> {
        self.stats.file_size_bytes
    }

    /// Read the per-channel length prefix (2 or 4 bytes, little-endian)
    /// and return it as `u32`. Returns `Ok(None)` if the stream is
    /// truncated mid-prefix; `Ok(Some(_))` on success; `Err` for any
    /// other I/O failure.
    fn read_length_field(&mut self, sizeof: u8) -> Result<Option<u32>, OsfError> {
        match sizeof {
            2 => match self.inner.read_u16::<LittleEndian>() {
                Ok(v) => Ok(Some(u32::from(v))),
                Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => Ok(None),
                Err(e) => Err(e.into()),
            },
            4 => match self.inner.read_u32::<LittleEndian>() {
                Ok(v) => Ok(Some(v)),
                Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => Ok(None),
                Err(e) => Err(e.into()),
            },
            other => Err(OsfError::InvalidMetablock(format!(
                "channel sizeoflengthvalue={other} reached the block reader; \
                 must be 2 or 4 (validated in the metablock parser)"
            ))),
        }
    }

    /// Read exactly `len` bytes into a fresh `Vec`. Returns `Ok(None)`
    /// if the stream is truncated; `Ok(Some(_))` on success; `Err` on
    /// other I/O failures.
    fn read_payload(&mut self, len: usize) -> Result<Option<Vec<u8>>, OsfError> {
        let mut buf = vec![0u8; len];
        match self.inner.read_exact(&mut buf) {
            Ok(()) => Ok(Some(buf)),
            Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => Ok(None),
            Err(e) => Err(e.into()),
        }
    }

    /// Drain `len` bytes from the stream without keeping them. Used
    /// when the caller did not opt in to skipped-payload capture.
    fn drain(&mut self, len: u64) -> Result<bool, OsfError> {
        match std::io::copy(&mut self.inner.by_ref().take(len), &mut std::io::sink()) {
            Ok(actual) if actual == len => Ok(true),
            Ok(_) => Ok(false),
            Err(e) => Err(e.into()),
        }
    }

    /// Handle the optional `0xFFFF` info-data block plus the optional
    /// 40-byte magic trailer. Sets `finished = true` and
    /// `trailer_seen = true` regardless of whether the magic trailer
    /// is present.
    fn consume_trailer(&mut self) -> Result<(), OsfError> {
        // Per spec: [u32 length][u8 control = bcReserved][N bytes payload].
        // The length is always u32 here, NOT the per-channel
        // sizeoflengthvalue. The 2-byte channel index that introduced
        // the trailer was already counted by the caller.
        self.stats.data_section_size_bytes += 4;
        let length = match self.inner.read_u32::<LittleEndian>() {
            Ok(v) => v,
            Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => {
                self.stats.blocks_truncated = self.stats.blocks_truncated.max(1);
                self.finished = true;
                self.stats.trailer_seen = true;
                return Ok(());
            }
            Err(e) => return Err(e.into()),
        };

        // Drain the payload (control byte + info-block bytes).
        if !self.drain(u64::from(length))? {
            self.stats.blocks_truncated = self.stats.blocks_truncated.max(1);
        } else {
            self.stats.data_section_size_bytes += u64::from(length);
        }

        // Best-effort: try to consume the magic trailer if present.
        // Reading it is non-mandatory; if the file ends before then,
        // we simply stop without error.
        let mut tail = [0u8; MAGIC_TRAILER_LEN];
        if let Ok(()) = self.inner.read_exact(&mut tail) {
            self.stats.data_section_size_bytes += MAGIC_TRAILER_LEN as u64;
            if !tail.starts_with(b"OSF_STREAM_END") {
                debug!(
                    "OSF trailer: 40-byte tail did not start with \
                     \"OSF_STREAM_END\"; got {:?}",
                    String::from_utf8_lossy(&tail[..14.min(tail.len())])
                );
            }
        }

        self.stats.trailer_seen = true;
        self.finished = true;
        Ok(())
    }
}

impl<R: Read> Iterator for BlockReader<R> {
    type Item = Result<Block, OsfError>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.finished {
            return None;
        }

        // Step 1: 2-byte channel index, little-endian. Clean EOF here
        // is the regular end of the data section.
        let channel_index = match self.inner.read_u16::<LittleEndian>() {
            Ok(v) => v,
            Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => {
                self.finished = true;
                return None;
            }
            Err(e) => {
                self.finished = true;
                return Some(Err(e.into()));
            }
        };
        self.stats.data_section_size_bytes += 2;

        // Step 2: trailer block (OSF4-only writers, OSF5 readers must
        // tolerate it). Consume and stop without yielding.
        if channel_index == TRAILER_CHANNEL_INDEX {
            if let Err(e) = self.consume_trailer() {
                self.finished = true;
                return Some(Err(e));
            }
            return None;
        }

        // Step 3: look up channel info. An index that is not in the
        // metablock is a corruption signal — we cannot even know how
        // wide the length prefix is supposed to be. Fail hard.
        let info = match self.channels.get(&channel_index).cloned() {
            Some(info) => info,
            None => {
                self.finished = true;
                return Some(Err(OsfError::UnknownChannelIndex(channel_index)));
            }
        };

        // Step 4: per-channel length prefix.
        let length = match self.read_length_field(info.size_of_length_value) {
            Ok(Some(v)) => v,
            Ok(None) => {
                self.stats.blocks_truncated = self.stats.blocks_truncated.max(1);
                self.finished = true;
                return None;
            }
            Err(e) => {
                self.finished = true;
                return Some(Err(e));
            }
        };
        self.stats.data_section_size_bytes += u64::from(info.size_of_length_value);

        if length == 0 {
            warn!(
                "channel {channel_index} produced a zero-length block; \
                 skipping (likely writer bug)"
            );
            self.record_skip(
                channel_index,
                0,
                &SkipReason::ReservedBlockType(0),
            );
            return Some(Ok(Block {
                channel_index,
                kind: BlockKind::Skipped {
                    reason: SkipReason::ReservedBlockType(0),
                    bytes_skipped: 0,
                    payload: None,
                },
            }));
        }

        let length_usize = length as usize;

        // Step 5: forward-compat skip — the channel's data type or
        // channel type is Unsupported. Drain the bytes (or capture
        // them if the caller asked) without trying to parse.
        if let Some(reason) = unsupported_reason(&info) {
            return Some(self.skip_block(channel_index, length_usize, reason));
        }

        // Step 6: pull the full payload into a Vec. Block sizes are
        // bounded by the spec to fit in `length` (u32 max for
        // sizeoflengthvalue=4), so a single allocation is fine.
        let payload = match self.read_payload(length_usize) {
            Ok(Some(v)) => v,
            Ok(None) => {
                self.stats.blocks_truncated = self.stats.blocks_truncated.max(1);
                self.finished = true;
                return None;
            }
            Err(e) => {
                self.finished = true;
                return Some(Err(e));
            }
        };
        self.stats.data_section_size_bytes += u64::from(length);

        // Step 7: decode the control byte and route to the typed
        // parser for the four supported block types. Reserved /
        // deprecated / unknown values become Skipped; the stream
        // cursor is already past the payload so the next next() call
        // sees the next block.
        let (control_byte_ref, body) =
            payload.split_first().expect("length > 0 guaranteed above");
        let control_byte = *control_byte_ref;
        let control = decode_control_byte(control_byte);

        let bytes_skipped = u64::from(length);
        let payload_field = if self.capture_skipped {
            Some(body.to_vec())
        } else {
            None
        };

        let block_kind = match control.kind {
            ControlKind::Reserved | ControlKind::TimebaseRealign => {
                let reason = SkipReason::ReservedBlockType(control_byte & 0x7F);
                self.record_skip(channel_index, length, &reason);
                BlockKind::Skipped {
                    reason,
                    bytes_skipped,
                    payload: payload_field,
                }
            }
            ControlKind::TrustedTimestamp
            | ControlKind::StatusEvent
            | ControlKind::MessageEvent => {
                let reason = SkipReason::DeprecatedBlockType(control_byte & 0x7F);
                self.record_skip(channel_index, length, &reason);
                BlockKind::Skipped {
                    reason,
                    bytes_skipped,
                    payload: payload_field,
                }
            }
            ControlKind::Unknown(raw) => {
                let reason = SkipReason::ReservedBlockType(raw);
                self.record_skip(channel_index, length, &reason);
                BlockKind::Skipped {
                    reason,
                    bytes_skipped,
                    payload: payload_field,
                }
            }
            ControlKind::StartData => {
                match parse_start_data(body, &info.data_type, control.multi_sample) {
                    Ok((ts, rate, samples)) => {
                        let n = samples.len() as u64;
                        let chan_stats =
                            self.stats.per_channel.entry(channel_index).or_default();
                        chan_stats.blocks_read += 1;
                        chan_stats.bytes_payload += u64::from(length);
                        chan_stats.samples_total += n;
                        chan_stats.segments += 1;
                        chan_stats.observe_timestamp(ts);
                        if n > 0 && rate > 0.0 {
                            // Last sample timestamp = ts + (n-1) / rate × 1e9 ns.
                            let last = ts.saturating_add(
                                (((n - 1) as f64 / rate) * 1.0e9) as i64,
                            );
                            chan_stats.observe_timestamp(last);
                        }
                        self.stats.blocks_read += 1;
                        BlockKind::StartData {
                            start_timestamp_ns: ts,
                            sample_rate_hz: rate,
                            samples,
                        }
                    }
                    Err(e) => return Some(Err(e)),
                }
            }
            ControlKind::ContinuedData => {
                match parse_continued_data(body, &info.data_type, control.multi_sample) {
                    Ok(samples) => {
                        let n = samples.len() as u64;
                        let chan_stats =
                            self.stats.per_channel.entry(channel_index).or_default();
                        chan_stats.blocks_read += 1;
                        chan_stats.bytes_payload += u64::from(length);
                        chan_stats.samples_total += n;
                        self.stats.blocks_read += 1;
                        BlockKind::ContinuedData { samples }
                    }
                    Err(e) => return Some(Err(e)),
                }
            }
            ControlKind::AbsTimeStampData => {
                match parse_abs_timestamp_data(
                    body,
                    &info.data_type,
                    control.multi_sample,
                    self.osf_version,
                ) {
                    Ok(samples) => {
                        let chan_stats =
                            self.stats.per_channel.entry(channel_index).or_default();
                        chan_stats.blocks_read += 1;
                        chan_stats.bytes_payload += u64::from(length);
                        chan_stats.samples_total += samples.len() as u64;
                        if let Some((first, last)) = abs_timestamp_range(&samples) {
                            chan_stats.observe_timestamp(first);
                            chan_stats.observe_timestamp(last);
                        }
                        self.stats.blocks_read += 1;
                        BlockKind::AbsTimestampData { samples }
                    }
                    Err(e) => return Some(Err(e)),
                }
            }
            ControlKind::ContinuedRelStampData => {
                match parse_continued_rel_stamp_data(body, &info.data_type, control.multi_sample) {
                    Ok(samples) => {
                        let n = samples.len() as u64;
                        let chan_stats =
                            self.stats.per_channel.entry(channel_index).or_default();
                        chan_stats.blocks_read += 1;
                        chan_stats.bytes_payload += u64::from(length);
                        chan_stats.samples_total += n;
                        self.stats.blocks_read += 1;
                        BlockKind::ContinuedRelStampData { samples }
                    }
                    Err(e) => return Some(Err(e)),
                }
            }
        };

        Some(Ok(Block {
            channel_index,
            kind: block_kind,
        }))
    }
}

impl<R: Read> BlockReader<R> {
    /// Helper for steps that need to drain the payload (or capture it)
    /// without ever decoding the control byte. Used for forward-compat
    /// skips of `Unsupported` channels.
    fn skip_block(
        &mut self,
        channel_index: u16,
        length: usize,
        reason: SkipReason,
    ) -> Result<Block, OsfError> {
        if self.capture_skipped {
            match self.read_payload(length)? {
                Some(buf) => {
                    self.stats.data_section_size_bytes += length as u64;
                    self.record_skip(channel_index, length as u32, &reason);
                    Ok(Block {
                        channel_index,
                        kind: BlockKind::Skipped {
                            reason,
                            bytes_skipped: length as u64,
                            payload: Some(buf),
                        },
                    })
                }
                None => {
                    self.stats.blocks_truncated = self.stats.blocks_truncated.max(1);
                    self.finished = true;
                    Err(OsfError::Io(std::io::Error::new(
                        std::io::ErrorKind::UnexpectedEof,
                        "stream truncated mid-skip-payload",
                    )))
                }
            }
        } else {
            if !self.drain(length as u64)? {
                self.stats.blocks_truncated = self.stats.blocks_truncated.max(1);
                self.finished = true;
                return Err(OsfError::Io(std::io::Error::new(
                    std::io::ErrorKind::UnexpectedEof,
                    "stream truncated mid-skip-payload",
                )));
            }
            self.stats.data_section_size_bytes += length as u64;
            self.record_skip(channel_index, length as u32, &reason);
            Ok(Block {
                channel_index,
                kind: BlockKind::Skipped {
                    reason,
                    bytes_skipped: length as u64,
                    payload: None,
                },
            })
        }
    }

    /// Centralised counter bump for every skip path. Updates the
    /// reason-specific top-level counter as well as the per-channel
    /// `blocks_skipped` and `bytes_payload` totals.
    fn record_skip(&mut self, channel_index: u16, length: u32, reason: &SkipReason) {
        match reason {
            SkipReason::UnsupportedDataType | SkipReason::UnsupportedChannelType => {
                self.stats.blocks_skipped_unsupported += 1;
            }
            SkipReason::DeprecatedBlockType(_) => {
                self.stats.blocks_skipped_deprecated_type += 1;
            }
            SkipReason::ReservedBlockType(_) => {
                self.stats.blocks_skipped_reserved_type += 1;
            }
        }
        let entry = self.stats.per_channel.entry(channel_index).or_default();
        entry.blocks_skipped += 1;
        entry.bytes_payload += u64::from(length);
    }
}

/// Extract `(first_timestamp, last_timestamp)` from a typed AbsTs
/// payload by inspecting the first and last sample of every variant.
fn abs_timestamp_range(payload: &TimestampedPayload) -> Option<(i64, i64)> {
    macro_rules! range_of {
        ($v:expr) => {{
            let v = $v;
            if v.is_empty() {
                None
            } else {
                Some((v.first().unwrap().0, v.last().unwrap().0))
            }
        }};
    }
    match payload {
        TimestampedPayload::Bool(v) => range_of!(v),
        TimestampedPayload::Int8(v) => range_of!(v),
        TimestampedPayload::Int16(v) => range_of!(v),
        TimestampedPayload::Int32(v) => range_of!(v),
        TimestampedPayload::Int64(v) => range_of!(v),
        TimestampedPayload::UInt8(v) => range_of!(v),
        TimestampedPayload::UInt16(v) => range_of!(v),
        TimestampedPayload::UInt32(v) => range_of!(v),
        TimestampedPayload::UInt64(v) => range_of!(v),
        TimestampedPayload::Float(v) => range_of!(v),
        TimestampedPayload::Double(v) => range_of!(v),
        TimestampedPayload::String(v) => range_of!(v),
        TimestampedPayload::Binary(v) => range_of!(v),
        TimestampedPayload::GpsLocation(v) => range_of!(v),
    }
}

fn unsupported_reason(info: &ChannelInfo) -> Option<SkipReason> {
    if matches!(info.data_type, DataType::Unsupported(_)) {
        return Some(SkipReason::UnsupportedDataType);
    }
    if matches!(info.channel_type, ChannelType::Unsupported(_)) {
        return Some(SkipReason::UnsupportedChannelType);
    }
    None
}

// ---------------------------------------------------------------
// Payload parsers. All multi-byte integers are little-endian.
//
// The functions take the body slice (everything *after* the control
// byte) plus the channel's data type and the multi-sample flag from
// bit 7 of the control byte. They return typed payload structs.
//
// Sample-count `N` semantics:
// - `multi_sample = false`: implicit N = 1, no `[u32 N]` prefix.
// - `multi_sample = true`: explicit `[u32 N]` prefix.
// ---------------------------------------------------------------

fn parse_start_data(
    body: &[u8],
    dt: &DataType,
    multi: bool,
) -> Result<(i64, f64, NumericPayload), OsfError> {
    let mut cur = body;
    let start_timestamp_ns = cur
        .read_i64::<LittleEndian>()
        .map_err(|e| invalid_block(format!("StartData timestamp: {e}")))?;
    let sample_rate_hz = cur
        .read_f64::<LittleEndian>()
        .map_err(|e| invalid_block(format!("StartData sample rate: {e}")))?;
    let n = read_sample_count(&mut cur, multi)?;
    let samples = read_numeric_n(&mut cur, dt, n)?;
    Ok((start_timestamp_ns, sample_rate_hz, samples))
}

fn parse_continued_data(
    body: &[u8],
    dt: &DataType,
    multi: bool,
) -> Result<NumericPayload, OsfError> {
    let mut cur = body;
    let n = read_sample_count(&mut cur, multi)?;
    read_numeric_n(&mut cur, dt, n)
}

fn parse_abs_timestamp_data(
    body: &[u8],
    dt: &DataType,
    multi: bool,
    osf_version: OsfVersion,
) -> Result<TimestampedPayload, OsfError> {
    // String / binary: the spec accepts both forms — bit-7 = 0 with
    // implicit N=1 (the canonical compact form, 4 bytes shorter) and
    // bit-7 = 1 with an explicit [u32 N] prefix. The Rust writer and
    // C++ Phase 7a encoder both emit the canonical bit-7 = 0 form for
    // single-sample blocks. We accept either on input.
    if matches!(dt, DataType::String | DataType::Binary) {
        return parse_abs_timestamp_string_or_binary(body, dt, multi, osf_version);
    }
    if matches!(dt, DataType::ByteArray) {
        // Reader normalises bytearray -> Binary in the metablock parser,
        // so this branch is theoretically unreachable. Defensive only.
        return parse_abs_timestamp_string_or_binary(body, &DataType::Binary, multi, osf_version);
    }

    let mut cur = body;
    let n = read_sample_count(&mut cur, multi)?;

    macro_rules! read_pairs {
        ($variant:ident, $reader:expr, $len:expr) => {{
            let mut v = Vec::with_capacity(n);
            for _ in 0..n {
                let ts = cur
                    .read_i64::<LittleEndian>()
                    .map_err(|e| invalid_block(format!("AbsTs ts: {e}")))?;
                let value = $reader(&mut cur)
                    .map_err(|e| invalid_block(format!("AbsTs value ({}): {e}", $len)))?;
                v.push((ts, value));
            }
            TimestampedPayload::$variant(v)
        }};
    }

    let payload = match dt {
        DataType::Bool => read_pairs!(Bool, |c: &mut &[u8]| c.read_u8().map(|b| b != 0), 1usize),
        DataType::Int8 => read_pairs!(Int8, |c: &mut &[u8]| c.read_i8(), 1usize),
        DataType::Int16 => read_pairs!(Int16, |c: &mut &[u8]| c.read_i16::<LittleEndian>(), 2usize),
        DataType::Int32 => read_pairs!(Int32, |c: &mut &[u8]| c.read_i32::<LittleEndian>(), 4usize),
        DataType::Int64 => read_pairs!(Int64, |c: &mut &[u8]| c.read_i64::<LittleEndian>(), 8usize),
        DataType::UInt8 => read_pairs!(UInt8, |c: &mut &[u8]| c.read_u8(), 1usize),
        DataType::UInt16 => {
            read_pairs!(UInt16, |c: &mut &[u8]| c.read_u16::<LittleEndian>(), 2usize)
        }
        DataType::UInt32 => {
            read_pairs!(UInt32, |c: &mut &[u8]| c.read_u32::<LittleEndian>(), 4usize)
        }
        DataType::UInt64 => {
            read_pairs!(UInt64, |c: &mut &[u8]| c.read_u64::<LittleEndian>(), 8usize)
        }
        DataType::Float => read_pairs!(Float, |c: &mut &[u8]| c.read_f32::<LittleEndian>(), 4usize),
        DataType::Double => {
            read_pairs!(Double, |c: &mut &[u8]| c.read_f64::<LittleEndian>(), 8usize)
        }
        DataType::GpsLocation => {
            let mut v = Vec::with_capacity(n);
            for _ in 0..n {
                let ts = cur
                    .read_i64::<LittleEndian>()
                    .map_err(|e| invalid_block(format!("AbsTs gps ts: {e}")))?;
                let gps = read_gps_location(&mut cur)?;
                v.push((ts, gps));
            }
            TimestampedPayload::GpsLocation(v)
        }
        DataType::String | DataType::Binary | DataType::ByteArray => {
            unreachable!("string/binary handled at top of function")
        }
        DataType::Unsupported(_) => {
            // Should already have been routed to Skipped in next();
            // defensive fallback.
            return Err(invalid_block(
                "AbsTimeStampData on Unsupported channel reached the typed parser".into(),
            ));
        }
    };
    Ok(payload)
}

/// Handle `bcAbsTimeStampData` for `string` and `binary`. Per spec the
/// multi-sample bit is always set; we additionally tolerate
/// `multi == false` as an implicit `N=1`. With `N>1` the spec mandates
/// equal-length segments — we split the body equally and parse each
/// chunk. If the body length is not divisible by `N`, we log a warn
/// and fall back to a single-sample interpretation rather than failing.
///
/// The null-terminator handling is version-deterministic (spec rev
/// 2026-05-24): OSF4 strips the last byte of every per-sample payload
/// unconditionally; OSF5 keeps the payload verbatim.
fn parse_abs_timestamp_string_or_binary(
    body: &[u8],
    dt: &DataType,
    multi: bool,
    osf_version: OsfVersion,
) -> Result<TimestampedPayload, OsfError> {
    let (n, rest) = if multi {
        let mut cur = body;
        let raw = cur
            .read_u32::<LittleEndian>()
            .map_err(|e| invalid_block(format!("AbsTs string/binary N: {e}")))?;
        if raw == 0 {
            return build_string_or_binary(dt, Vec::new());
        }
        (raw as usize, cur)
    } else {
        warn!(
            "bcAbsTimeStampData for {dt:?} found with bit 7 clear; \
             spec mandates bit 7 set. Treating as implicit N=1."
        );
        (1usize, body)
    };

    if n == 1 {
        let mut cur = rest;
        let ts = cur
            .read_i64::<LittleEndian>()
            .map_err(|e| invalid_block(format!("AbsTs string/binary ts: {e}")))?;
        let payload = strip_osf4_terminator(cur, osf_version);
        return build_string_or_binary(dt, vec![(ts, payload.to_vec())]);
    }

    // N > 1: equal-length segments.
    let total = rest.len();
    if total % n != 0 {
        warn!(
            "bcAbsTimeStampData for {dt:?} with N={n} has body length {total} \
             that is not divisible by N; falling back to single-sample parse"
        );
        let mut cur = rest;
        let ts = cur
            .read_i64::<LittleEndian>()
            .map_err(|e| invalid_block(format!("AbsTs string/binary ts: {e}")))?;
        let payload = strip_osf4_terminator(cur, osf_version);
        return build_string_or_binary(dt, vec![(ts, payload.to_vec())]);
    }

    let per_sample = total / n;
    // OSF4 needs i64 ts (8) + at least one byte (terminator alone is
    // legal for an empty payload) = 9. OSF5 needs only i64 ts = 8.
    let min_per_sample = match osf_version {
        OsfVersion::Osf4 => 9usize,
        OsfVersion::Osf5 => 8usize,
    };
    if per_sample < min_per_sample {
        return Err(invalid_block(format!(
            "bcAbsTimeStampData for {dt:?} N={n}: per-sample size {per_sample} \
             is less than {min_per_sample} (need i64 ts{})",
            if osf_version == OsfVersion::Osf4 {
                " + at least the OSF4 null terminator"
            } else {
                ""
            }
        )));
    }
    let mut samples = Vec::with_capacity(n);
    for i in 0..n {
        let chunk = &rest[i * per_sample..(i + 1) * per_sample];
        let mut chunk_cur = chunk;
        let ts = chunk_cur
            .read_i64::<LittleEndian>()
            .map_err(|e| invalid_block(format!("AbsTs string/binary ts (chunk {i}): {e}")))?;
        let payload = strip_osf4_terminator(chunk_cur, osf_version);
        samples.push((ts, payload.to_vec()));
    }
    build_string_or_binary(dt, samples)
}

fn build_string_or_binary(
    dt: &DataType,
    samples: Vec<(i64, Vec<u8>)>,
) -> Result<TimestampedPayload, OsfError> {
    match dt {
        DataType::String => {
            let mut decoded = Vec::with_capacity(samples.len());
            for (ts, bytes) in samples {
                let s = String::from_utf8(bytes).map_err(|e| {
                    invalid_block(format!("AbsTs string is not valid UTF-8: {e}"))
                })?;
                decoded.push((ts, s));
            }
            Ok(TimestampedPayload::String(decoded))
        }
        DataType::Binary | DataType::ByteArray => Ok(TimestampedPayload::Binary(samples)),
        other => Err(invalid_block(format!(
            "build_string_or_binary called with non-string/binary datatype {other:?}"
        ))),
    }
}

/// Spec rev 2026-05-24 — version-deterministic null-terminator rule.
///
/// - OSF4: strip the last byte of `bytes` unconditionally. The byte is
///   guaranteed to be `0x00` per spec; if a writer is non-conforming
///   and emits a non-zero last byte, that byte is silently discarded.
///   The reader does not validate it because the rule is deterministic
///   and there is no fallback path to take.
/// - OSF5: return `bytes` unchanged. A trailing `0x00` is a regular
///   data byte, not a sentinel.
fn strip_osf4_terminator(bytes: &[u8], osf_version: OsfVersion) -> &[u8] {
    if osf_version == OsfVersion::Osf4 && !bytes.is_empty() {
        return &bytes[..bytes.len() - 1];
    }
    bytes
}

fn parse_continued_rel_stamp_data(
    body: &[u8],
    dt: &DataType,
    multi: bool,
) -> Result<RelTimestampedPayload, OsfError> {
    let mut cur = body;
    let n = read_sample_count(&mut cur, multi)?;

    macro_rules! read_rel_pairs {
        ($variant:ident, $reader:expr) => {{
            let mut v = Vec::with_capacity(n);
            for _ in 0..n {
                let dt_ns = cur
                    .read_u32::<LittleEndian>()
                    .map_err(|e| invalid_block(format!("RelTs delta: {e}")))?;
                let value = $reader(&mut cur)
                    .map_err(|e| invalid_block(format!("RelTs value: {e}")))?;
                v.push((dt_ns, value));
            }
            RelTimestampedPayload::$variant(v)
        }};
    }

    let payload = match dt {
        DataType::Bool => read_rel_pairs!(Bool, |c: &mut &[u8]| c.read_u8().map(|b| b != 0)),
        DataType::Int8 => read_rel_pairs!(Int8, |c: &mut &[u8]| c.read_i8()),
        DataType::Int16 => read_rel_pairs!(Int16, |c: &mut &[u8]| c.read_i16::<LittleEndian>()),
        DataType::Int32 => read_rel_pairs!(Int32, |c: &mut &[u8]| c.read_i32::<LittleEndian>()),
        DataType::Int64 => read_rel_pairs!(Int64, |c: &mut &[u8]| c.read_i64::<LittleEndian>()),
        DataType::UInt8 => read_rel_pairs!(UInt8, |c: &mut &[u8]| c.read_u8()),
        DataType::UInt16 => read_rel_pairs!(UInt16, |c: &mut &[u8]| c.read_u16::<LittleEndian>()),
        DataType::UInt32 => read_rel_pairs!(UInt32, |c: &mut &[u8]| c.read_u32::<LittleEndian>()),
        DataType::UInt64 => read_rel_pairs!(UInt64, |c: &mut &[u8]| c.read_u64::<LittleEndian>()),
        DataType::Float => read_rel_pairs!(Float, |c: &mut &[u8]| c.read_f32::<LittleEndian>()),
        DataType::Double => read_rel_pairs!(Double, |c: &mut &[u8]| c.read_f64::<LittleEndian>()),
        other => {
            return Err(invalid_block(format!(
                "bcContinuedRelStampData not allowed for datatype {other:?}"
            )));
        }
    };
    Ok(payload)
}

/// Read a numeric sample run of length `n` from `cur`, building the
/// matching [`NumericPayload`] variant. Numeric data types only —
/// equidistant blocks (`bcStartData`, `bcContinuedData`) reject the
/// non-numeric types per spec.
fn read_numeric_n(
    cur: &mut &[u8],
    dt: &DataType,
    n: usize,
) -> Result<NumericPayload, OsfError> {
    macro_rules! read_run {
        ($variant:ident, $reader:expr) => {{
            let mut v = Vec::with_capacity(n);
            for _ in 0..n {
                v.push($reader(cur).map_err(|e| invalid_block(format!("numeric run: {e}")))?);
            }
            NumericPayload::$variant(v)
        }};
    }

    let payload = match dt {
        DataType::Bool => read_run!(Bool, |c: &mut &[u8]| c.read_u8().map(|b| b != 0)),
        DataType::Int8 => read_run!(Int8, |c: &mut &[u8]| c.read_i8()),
        DataType::Int16 => read_run!(Int16, |c: &mut &[u8]| c.read_i16::<LittleEndian>()),
        DataType::Int32 => read_run!(Int32, |c: &mut &[u8]| c.read_i32::<LittleEndian>()),
        DataType::Int64 => read_run!(Int64, |c: &mut &[u8]| c.read_i64::<LittleEndian>()),
        DataType::UInt8 => read_run!(UInt8, |c: &mut &[u8]| c.read_u8()),
        DataType::UInt16 => read_run!(UInt16, |c: &mut &[u8]| c.read_u16::<LittleEndian>()),
        DataType::UInt32 => read_run!(UInt32, |c: &mut &[u8]| c.read_u32::<LittleEndian>()),
        DataType::UInt64 => read_run!(UInt64, |c: &mut &[u8]| c.read_u64::<LittleEndian>()),
        DataType::Float => read_run!(Float, |c: &mut &[u8]| c.read_f32::<LittleEndian>()),
        DataType::Double => read_run!(Double, |c: &mut &[u8]| c.read_f64::<LittleEndian>()),
        other => {
            return Err(invalid_block(format!(
                "equidistant blocks (bcStartData / bcContinuedData) only support \
                 numeric datatypes; got {other:?}"
            )));
        }
    };
    Ok(payload)
}

fn read_sample_count(cur: &mut &[u8], multi: bool) -> Result<usize, OsfError> {
    if !multi {
        return Ok(1);
    }
    let n = cur
        .read_u32::<LittleEndian>()
        .map_err(|e| invalid_block(format!("multi-sample N: {e}")))?;
    Ok(n as usize)
}

fn read_gps_location(cur: &mut &[u8]) -> Result<GpsLocation, OsfError> {
    let latitude = cur
        .read_f64::<LittleEndian>()
        .map_err(|e| invalid_block(format!("gps latitude: {e}")))?;
    let longitude = cur
        .read_f64::<LittleEndian>()
        .map_err(|e| invalid_block(format!("gps longitude: {e}")))?;
    let altitude = cur
        .read_f64::<LittleEndian>()
        .map_err(|e| invalid_block(format!("gps altitude: {e}")))?;
    Ok(GpsLocation {
        latitude,
        longitude,
        altitude,
    })
}

fn invalid_block(msg: String) -> OsfError {
    OsfError::InvalidBlock(msg)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::meta::{Channel, MetaBlock};
    use std::io::Cursor;

    fn channel(index: u16, data_type: DataType, size: u8) -> Channel {
        Channel {
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

    fn make_meta(channels: Vec<Channel>) -> MetaBlock {
        MetaBlock {
            file_info: Default::default(),
            channels,
            infos: Vec::new(),
        }
    }

    fn make_meta_v4(channels: Vec<Channel>) -> MetaBlock {
        MetaBlock {
            file_info: crate::meta::FileInfo {
                version: 4,
                ..Default::default()
            },
            channels,
            infos: Vec::new(),
        }
    }

    #[test]
    fn empty_stream_yields_none_without_error() {
        let meta = make_meta(vec![]);
        let mut r = BlockReader::new(Cursor::new(Vec::<u8>::new()), &meta);
        assert!(r.next().is_none());
        assert_eq!(r.blocks_truncated(), 0);
    }

    #[test]
    fn truncation_in_channel_index_is_silent_eof() {
        let meta = make_meta(vec![channel(0, DataType::Int16, 2)]);
        // single byte — can't form a u16 channel index.
        let mut r = BlockReader::new(Cursor::new(vec![0u8]), &meta);
        assert!(r.next().is_none());
    }

    #[test]
    fn truncation_in_length_field_bumps_counter() {
        let meta = make_meta(vec![channel(0, DataType::Int16, 4)]);
        // channel index 0, then only 2 bytes of a 4-byte length field.
        let mut r = BlockReader::new(Cursor::new(vec![0, 0, 1, 0]), &meta);
        assert!(r.next().is_none());
        assert_eq!(r.blocks_truncated(), 1);
    }

    #[test]
    fn truncation_mid_payload_bumps_counter() {
        let meta = make_meta(vec![channel(0, DataType::Int16, 2)]);
        // channel 0, length=10 (u16), but only 5 bytes follow.
        let mut bytes = vec![0u8, 0, 10, 0];
        bytes.extend_from_slice(&[8, 0, 0, 0, 0]);
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        assert!(r.next().is_none());
        assert_eq!(r.blocks_truncated(), 1);
    }

    #[test]
    fn unknown_channel_index_is_hard_error() {
        let meta = make_meta(vec![channel(0, DataType::Int16, 2)]);
        // channel 7 is not in the metablock.
        let bytes = vec![7u8, 0, 1, 0, 0];
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let err = r.next().unwrap().unwrap_err();
        assert!(matches!(err, OsfError::UnknownChannelIndex(7)), "got {err:?}");
    }

    #[test]
    fn unsupported_data_type_yields_skipped_with_drained_bytes() {
        let meta = make_meta(vec![channel(
            0,
            DataType::Unsupported("future_xy".into()),
            2,
        )]);
        // Two blocks back-to-back; the first must be skipped, the
        // second must be reachable.
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&[0, 0, 5, 0, 0xAA, 1, 2, 3, 4]); // 5 bytes payload
        bytes.extend_from_slice(&[0, 0, 5, 0, 0xAA, 5, 6, 7, 8]);
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);

        let first = r.next().unwrap().unwrap();
        assert_eq!(first.channel_index, 0);
        match first.kind {
            BlockKind::Skipped {
                reason,
                bytes_skipped,
                payload,
            } => {
                assert_eq!(reason, SkipReason::UnsupportedDataType);
                assert_eq!(bytes_skipped, 5);
                assert!(payload.is_none(), "default capture is off");
            }
            other => panic!("expected Skipped, got {other:?}"),
        }

        let second = r.next().unwrap().unwrap();
        assert_eq!(second.channel_index, 0); // stream still aligned
    }

    #[test]
    fn capture_skipped_payload_keeps_body_bytes() {
        let meta = make_meta(vec![channel(0, DataType::Int16, 2)]);
        // Reserved control byte 0 → Skipped; body is 2 bytes after
        // control.
        let bytes = vec![0u8, 0, 3, 0, 0x00, 0xAA, 0xBB];
        let mut r =
            BlockReader::new(Cursor::new(bytes), &meta).with_capture_skipped_payload(true);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::Skipped {
                reason,
                bytes_skipped,
                payload,
            } => {
                assert_eq!(reason, SkipReason::ReservedBlockType(0));
                assert_eq!(bytes_skipped, 3);
                assert_eq!(payload.as_deref(), Some(&[0xAA, 0xBB][..]));
            }
            other => panic!("expected Skipped, got {other:?}"),
        }
    }

    #[test]
    fn deprecated_control_bytes_route_to_deprecated_skip_reason() {
        let meta = make_meta(vec![channel(0, DataType::Int16, 2)]);
        for raw in [1u8, 3, 4] {
            let bytes = vec![0u8, 0, 1, 0, raw];
            let mut r = BlockReader::new(Cursor::new(bytes), &meta);
            let blk = r.next().unwrap().unwrap();
            match blk.kind {
                BlockKind::Skipped { reason, .. } => {
                    assert_eq!(
                        reason,
                        SkipReason::DeprecatedBlockType(raw),
                        "deprecated byte {raw}"
                    );
                }
                other => panic!("expected Skipped, got {other:?}"),
            }
        }
    }

    #[test]
    fn unknown_high_control_byte_routes_to_reserved_skip_reason() {
        let meta = make_meta(vec![channel(0, DataType::Int16, 2)]);
        let bytes = vec![0u8, 0, 1, 0, 0x55];
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::Skipped { reason, .. } => {
                assert_eq!(reason, SkipReason::ReservedBlockType(0x55));
            }
            other => panic!("expected Skipped, got {other:?}"),
        }
    }

    #[test]
    fn parses_single_sample_abs_timestamp_int64() {
        // Layout for the actual osf5_scalar_int64.osf first block:
        // [u16 0][u16 17][u8 0x08][i64 ts][i64 0]
        let meta = make_meta(vec![channel(0, DataType::Int64, 2)]);
        let mut bytes = vec![0u8, 0, 17, 0, 0x08];
        bytes.extend_from_slice(&0x18AC_BBA9_5F76_EC57i64.to_le_bytes());
        bytes.extend_from_slice(&0i64.to_le_bytes());
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::AbsTimestampData {
                samples: TimestampedPayload::Int64(v),
            } => {
                assert_eq!(v.len(), 1);
                assert_eq!(v[0].0, 0x18AC_BBA9_5F76_EC57);
                assert_eq!(v[0].1, 0);
            }
            other => panic!("expected AbsTimestampData/Int64, got {other:?}"),
        }
    }

    #[test]
    fn parses_multi_sample_abs_timestamp_double() {
        // [u16 0][u16 N=2 → length=29][u8 0x88 (multi)][u32 N=2]
        // [i64 ts][f64][i64 ts][f64]
        let meta = make_meta(vec![channel(0, DataType::Double, 2)]);
        let payload_len = 1 + 4 + 2 * (8 + 8); // 1 ctl + 4 N + 2 × (ts + value)
        let mut bytes = vec![0u8, 0];
        bytes.extend_from_slice(&u16::try_from(payload_len).unwrap().to_le_bytes());
        bytes.push(0x88);
        bytes.extend_from_slice(&2u32.to_le_bytes());
        bytes.extend_from_slice(&100i64.to_le_bytes());
        bytes.extend_from_slice(&1.5f64.to_le_bytes());
        bytes.extend_from_slice(&200i64.to_le_bytes());
        bytes.extend_from_slice(&2.5f64.to_le_bytes());
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::AbsTimestampData {
                samples: TimestampedPayload::Double(v),
            } => {
                assert_eq!(v, vec![(100, 1.5), (200, 2.5)]);
            }
            other => panic!("expected AbsTimestampData/Double, got {other:?}"),
        }
    }

    #[test]
    fn parses_start_data_single_sample_double() {
        // [u16 0][u16 length=17][u8 0x06][i64 ts][f64 rate][f64 value]
        // length = 1 ctl + 8 ts + 8 rate + 8 value = 25
        let meta = make_meta(vec![channel(0, DataType::Double, 2)]);
        let mut bytes = vec![0u8, 0, 25, 0, 0x06];
        bytes.extend_from_slice(&1_000_000i64.to_le_bytes());
        bytes.extend_from_slice(&1000.0f64.to_le_bytes());
        bytes.extend_from_slice(&2.5f64.to_le_bytes());
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::StartData {
                start_timestamp_ns,
                sample_rate_hz,
                samples: NumericPayload::Double(v),
            } => {
                assert_eq!(start_timestamp_ns, 1_000_000);
                assert!((sample_rate_hz - 1000.0).abs() < 1e-9);
                assert_eq!(v, vec![2.5]);
            }
            other => panic!("expected StartData/Double, got {other:?}"),
        }
    }

    #[test]
    fn parses_start_data_multi_sample_float_n10() {
        // [u16 0][u16 length][u8 0x86][i64 ts][f64 rate][u32 N=10][10×f32]
        let meta = make_meta(vec![channel(0, DataType::Float, 2)]);
        let n: u32 = 10;
        let payload_len = 1 + 8 + 8 + 4 + (n as usize) * 4;
        let mut bytes = vec![0u8, 0];
        bytes.extend_from_slice(&u16::try_from(payload_len).unwrap().to_le_bytes());
        bytes.push(0x86);
        bytes.extend_from_slice(&7i64.to_le_bytes());
        bytes.extend_from_slice(&500.0f64.to_le_bytes());
        bytes.extend_from_slice(&n.to_le_bytes());
        for i in 0..n {
            bytes.extend_from_slice(&(i as f32).to_le_bytes());
        }
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::StartData {
                samples: NumericPayload::Float(v),
                ..
            } => {
                assert_eq!(v.len(), 10);
                assert_eq!(v[3], 3.0);
            }
            other => panic!("expected StartData/Float, got {other:?}"),
        }
    }

    #[test]
    fn parses_continued_data_int16_multi() {
        // [u16 0][u16 length][u8 0x85][u32 N=4][4×i16]
        let meta = make_meta(vec![channel(0, DataType::Int16, 2)]);
        let n: u32 = 4;
        let payload_len = 1 + 4 + (n as usize) * 2;
        let mut bytes = vec![0u8, 0];
        bytes.extend_from_slice(&u16::try_from(payload_len).unwrap().to_le_bytes());
        bytes.push(0x85);
        bytes.extend_from_slice(&n.to_le_bytes());
        for i in 0..n as i16 {
            bytes.extend_from_slice(&(10 * i).to_le_bytes());
        }
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::ContinuedData {
                samples: NumericPayload::Int16(v),
            } => {
                assert_eq!(v, vec![0, 10, 20, 30]);
            }
            other => panic!("expected ContinuedData/Int16, got {other:?}"),
        }
    }

    #[test]
    fn parses_abs_timestamp_string_osf5_keeps_payload_verbatim() {
        // OSF5 reader: payload bytes are kept verbatim, no terminator
        // stripping. [u16 0][u32 length][u8 0x88][u32 N=1][i64 ts][b"hi"]
        let meta = make_meta(vec![channel(0, DataType::String, 4)]);
        let body_string = b"hi";
        let payload_len = 1 + 4 + 8 + body_string.len();
        let mut bytes = vec![0u8, 0];
        bytes.extend_from_slice(&u32::try_from(payload_len).unwrap().to_le_bytes());
        bytes.push(0x88);
        bytes.extend_from_slice(&1u32.to_le_bytes());
        bytes.extend_from_slice(&42i64.to_le_bytes());
        bytes.extend_from_slice(body_string);
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::AbsTimestampData {
                samples: TimestampedPayload::String(v),
            } => {
                assert_eq!(v, vec![(42, "hi".to_string())]);
            }
            other => panic!("expected AbsTimestampData/String, got {other:?}"),
        }
    }

    #[test]
    fn parses_abs_timestamp_string_osf4_strips_last_byte() {
        // OSF4 reader: the spec-mandated trailing 0x00 is stripped
        // unconditionally before the payload reaches the manager.
        // [u16 0][u32 length][u8 0x88][u32 N=1][i64 ts][b"hi" + 0x00]
        let meta = make_meta_v4(vec![channel(0, DataType::String, 4)]);
        let body_string = b"hi\x00";
        let payload_len = 1 + 4 + 8 + body_string.len();
        let mut bytes = vec![0u8, 0];
        bytes.extend_from_slice(&u32::try_from(payload_len).unwrap().to_le_bytes());
        bytes.push(0x88);
        bytes.extend_from_slice(&1u32.to_le_bytes());
        bytes.extend_from_slice(&42i64.to_le_bytes());
        bytes.extend_from_slice(body_string);
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::AbsTimestampData {
                samples: TimestampedPayload::String(v),
            } => {
                assert_eq!(v, vec![(42, "hi".to_string())]);
            }
            other => panic!("expected AbsTimestampData/String, got {other:?}"),
        }
    }

    #[test]
    fn parses_abs_timestamp_binary_osf5_keeps_trailing_null_byte() {
        // OSF5 reader: a trailing 0x00 in a binary payload is a real
        // data byte (ASN.1 blob, protobuf message, …). The reader
        // keeps it verbatim.
        let meta = make_meta(vec![channel(0, DataType::Binary, 4)]);
        let body = [0xFFu8, 0xD8, 0xFF, 0xE0, 0x00];
        let payload_len = 1 + 4 + 8 + body.len();
        let mut bytes = vec![0u8, 0];
        bytes.extend_from_slice(&u32::try_from(payload_len).unwrap().to_le_bytes());
        bytes.push(0x88);
        bytes.extend_from_slice(&1u32.to_le_bytes());
        bytes.extend_from_slice(&123i64.to_le_bytes());
        bytes.extend_from_slice(&body);
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::AbsTimestampData {
                samples: TimestampedPayload::Binary(v),
            } => {
                assert_eq!(v, vec![(123, vec![0xFF, 0xD8, 0xFF, 0xE0, 0x00])]);
            }
            other => panic!("expected AbsTimestampData/Binary, got {other:?}"),
        }
    }

    #[test]
    fn parses_abs_timestamp_binary_osf4_strips_trailing_null_byte() {
        // OSF4 reader: the spec-mandated trailing 0x00 is the
        // terminator and is removed before the payload reaches the
        // manager. JPEG magic + null on disk → JPEG magic only after
        // strip.
        let meta = make_meta_v4(vec![channel(0, DataType::Binary, 4)]);
        let body = [0xFFu8, 0xD8, 0xFF, 0xE0, 0x00];
        let payload_len = 1 + 4 + 8 + body.len();
        let mut bytes = vec![0u8, 0];
        bytes.extend_from_slice(&u32::try_from(payload_len).unwrap().to_le_bytes());
        bytes.push(0x88);
        bytes.extend_from_slice(&1u32.to_le_bytes());
        bytes.extend_from_slice(&123i64.to_le_bytes());
        bytes.extend_from_slice(&body);
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::AbsTimestampData {
                samples: TimestampedPayload::Binary(v),
            } => {
                assert_eq!(v, vec![(123, vec![0xFF, 0xD8, 0xFF, 0xE0])]);
            }
            other => panic!("expected AbsTimestampData/Binary, got {other:?}"),
        }
    }

    #[test]
    fn parses_abs_timestamp_gpslocation() {
        let meta = make_meta(vec![channel(0, DataType::GpsLocation, 2)]);
        let payload_len = 1 + 8 + 24;
        let mut bytes = vec![0u8, 0];
        bytes.extend_from_slice(&u16::try_from(payload_len).unwrap().to_le_bytes());
        bytes.push(0x08);
        bytes.extend_from_slice(&999i64.to_le_bytes());
        bytes.extend_from_slice(&48.1374f64.to_le_bytes());
        bytes.extend_from_slice(&11.5755f64.to_le_bytes());
        bytes.extend_from_slice(&519.0f64.to_le_bytes());
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::AbsTimestampData {
                samples: TimestampedPayload::GpsLocation(v),
            } => {
                assert_eq!(v.len(), 1);
                assert_eq!(v[0].0, 999);
                assert!((v[0].1.latitude - 48.1374).abs() < 1e-9);
                assert!((v[0].1.longitude - 11.5755).abs() < 1e-9);
                assert!((v[0].1.altitude - 519.0).abs() < 1e-9);
            }
            other => panic!("expected GpsLocation, got {other:?}"),
        }
    }

    #[test]
    fn parses_continued_rel_stamp_data_int16() {
        // OSF4-only block type. [u16 0][u16 length][u8 0x87][u32 N=2]
        // [u32 dt][i16 v][u32 dt][i16 v]
        let meta = make_meta(vec![channel(0, DataType::Int16, 2)]);
        let payload_len = 1 + 4 + 2 * (4 + 2);
        let mut bytes = vec![0u8, 0];
        bytes.extend_from_slice(&u16::try_from(payload_len).unwrap().to_le_bytes());
        bytes.push(0x87);
        bytes.extend_from_slice(&2u32.to_le_bytes());
        bytes.extend_from_slice(&100u32.to_le_bytes());
        bytes.extend_from_slice(&7i16.to_le_bytes());
        bytes.extend_from_slice(&200u32.to_le_bytes());
        bytes.extend_from_slice(&8i16.to_le_bytes());
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let blk = r.next().unwrap().unwrap();
        match blk.kind {
            BlockKind::ContinuedRelStampData {
                samples: RelTimestampedPayload::Int16(v),
            } => {
                assert_eq!(v, vec![(100, 7), (200, 8)]);
            }
            other => panic!("expected ContinuedRelStampData/Int16, got {other:?}"),
        }
    }

    #[test]
    fn equidistant_block_with_string_data_type_is_invalid_block() {
        let meta = make_meta(vec![channel(0, DataType::String, 4)]);
        // bcContinuedData (5) on a string channel — not allowed.
        let mut bytes = vec![0u8, 0];
        bytes.extend_from_slice(&5u32.to_le_bytes()); // length=5 (u32)
        bytes.push(0x05);
        bytes.extend_from_slice(&[1, 2, 3, 4]);
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        let err = r.next().unwrap().unwrap_err();
        assert!(matches!(err, OsfError::InvalidBlock(_)), "got {err:?}");
    }

    #[test]
    fn trailer_block_is_consumed_silently() {
        let meta = make_meta(vec![channel(0, DataType::Int16, 2)]);
        // [u16 0xFFFF][u32 length=2][u8 0][u8 0]; no magic trailer.
        let bytes = vec![0xFF, 0xFF, 2, 0, 0, 0, 0x00, 0x00];
        let mut r = BlockReader::new(Cursor::new(bytes), &meta);
        assert!(r.next().is_none());
        assert!(r.trailer_seen());
        assert_eq!(r.blocks_truncated(), 0);
    }
}
