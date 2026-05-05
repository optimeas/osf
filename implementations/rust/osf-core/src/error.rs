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
}
