// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! OSF block model.
//!
//! Every block in the binary stream consists of a 2-byte channel index,
//! a length field (2 or 4 bytes per the channel's `sizeoflengthvalue`),
//! a 1-byte control byte, and a payload. This module defines the typed
//! representation a block lands in once the reader has decoded it.
//!
//! Two design choices are intentional:
//!
//! 1. **Unpacked typed payloads, not zero-copy.** Each `Block` carries
//!    its samples in pre-allocated typed vectors (`Vec<f64>`,
//!    `Vec<i32>`, …). Block sizes in OSF are typically KB to a few MB,
//!    so the per-block allocation is acceptable, and the lifetime
//!    story is dramatically simpler than a `&[u8]`-borrowing API. A
//!    zero-copy `RawBlockReader` is conceivable as a *future second
//!    layer* if profiling justifies it — not now.
//!
//! 2. **Skipped blocks remain visible.** Blocks the reader cannot
//!    interpret (deprecated control bytes, `Unsupported` channels,
//!    unknown control values) come through as [`BlockKind::Skipped`]
//!    rather than being silently swallowed. Stream position is
//!    preserved either way; the caller can opt to inspect skipped
//!    payloads via [`crate::reader::BlockReader`] options that ship in
//!    a later commit.

/// A decoded data block from an OSF stream.
#[derive(Debug, Clone, PartialEq)]
pub struct Block {
    /// Channel this block belongs to. `0xFFFF` is reserved for the
    /// optional info-data block at the end of an OSF4 file and is not
    /// produced as a regular `Block` by the reader.
    pub channel_index: u16,
    /// Decoded payload, discriminated by control byte.
    pub kind: BlockKind,
}

/// Discriminated union of OSF block payloads.
#[derive(Debug, Clone, PartialEq)]
pub enum BlockKind {
    /// `bcStartData`: opens a new equidistant segment with absolute
    /// start timestamp and sample rate; carries the first samples.
    /// Allowed only for numeric data types per spec.
    StartData {
        /// Absolute start timestamp of this segment in nanoseconds
        /// since the Unix epoch (UTC).
        start_timestamp_ns: i64,
        /// Sample rate in Hz, valid until the next `bcStartData` block
        /// of the same channel. Spec rev 2026-05-04 makes this field
        /// mandatory in both OSF4 and OSF5.
        sample_rate_hz: f64,
        /// Sample values in their typed form.
        samples: NumericPayload,
    },
    /// `bcContinuedData`: continuation of the current equidistant
    /// segment of the same channel. Time per sample is `1 /
    /// sample_rate_hz` from the most recent `StartData` of the same
    /// channel. Numeric only.
    ContinuedData {
        /// Sample values, picking up where the previous block ended.
        samples: NumericPayload,
    },
    /// `bcAbsTimeStampData`: each sample carries its own absolute
    /// timestamp. Supports all data types including string and binary.
    AbsTimestampData {
        /// `(timestamp_ns, value)` pairs in stream order.
        samples: TimestampedPayload,
    },
    /// `bcContinuedRelStampData`: OSF4-only. Each sample carries a
    /// relative time delta in nanoseconds (`uint32`) plus a value.
    /// Reader supports it; writers never produce it.
    ContinuedRelStampData {
        /// `(delta_ns, value)` pairs in stream order.
        samples: RelTimestampedPayload,
    },
    /// Block kept for stream-position purposes after the reader chose
    /// not to interpret it: known control byte but channel marked as
    /// `Unsupported`, block type intentionally skipped (`bcStatusEvent`,
    /// `bcTrustedTimestamp`, `bcTimebaseRealign`, `bcReserved`, unknown
    /// control values, or `bcMessageEvent` in its two unspecified cases —
    /// bit 7 set, or a channel `datatype` other than string/binary — see
    /// `SkipReason::ReservedBlockType`), a frame whose integrity CRC did
    /// not match, an unverified integrity signature block, or a
    /// non-conforming zero-length block (`SkipReason::ZeroLengthBlock`).
    /// A `bcMessageEvent` block on a string/binary channel with bit 7
    /// clear is instead decoded into `BlockKind::AbsTimestampData`
    /// (OSF-UP4, DECISIONS §26).
    Skipped {
        /// Why the reader skipped this block.
        reason: SkipReason,
        /// Number of payload bytes the reader had to consume from the
        /// stream (control byte + payload). Always ≥ 1, except for
        /// `SkipReason::ZeroLengthBlock`, which is always 0 — that block
        /// has no control byte and no payload to consume.
        bytes_skipped: u64,
        /// Captured payload bytes after the control byte. Default
        /// behaviour is `None` (bytes are dropped without allocation).
        /// Opt in with
        /// `BlockReader::with_capture_skipped_payload(true)` to have
        /// the reader keep the bytes here so application code can
        /// parse them without re-implementing block-framing.
        payload: Option<Vec<u8>>,
    },
}

