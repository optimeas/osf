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
    Block, BlockKind, ControlKind, SkipReason, decode_control_byte,
};
use crate::error::OsfError;
use crate::meta::MetaBlock;
use crate::types::{ChannelType, DataType};
use byteorder::{LittleEndian, ReadBytesExt};
use log::{debug, warn};
use std::collections::HashMap;
use std::io::Read;

/// Special channel index that introduces the optional info-data block
/// at the end of an OSF4 file. OSF5 no longer writes it but readers
/// must tolerate it.
const TRAILER_CHANNEL_INDEX: u16 = 0xFFFF;

/// Length of the magic trailer string written after the info block in
/// OSF4 files (`OSF_STREAM_END <pos>===…`). Padded to exactly 40 bytes.
const MAGIC_TRAILER_LEN: usize = 40;

/// Per-channel metadata the reader needs to decode a block: the
/// channel and data types so we know how to route the payload, and
/// `size_of_length_value` so we know how wide the length prefix is on
/// the wire.
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
    channels: HashMap<u16, ChannelInfo>,
    finished: bool,
    capture_skipped: bool,
    file_size_bytes: Option<u64>,
    blocks_truncated: u64,
    trailer_seen: bool,
}

impl<R: Read> BlockReader<R> {
    /// Build a reader from an open stream and the parsed metablock.
    ///
    /// The metablock is consumed only for its channel definitions; the
    /// reader keeps a `HashMap<u16, ChannelInfo>` indexed by channel
    /// index for fast lookup during iteration.
    pub fn new(inner: R, meta: &MetaBlock) -> Self {
        let mut channels = HashMap::with_capacity(meta.channels.len());
        for chan in &meta.channels {
            channels.insert(
                chan.index,
                ChannelInfo {
                    channel_type: chan.channel_type.clone(),
                    data_type: chan.data_type.clone(),
                    size_of_length_value: chan.size_of_length_value,
                },
            );
        }
        Self {
            inner,
            channels,
            finished: false,
            capture_skipped: false,
            file_size_bytes: None,
            blocks_truncated: 0,
            trailer_seen: false,
        }
    }

    /// Opt in to capturing the raw payload bytes of skipped blocks.
    /// Default is `false` (zero allocation).
    #[must_use]
    pub fn with_capture_skipped_payload(mut self, enabled: bool) -> Self {
        self.capture_skipped = enabled;
        self
    }

    /// Record the file size that produced this stream so consumers (e.g.
    /// the stats reporter) can show it. The reader does not use the
    /// value internally.
    #[must_use]
    pub fn with_file_size(mut self, file_size_bytes: u64) -> Self {
        self.file_size_bytes = Some(file_size_bytes);
        self
    }

    /// Number of blocks the reader could not finish reading because
    /// the underlying stream ended mid-block. Capped at 1 by
    /// construction.
    #[must_use]
    pub fn blocks_truncated(&self) -> u64 {
        self.blocks_truncated
    }

    /// `true` if the reader has consumed the optional `0xFFFF`
    /// info-data block. OSF5 writers no longer produce it; OSF4
    /// writers may.
    #[must_use]
    pub fn trailer_seen(&self) -> bool {
        self.trailer_seen
    }

    /// File size that was supplied via [`Self::with_file_size`], if
    /// any.
    #[must_use]
    pub fn file_size_bytes(&self) -> Option<u64> {
        self.file_size_bytes
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
        // sizeoflengthvalue.
        let length = match self.inner.read_u32::<LittleEndian>() {
            Ok(v) => v,
            Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => {
                self.blocks_truncated = self.blocks_truncated.max(1);
                self.finished = true;
                self.trailer_seen = true;
                return Ok(());
            }
            Err(e) => return Err(e.into()),
        };

        // Drain the payload (control byte + info-block bytes).
        if !self.drain(u64::from(length))? {
            self.blocks_truncated = self.blocks_truncated.max(1);
        }

        // Best-effort: try to consume the magic trailer if present.
        // Reading it is non-mandatory; if the file ends before then,
        // we simply stop without error.
        let mut tail = [0u8; MAGIC_TRAILER_LEN];
        if let Ok(()) = self.inner.read_exact(&mut tail) {
            if !tail.starts_with(b"OSF_STREAM_END") {
                debug!(
                    "OSF trailer: 40-byte tail did not start with \
                     \"OSF_STREAM_END\"; got {:?}",
                    String::from_utf8_lossy(&tail[..14.min(tail.len())])
                );
            }
        }

        self.trailer_seen = true;
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
                self.blocks_truncated = self.blocks_truncated.max(1);
                self.finished = true;
                return None;
            }
            Err(e) => {
                self.finished = true;
                return Some(Err(e));
            }
        };

        if length == 0 {
            warn!(
                "channel {channel_index} produced a zero-length block; \
                 skipping (likely writer bug)"
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
                self.blocks_truncated = self.blocks_truncated.max(1);
                self.finished = true;
                return None;
            }
            Err(e) => {
                self.finished = true;
                return Some(Err(e));
            }
        };

        // Step 7: decode the control byte. Reserved / deprecated /
        // unknown values become Skipped. The full per-control-byte
        // typed parsing arrives in the next commit; until then a
        // recognised control byte still produces a Skipped block so
        // the stream cursor stays consistent.
        let (control_byte, body) = payload.split_first().expect("length > 0 guaranteed above");
        let control = decode_control_byte(*control_byte);

        let bytes_skipped = u64::from(length);
        let body_bytes = body.to_vec();
        let payload_field = if self.capture_skipped {
            Some(body_bytes)
        } else {
            None
        };

        let reason = match control.kind {
            ControlKind::Reserved | ControlKind::TimebaseRealign => {
                SkipReason::ReservedBlockType(*control_byte & 0x7F)
            }
            ControlKind::TrustedTimestamp
            | ControlKind::StatusEvent
            | ControlKind::MessageEvent => {
                SkipReason::DeprecatedBlockType(*control_byte & 0x7F)
            }
            ControlKind::Unknown(raw) => SkipReason::ReservedBlockType(raw),
            // The four "supported" block types fall through to Skipped
            // in this commit; the next commit replaces this branch with
            // typed parsers. Using ReservedBlockType as the placeholder
            // here is *intentional* — for Session 3 commit 3 the reader
            // is genuinely treating these as opaque.
            ControlKind::StartData
            | ControlKind::ContinuedData
            | ControlKind::AbsTimeStampData
            | ControlKind::ContinuedRelStampData => {
                SkipReason::ReservedBlockType(*control_byte & 0x7F)
            }
        };

        Some(Ok(Block {
            channel_index,
            kind: BlockKind::Skipped {
                reason,
                bytes_skipped,
                payload: payload_field,
            },
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
                Some(buf) => Ok(Block {
                    channel_index,
                    kind: BlockKind::Skipped {
                        reason,
                        bytes_skipped: length as u64,
                        payload: Some(buf),
                    },
                }),
                None => {
                    self.blocks_truncated = self.blocks_truncated.max(1);
                    self.finished = true;
                    Err(OsfError::Io(std::io::Error::new(
                        std::io::ErrorKind::UnexpectedEof,
                        "stream truncated mid-skip-payload",
                    )))
                }
            }
        } else {
            if !self.drain(length as u64)? {
                self.blocks_truncated = self.blocks_truncated.max(1);
                self.finished = true;
                return Err(OsfError::Io(std::io::Error::new(
                    std::io::ErrorKind::UnexpectedEof,
                    "stream truncated mid-skip-payload",
                )));
            }
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
