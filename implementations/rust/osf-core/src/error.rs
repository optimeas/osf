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
}