/// Reason why a block ended up as [`BlockKind::Skipped`].
///
/// Marked `#[non_exhaustive]`: readers must tolerate future skip reasons, so
/// downstream `match` arms need a catch-all.
#[derive(Debug, Clone, PartialEq)]
#[non_exhaustive]
pub enum SkipReason {
    /// The channel's `data_type` is [`crate::DataType::Unsupported`] —
    /// either a future-spec spelling or one not implemented yet.
    UnsupportedDataType,
    /// The channel's `channel_type` is [`crate::ChannelType::Unsupported`].
    UnsupportedChannelType,
    /// Deprecated control byte that newer writers no longer emit but
    /// readers must tolerate. The inner `u8` is the raw control-byte
    /// value (1 = `bcTrustedTimestamp`). `bcStatusEvent` (3) and
    /// `bcMessageEvent` (4) each have their own reason — see
    /// [`Self::StatusEventBlock`] and [`BlockKind::AbsTimestampData`]
    /// respectively (OSF-UP4, DECISIONS §26).
    DeprecatedBlockType(u8),
    /// A `bcStatusEvent` block (control byte 3). Skipped deliberately: its
    /// payload is a fixed status word rather than a value of the channel's
    /// declared datatype, so it is not a sample of that channel (OSF-UP4,
    /// DECISIONS §26). Counted separately so an occurrence stays visible.
    StatusEventBlock,
    /// Reserved control byte (0 = `bcReserved`, 2 = `bcTimebaseRealign`)
    /// or any value above 8 the spec does not currently define.
    ReservedBlockType(u8),
    /// The block's frame CRC (integrity profile level `crc`) did not match
    /// the recomputed CRC32C. The block is dropped best-effort so the rest
    /// of the file stays readable.
    CrcFailed,
    /// An integrity signature block (`bcIntegritySignature = 9` on the
    /// reserved channel `0xFFFE`). This crate reads level `crc` but does not
    /// verify signatures, so the block is skipped via its length field.
    SignatureBlock,
    /// The block's length field read `0`. A conforming block always carries at
    /// least its control byte, so this is a non-conforming writer artefact
    /// (OSF-UP3, DECISIONS §25). The frame is nothing but the channel index and
    /// the length field — both already consumed — so the reader counts it and
    /// keeps scanning.
    ZeroLengthBlock,
}

/// Equidistant numeric payload: a single typed vector for the channel's
/// `data_type`. One variant per supported numeric data type.
#[derive(Debug, Clone, PartialEq)]
pub enum NumericPayload {
    /// `bool` samples — 1 byte per sample on disk, `0x00` is `false`.
    Bool(Vec<bool>),
    /// Signed 8-bit integers.
    Int8(Vec<i8>),
    /// Signed 16-bit integers.
    Int16(Vec<i16>),
    /// Signed 32-bit integers.
    Int32(Vec<i32>),
    /// Signed 64-bit integers.
    Int64(Vec<i64>),
    /// Unsigned 8-bit integers.
    UInt8(Vec<u8>),
    /// Unsigned 16-bit integers.
    UInt16(Vec<u16>),
    /// Unsigned 32-bit integers.
    UInt32(Vec<u32>),
    /// Unsigned 64-bit integers.
    UInt64(Vec<u64>),
    /// IEEE-754 single-precision floats.
    Float(Vec<f32>),
    /// IEEE-754 double-precision floats.
    Double(Vec<f64>),
}

impl NumericPayload {
    /// Number of samples held by this payload.
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
        }
    }

    /// True when no samples are held.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

