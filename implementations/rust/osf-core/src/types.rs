// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! Core OSF type enumerations.
//!
//! These mirror the spec revision **2026-05-04** datatype set. Removed
//! datatypes (`pair`, `triple`, `candata`, `gpsdata`) are intentionally
//! absent — readers must reject those legacy strings rather than silently
//! mapping them to a current type.
//!
//! Forward-compatible variants `DataType::Unsupported` and
//! `ChannelType::Unsupported` carry the on-disk string verbatim so a file
//! that uses a future-spec datatype can still be parsed channel-by-channel
//! without aborting the whole metablock. Block reading for such a channel
//! will fail explicitly when it is later requested (Session 3+).

/// Data type carried by an OSF channel. Matches the on-disk `datatype`
/// attribute / JSON field exactly (lowercase, ASCII).
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum DataType {
    /// 1-byte boolean (`0x00` = false, anything else = true).
    Bool,
    /// Signed 8-bit integer.
    Int8,
    /// Signed 16-bit integer (little-endian on disk).
    Int16,
    /// Signed 32-bit integer (little-endian on disk).
    Int32,
    /// Signed 64-bit integer (little-endian on disk).
    Int64,
    /// Unsigned 8-bit integer. Added in spec revision 2026-05-04.
    UInt8,
    /// Unsigned 16-bit integer. Added in spec revision 2026-05-04.
    UInt16,
    /// Unsigned 32-bit integer. Added in spec revision 2026-05-04.
    UInt32,
    /// Unsigned 64-bit integer. Added in spec revision 2026-05-04.
    UInt64,
    /// IEEE-754 single-precision floating point.
    Float,
    /// IEEE-754 double-precision floating point.
    Double,
    /// UTF-8 string. On-disk layout is version-deterministic per spec
    /// rev 2026-05-24: OSF4 writers MUST append a trailing `0x00`
    /// terminator and OSF4 readers MUST strip it; OSF5 writers MUST
    /// NOT append it and OSF5 readers MUST NOT strip it.
    String,
    /// Opaque byte payload. Same version-deterministic null-terminator
    /// rule as [`DataType::String`]: present in OSF4, absent in OSF5.
    Binary,
    /// Reserved spelling for the read-side alias `bytearray`. The current
    /// parser normalises `bytearray` directly to [`DataType::Binary`] on
    /// read, and the writer always emits `binary`. This variant is kept
    /// in the type so that downstream code can express "input was spelled
    /// `bytearray`" if it ever needs that distinction; the metablock
    /// parser does not produce it.
    ByteArray,
    /// 24-byte struct of `latitude`, `longitude`, `altitude` as
    /// little-endian `double`s. Renamed from `gpsdata` in spec revision
    /// 2026-05-04.
    GpsLocation,
    /// Forward-compatibility variant. Carries the on-disk string of a
    /// datatype the current build does not know. The metablock parser
    /// emits a `log::warn!` and continues; block-reader code rejects this
    /// variant explicitly when it encounters one.
    Unsupported(String),
}

/// The logical **data shape** of a channel — the OSF `channeltype`
/// metablock attribute.
///
/// This is the channel's structure, NOT its storage mode: whether a channel
/// is equidistant or timestamped is derived at read time from the block
/// control byte (`bcStartData`/`bcContinuedData` ⇒ equidistant;
/// `bcAbsTimeStampData` ⇒ per-sample timestamps) together with
/// `time_increment_ns` — never from `channeltype`. Accordingly the wire
/// strings `equidistant`/`timestamped` are NOT channeltypes and never appear
/// in a conformant file.
///
/// The spec value set is `scalar`, `vector`, `matrix`, `binary`
/// (`docs/de/osf_general.md` channel-field reference + "Kanaltypen";
/// `osf4.md`). A missing `channeltype` defaults to [`ChannelType::Scalar`].
#[derive(Debug, Clone, Default, PartialEq, Eq, Hash)]
pub enum ChannelType {
    /// One value per point in time — the most common shape. Default when the
    /// `channeltype` attribute is absent.
    #[default]
    Scalar,
    /// A sequence of values per block (e.g. an FFT spectrum). Full vector
    /// payload decoding is a future feature; the channel is retained and its
    /// blocks are read.
    Vector,
    /// A two-dimensional structure per timestamp (e.g. a rainflow matrix).
    Matrix,
    /// Arbitrary binary blocks — one blob per point in time (typically with a
    /// `mimetype`). Payload-equivalent to a `scalar` channel of `datatype`
    /// `binary`.
    Binary,
    /// Forward-compatibility variant. Carries the on-disk string of a
    /// channel type the current build does not know.
    Unsupported(String),
}

/// Block-content discriminator used in the OSF stream.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum BlockContent {
    /// Marks the start of a new equidistant segment; carries an `int64`
    /// start timestamp followed by a `double` sample rate.
    StartData,
    /// Carries one or more samples each prefixed with an absolute
    /// timestamp (timestamped channels).
    AbsTimeStampData,
    /// Carries a packed run of equidistant samples without per-sample
    /// timestamps.
    EquidistantData,
}
