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

//! `osf-core` — core library for the Open Streaming Format (OSF).
//!
//! This crate is the Rust foundation for OSF tooling. It provides parsing,
//! reading, and (eventually) writing of OSF4 and OSF5 files. The Python
//! bindings in `implementations/python/` build on top of this crate via
//! PyO3 (see DECISIONS.md §18).
//!
//! Current revision implements:
//! - Magic-header detection (OSF4 vs. OSF5, including legacy identifiers).
//! - Shared metablock data model and validation helpers
//!   ([`MetaBlock`], [`parse_data_type`], [`parse_channel_type`]).
//!
//! OSF4 (XML) and OSF5 (JSON) metablock parsers, block-stream reading,
//! and writing follow in subsequent sessions.

pub(crate) mod binary_write;
pub mod block;
pub mod compression;
pub mod data_channel;
pub mod error;
pub mod header;
pub mod manager;
pub mod meta;
pub mod meta_json;
pub mod meta_xml;
pub mod reader;
pub mod stats;
pub mod types;
pub mod writer;

pub use block::{
    Block, BlockKind, GpsLocation, NumericPayload, RelTimestampedPayload, SkipReason,
    TimestampedPayload,
};
pub use data_channel::{
    Channel, ChannelMeta, EquidistantChannel, NumericValueRef, NumericValues, Sample, Segment,
    TimestampedChannel, VariableChannel, VariableValueRef,
};
pub use manager::DataManager;
pub use error::OsfError;
pub use header::{MagicHeader, OsfVersion, parse_magic_header};
pub use meta::{
    Channel as MetaChannel, FileInfo, Info, MetaBlock, SpectrumType, parse_channel_type,
    parse_data_type,
};
pub use meta_json::parse_metablock_json;
pub use meta_xml::parse_metablock_xml;
pub use reader::BlockReader;
pub use stats::{ChannelStats, ReaderStats};
pub use types::{BlockContent, ChannelType, DataType};

/// Convenience entry point: open `path`, parse the magic header and
/// metablock, and stream the data section through a [`BlockReader`].
/// Returns the parsed [`MetaBlock`], the materialised list of blocks,
/// and the final [`ReaderStats`].
///
/// This collects every block into memory; for very large files prefer
/// driving the [`BlockReader`] iterator yourself.
///
/// # Errors
///
/// Forwards errors from the magic-header parser, the metablock parser,
/// and the block reader.
pub fn read_file(
    path: &std::path::Path,
) -> Result<(MetaBlock, Vec<Block>, ReaderStats), OsfError> {
    use std::fs::File;
    use std::io::{BufReader, Read};

    let file_size = std::fs::metadata(path)?.len();

    /// Tiny `Read` adapter that counts bytes consumed so we can report
    /// `header_size_bytes` and `metablock_size_bytes` without
    /// requiring `Seek` on the inner reader.
    struct CountingRead<R: Read> {
        inner: R,
        bytes_read: u64,
    }
    impl<R: Read> Read for CountingRead<R> {
        fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
            let n = self.inner.read(buf)?;
            self.bytes_read += n as u64;
            Ok(n)
        }
    }

    let mut counted = CountingRead {
        inner: BufReader::new(File::open(path)?),
        bytes_read: 0,
    };

    let header = parse_magic_header(&mut counted)?;
    let header_size_bytes = counted.bytes_read;

    let mut body = vec![0u8; header.metablock_len as usize];
    counted.read_exact(&mut body)?;
    let metablock_size_bytes = header.metablock_len;
    let meta = parse_metablock(header.version, &body)?;

    let mut block_reader = BlockReader::new(counted, &meta).with_file_size(file_size);
    let mut blocks = Vec::new();
    for blk in &mut block_reader {
        blocks.push(blk?);
    }

    let mut stats = block_reader.stats();
    stats.header_size_bytes = header_size_bytes;
    stats.metablock_size_bytes = metablock_size_bytes;
    Ok((meta, blocks, stats))
}

/// Parse the metablock body for the given OSF version.
///
/// `bytes` must be exactly the metablock payload (without the magic-header
/// line and without any block-stream bytes that follow). Use the
/// `metablock_len` field of the [`MagicHeader`] to slice the right
/// portion out of the input.
///
/// # Errors
///
/// Forwards parser-level errors from [`parse_metablock_json`] /
/// [`parse_metablock_xml`] and the validation helpers in [`crate::meta`].
pub fn parse_metablock(version: OsfVersion, bytes: &[u8]) -> Result<MetaBlock, OsfError> {
    match version {
        OsfVersion::Osf4 => parse_metablock_xml(bytes),
        OsfVersion::Osf5 => parse_metablock_json(bytes),
    }
}
