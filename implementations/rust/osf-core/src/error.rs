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

//! Error types for the OSF core library.

use std::io;
use thiserror::Error;

/// Errors produced by the OSF reader and writer.
#[derive(Debug, Error)]
pub enum OsfError {
    /// Underlying I/O failure while reading or writing the file.
    #[error("I/O error: {0}")]
    Io(#[from] io::Error),

    /// The first line of the file could not be parsed as an OSF magic
    /// header. Carries a description of what was found.
    #[error("invalid OSF magic header: {0}")]
    InvalidMagicHeader(String),

    /// The magic header was parseable but identifies a version this build
    /// does not support.
    #[error("unsupported OSF version: {0}")]
    UnsupportedVersion(String),

    /// The magic header line was longer than the configured sanity limit
    /// before a newline was seen — almost certainly not an OSF file.
    #[error("magic header line exceeded {0} bytes without terminator")]
    MagicHeaderTooLong(usize),

    /// The metablock body was structurally malformed in a way that we
    /// cannot recover from (missing required field, unparseable number,
    /// invalid `sizeoflengthvalue`, etc.). Other channels in the same
    /// file are unaffected; the file as a whole is rejected because the
    /// metablock is the contract for the binary blocks that follow.
    #[error("invalid OSF metablock: {0}")]
    InvalidMetablock(String),

    /// Encountered a string, attribute, or datatype that was removed in
    /// spec revision **2026-05-04**. Carries the field name that held
    /// the removed value, the value itself, and (where applicable) the
    /// replacement spelling so the caller can produce a useful message.
    #[error(
        "field {field:?} uses {value:?}, removed in spec revision 2026-05-04{}",
        replacement.map(|r| format!(" — replacement: {r:?}")).unwrap_or_default()
    )]
    RemovedInSpec2026_05_04 {
        /// Logical name of the field that carried the removed value
        /// (e.g. `"datatype"`).
        field: &'static str,
        /// The literal value found on disk.
        value: String,
        /// Spelling that replaces the removed value, where the spec
        /// defines a replacement.
        replacement: Option<&'static str>,
    },

    /// `serde_json` was unable to parse the metablock body.
    #[error("OSF5 metablock JSON parse error: {0}")]
    Json(#[from] serde_json::Error),

    /// `quick-xml` was unable to parse the metablock body.
    #[error("OSF4 metablock XML parse error: {0}")]
    Xml(String),

    /// The block stream referenced a channel index that does not appear
    /// in the metablock. Without the channel definition the reader does
    /// not know how wide the length prefix is, so this is a hard error
    /// rather than a graceful skip — the file is corrupted.
    #[error("block references unknown channel index {0}")]
    UnknownChannelIndex(u16),

    /// The block stream payload was structurally malformed — wrong
    /// length for the declared data type, required field missing,
    /// equidistant block on a string/binary channel, etc.
    #[error("invalid OSF block payload: {0}")]
    InvalidBlock(String),

    /// The same channel produced both equidistant blocks
    /// (`bcStartData` / `bcContinuedData`) and timestamped blocks
    /// (`bcAbsTimeStampData` / `bcContinuedRelStampData`). Spec rev
    /// 2026-05-04 (Restrictions table in `osf_general.md`) forbids
    /// the mix per channel; see `BlockReader` for stream-level skip
    /// behaviour.
    #[error("channel {index} mixes equidistant and timestamped blocks")]
    ChannelMixedBlockTypes {
        /// Channel index whose block types disagree.
        index: u16,
    },

    /// A `bcContinuedData` block arrived for a channel that has not
    /// yet seen a `bcStartData`. Equidistant continuation depends on
    /// the most recent start block for its sample rate, so without an
    /// open segment the data has no meaningful timeline.
    #[error("channel {index} produced bcContinuedData without a preceding bcStartData")]
    ContinuedDataWithoutStart {
        /// Channel index that produced the orphan continuation.
        index: u16,
    },

    /// A `bcContinuedRelStampData` block arrived for a channel that
    /// has not yet observed an absolute timestamp. The first relative
    /// delta is anchored to the channel's last known absolute time;
    /// without an anchor the deltas cannot be lifted to absolute time.
    #[error("channel {index} produced bcContinuedRelStampData without an absolute anchor")]
    RelStampWithoutAnchor {
        /// Channel index that produced the orphan rel-stamp block.
        index: u16,
    },

    /// A block payload's typed variant did not match the channel's
    /// declared `data_type`. The reader normally enforces this at the
    /// stream level; the manager defends against it as well in case a
    /// future refactor weakens the reader-side check.
    #[error(
        "channel {channel} data type mismatch: expected {expected:?}, got block payload {got:?}"
    )]
    DataTypeMismatch {
        /// Channel index that produced the mismatched block.
        channel: u16,
        /// Datatype declared in the metablock.
        expected: crate::types::DataType,
        /// Datatype implied by the block payload.
        got: crate::types::DataType,
    },

    /// `DataManager::channel(name)` was called with a name that does
    /// not match any channel in the file. The manager's lookup methods
    /// return `Option`, but this variant is reserved so future
    /// fallible APIs (e.g. `try_channel`) can use a typed error.
    #[error("channel {name:?} not found")]
    ChannelNotFound {
        /// Name that was looked up.
        name: String,
    },

    /// A typed flat-access helper (e.g. `as_doubles_flat`) was called
    /// on a channel whose stored data type does not match the
    /// requested one.
    #[error(
        "channel {channel} flat-access mismatch: requested {requested:?}, channel holds {actual:?}"
    )]
    DataTypeAccessMismatch {
        /// Channel index whose typed access failed.
        channel: u16,
        /// Datatype the helper was asked to produce.
        requested: crate::types::DataType,
        /// Datatype the channel actually stores.
        actual: crate::types::DataType,
    },
}