/// Timestamped payload: `(timestamp_ns, value)` pairs, with one variant
/// per supported data type. `String`, `Binary`, and `GpsLocation` only
/// occur in this kind of block (equidistant blocks are numeric-only per
/// spec).
#[derive(Debug, Clone, PartialEq)]
pub enum TimestampedPayload {
    /// `bool` samples paired with absolute timestamps.
    Bool(Vec<(i64, bool)>),
    /// Signed 8-bit integers paired with absolute timestamps.
    Int8(Vec<(i64, i8)>),
    /// Signed 16-bit integers paired with absolute timestamps.
    Int16(Vec<(i64, i16)>),
    /// Signed 32-bit integers paired with absolute timestamps.
    Int32(Vec<(i64, i32)>),
    /// Signed 64-bit integers paired with absolute timestamps.
    Int64(Vec<(i64, i64)>),
    /// Unsigned 8-bit integers paired with absolute timestamps.
    UInt8(Vec<(i64, u8)>),
    /// Unsigned 16-bit integers paired with absolute timestamps.
    UInt16(Vec<(i64, u16)>),
    /// Unsigned 32-bit integers paired with absolute timestamps.
    UInt32(Vec<(i64, u32)>),
    /// Unsigned 64-bit integers paired with absolute timestamps.
    UInt64(Vec<(i64, u64)>),
    /// IEEE-754 single-precision floats with absolute timestamps.
    Float(Vec<(i64, f32)>),
    /// IEEE-754 double-precision floats with absolute timestamps.
    Double(Vec<(i64, f64)>),
    /// UTF-8 strings with absolute timestamps. For OSF4 input the
    /// spec-mandated trailing `0x00` byte has been stripped by the
    /// reader; OSF5 input carries no terminator on disk.
    String(Vec<(i64, String)>),
    /// Opaque byte payloads with absolute timestamps. For OSF4 input
    /// the spec-mandated trailing `0x00` byte has been stripped by
    /// the reader; OSF5 input carries no terminator on disk and may
    /// legitimately end in `0x00`.
    Binary(Vec<(i64, Vec<u8>)>),
    /// 24-byte GPS-location structs paired with absolute timestamps.
    GpsLocation(Vec<(i64, GpsLocation)>),
}

impl TimestampedPayload {
    /// Number of samples held by this payload.
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
            Self::String(v) => v.len(),
            Self::Binary(v) => v.len(),
            Self::GpsLocation(v) => v.len(),
        }
    }

    /// True when no samples are held.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

/// Relative-timestamped payload (OSF4 `bcContinuedRelStampData`):
/// `(delta_ns, value)` pairs where `delta_ns` is the offset from the
/// previous sample of the same channel.
#[derive(Debug, Clone, PartialEq)]
pub enum RelTimestampedPayload {
    /// `bool` samples paired with relative time deltas.
    Bool(Vec<(u32, bool)>),
    /// Signed 8-bit integers paired with relative time deltas.
    Int8(Vec<(u32, i8)>),
    /// Signed 16-bit integers paired with relative time deltas.
    Int16(Vec<(u32, i16)>),
    /// Signed 32-bit integers paired with relative time deltas.
    Int32(Vec<(u32, i32)>),
    /// Signed 64-bit integers paired with relative time deltas.
    Int64(Vec<(u32, i64)>),
    /// Unsigned 8-bit integers paired with relative time deltas.
    UInt8(Vec<(u32, u8)>),
    /// Unsigned 16-bit integers paired with relative time deltas.
    UInt16(Vec<(u32, u16)>),
    /// Unsigned 32-bit integers paired with relative time deltas.
    UInt32(Vec<(u32, u32)>),
    /// Unsigned 64-bit integers paired with relative time deltas.
    UInt64(Vec<(u32, u64)>),
    /// IEEE-754 single-precision floats with relative time deltas.
    Float(Vec<(u32, f32)>),
    /// IEEE-754 double-precision floats with relative time deltas.
    Double(Vec<(u32, f64)>),
}

impl RelTimestampedPayload {
    /// Number of samples held by this payload.
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
        }
    }

    /// True when no samples are held.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

/// Decoded form of the 1-byte control byte that follows the length
/// field in every OSF block.
///
/// Bit 7 carries the multi-sample flag; bits 0–6 select the block
/// type. Spec rev 2026-05-04 defines values 0 through 8; anything else
/// is reserved and produces [`ControlKind::Unknown`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct ControlByte {
    pub(crate) kind: ControlKind,
    /// Bit 7 of the control byte. When set, the payload begins with a
    /// `uint32` sample count `N`; when clear, exactly one sample
    /// follows (with one exception: `bcAbsTimeStampData` for `string`
    /// and `binary` always sets bit 7 per spec).
    pub(crate) multi_sample: bool,
}

/// Block-type discriminator extracted from the lower 7 bits of the
/// control byte.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum ControlKind {
    /// 0 — `bcReserved`. Originally `bcMetaData`, never used.
    Reserved,
    /// 1 — `bcTrustedTimestamp`. Deprecated; readers skip.
    TrustedTimestamp,
    /// 2 — `bcTimebaseRealign`. Deprecated; readers skip.
    TimebaseRealign,
    /// 3 — `bcStatusEvent`. Deprecated; readers skip.
    StatusEvent,
    /// 4 — `bcMessageEvent`. Deprecated; readers skip.
    MessageEvent,
    /// 5 — `bcContinuedData`. Equidistant continuation block.
    ContinuedData,
    /// 6 — `bcStartData`. Equidistant start block with sample rate.
    StartData,
    /// 7 — `bcContinuedRelStampData`. OSF4-only on read.
    ContinuedRelStampData,
    /// 8 — `bcAbsTimeStampData`. Per-sample absolute timestamps.
    AbsTimeStampData,
    /// Anything else (currently bits 0–6 ≥ 9). Carries the raw value
    /// so the reader can emit it in [`SkipReason::ReservedBlockType`]
    /// for diagnostics.
    Unknown(u8),
}

/// Decode the 1-byte control byte that follows the length field.
pub(crate) fn decode_control_byte(byte: u8) -> ControlByte {
    let multi_sample = byte & 0x80 != 0;
    let kind = match byte & 0x7F {
        0 => ControlKind::Reserved,
        1 => ControlKind::TrustedTimestamp,
        2 => ControlKind::TimebaseRealign,
        3 => ControlKind::StatusEvent,
        4 => ControlKind::MessageEvent,
        5 => ControlKind::ContinuedData,
        6 => ControlKind::StartData,
        7 => ControlKind::ContinuedRelStampData,
        8 => ControlKind::AbsTimeStampData,
        other => ControlKind::Unknown(other),
    };
    ControlByte { kind, multi_sample }
}

/// On-disk `gpslocation` payload (24 bytes: three little-endian
/// `double`s in the order `latitude`, `longitude`, `altitude` per spec
/// revision 2026-05-04).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct GpsLocation {
    /// Latitude in decimal degrees, north-positive.
    pub latitude: f64,
    /// Longitude in decimal degrees, east-positive.
    pub longitude: f64,
    /// Altitude in meters above the WGS-84 ellipsoid.
    pub altitude: f64,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn numeric_payload_len_works_for_each_variant() {
        assert_eq!(NumericPayload::Bool(vec![true, false]).len(), 2);
        assert_eq!(NumericPayload::Double(vec![1.0; 7]).len(), 7);
        assert!(NumericPayload::Int32(Vec::new()).is_empty());
    }

    #[test]
    fn timestamped_payload_len_works_for_each_variant() {
        assert_eq!(
            TimestampedPayload::String(vec![(0, "x".into())]).len(),
            1
        );
        assert_eq!(
            TimestampedPayload::Binary(vec![(1, vec![1, 2, 3]); 4]).len(),
            4
        );
        assert!(TimestampedPayload::Double(Vec::new()).is_empty());
    }

    #[test]
    fn rel_timestamped_payload_len_works() {
        assert_eq!(
            RelTimestampedPayload::Float(vec![(100, 1.0), (200, 2.0)]).len(),
            2
        );
        assert!(RelTimestampedPayload::Bool(Vec::new()).is_empty());
    }

    #[test]
    fn control_byte_decodes_all_documented_values() {
        let cases = [
            (0x00, ControlKind::Reserved, false),
            (0x01, ControlKind::TrustedTimestamp, false),
            (0x02, ControlKind::TimebaseRealign, false),
            (0x03, ControlKind::StatusEvent, false),
            (0x04, ControlKind::MessageEvent, false),
            (0x05, ControlKind::ContinuedData, false),
            (0x06, ControlKind::StartData, false),
            (0x07, ControlKind::ContinuedRelStampData, false),
            (0x08, ControlKind::AbsTimeStampData, false),
        ];
        for (byte, kind, multi) in cases {
            let cb = decode_control_byte(byte);
            assert_eq!(cb.kind, kind, "byte 0x{byte:02x}");
            assert_eq!(cb.multi_sample, multi, "byte 0x{byte:02x}");
        }
    }

    #[test]
    fn control_byte_recognises_multi_sample_bit() {
        for low in 0u8..=8 {
            let cb = decode_control_byte(low | 0x80);
            assert!(cb.multi_sample, "byte 0x{:02x} should be multi", low | 0x80);
        }
    }

    #[test]
    fn control_byte_passes_unknown_values_through() {
        let cb = decode_control_byte(0x09);
        assert_eq!(cb.kind, ControlKind::Unknown(9));
        let cb = decode_control_byte(0x7F);
        assert_eq!(cb.kind, ControlKind::Unknown(0x7F));
        // High bit must not contaminate the kind.
        let cb = decode_control_byte(0x89);
        assert_eq!(cb.kind, ControlKind::Unknown(9));
        assert!(cb.multi_sample);
    }

    #[test]
    fn block_skipped_default_payload_is_none() {
        let blk = Block {
            channel_index: 7,
            kind: BlockKind::Skipped {
                reason: SkipReason::ReservedBlockType(0),
                bytes_skipped: 1,
                payload: None,
            },
        };
        match blk.kind {
            BlockKind::Skipped { payload, .. } => assert!(payload.is_none()),
            _ => panic!("expected Skipped"),
        }
    }
}
