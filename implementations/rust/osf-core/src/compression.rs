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

//! Transparent OSFZ decompression on the read path.
//!
//! OSFZ files are compressed OSF files with no dedicated magic header
//! — detection is by the leading two bytes of the stream:
//!
//! | Format | Magic bytes                                        | Decoder |
//! |--------|----------------------------------------------------|---------|
//! | gzip   | `0x1F 0x8B`                                        | [`GzDecoder`]   |
//! | zlib   | `0x78 0x01 / 0x5E / 0x9C / 0xDA`                   | [`ZlibDecoder`] |
//! | none   | anything else (real OSF starts with `OSF` = `0x4F`)| direct  |
//!
//! [DECISIONS §12](../../../DECISIONS.md#12-osfz-compression) was
//! revised on 2026-05-06 to require both gzip and zlib detection —
//! deployed Optimeas devices emit gzip-wrapped OSF (`weather_station.osfz`),
//! while older tooling used raw zlib. Supporting only one would
//! exclude real field files.
//!
//! [`detect_and_wrap`] is the entry point. It returns a
//! [`MaybeCompressed`] enum the caller can pass through to the rest
//! of the read stack — the variant is preserved (no `Box<dyn Read>`)
//! so the generic `BlockReader<R: Read>` remains zero-cost.

use flate2::read::{GzDecoder, ZlibDecoder};
use std::io::{self, BufRead, BufReader, Read};

/// Result of [`detect_and_wrap`]: either the original (buffered)
/// reader for an uncompressed stream, or a transparent decompressor.
///
/// `Read` is implemented for all variants; downstream code does not
/// need to discriminate. Use [`Self::is_compressed`] /
/// [`Self::detected_format`] to learn what was detected for telemetry.
pub enum MaybeCompressed<R: Read> {
    /// Stream is uncompressed; passes through the leading bytes.
    Plain(BufReader<R>),
    /// Stream is zlib-compressed (RFC 1950).
    Zlib(ZlibDecoder<BufReader<R>>),
    /// Stream is gzip-compressed (RFC 1952). The format Optimeas
    /// devices currently emit for `.osfz`.
    Gzip(GzDecoder<BufReader<R>>),
}

/// What [`detect_and_wrap`] decided about the stream.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CompressionFormat {
    /// No compression detected.
    None,
    /// Stream is zlib-compressed (RFC 1950).
    Zlib,
    /// Stream is gzip-compressed (RFC 1952).
    Gzip,
}

impl<R: Read> MaybeCompressed<R> {
    /// True if the stream was detected as compressed (zlib or gzip).
    #[must_use]
    pub fn is_compressed(&self) -> bool {
        !matches!(self, Self::Plain(_))
    }

    /// Concrete format detection result.
    #[must_use]
    pub fn detected_format(&self) -> CompressionFormat {
        match self {
            Self::Plain(_) => CompressionFormat::None,
            Self::Zlib(_) => CompressionFormat::Zlib,
            Self::Gzip(_) => CompressionFormat::Gzip,
        }
    }
}

impl<R: Read> Read for MaybeCompressed<R> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        match self {
            Self::Plain(r) => r.read(buf),
            Self::Zlib(r) => r.read(buf),
            Self::Gzip(r) => r.read(buf),
        }
    }
}

/// Inspect the first two bytes of `reader` and dispatch to the right
/// decompressor.
///
/// The reader is wrapped in a [`BufReader`] so the leading bytes can
/// be peeked via `fill_buf()` without consuming them. After
/// detection, the buffered reader is handed off either as `Plain`
/// (with the cursor still at byte 0) or to the appropriate
/// `flate2` decoder, which then re-reads the bytes itself as part of
/// the compressed stream's magic / header.
///
/// Streams shorter than two bytes are treated as `Plain` — the
/// remaining I/O layers will surface the EOF appropriately when they
/// try to read the OSF magic header.
///
/// # Errors
///
/// Returns the underlying `io::Error` if the initial peek fails for
/// any reason other than `UnexpectedEof`. EOF before the second byte
/// is treated as `Plain`, not as an error.
pub fn detect_and_wrap<R: Read>(reader: R) -> io::Result<MaybeCompressed<R>> {
    let mut buffered = BufReader::new(reader);
    let format = peek_format(&mut buffered)?;
    Ok(match format {
        CompressionFormat::None => MaybeCompressed::Plain(buffered),
        CompressionFormat::Zlib => MaybeCompressed::Zlib(ZlibDecoder::new(buffered)),
        CompressionFormat::Gzip => MaybeCompressed::Gzip(GzDecoder::new(buffered)),
    })
}

/// Peek the leading bytes via `BufReader::fill_buf` (which does not
/// consume) and classify the stream.
fn peek_format<R: Read>(buffered: &mut BufReader<R>) -> io::Result<CompressionFormat> {
    let head = buffered.fill_buf()?;
    if head.len() < 2 {
        return Ok(CompressionFormat::None);
    }
    let first = head[0];
    let second = head[1];

    if first == 0x1F && second == 0x8B {
        return Ok(CompressionFormat::Gzip);
    }
    if first == 0x78 && matches!(second, 0x01 | 0x5E | 0x9C | 0xDA) {
        return Ok(CompressionFormat::Zlib);
    }
    Ok(CompressionFormat::None)
}

#[cfg(test)]
mod tests {
    use super::*;
    use flate2::Compression;
    use flate2::write::{GzEncoder, ZlibEncoder};
    use std::io::Cursor;
    use std::io::Write;

    fn zlib_encode(input: &[u8]) -> Vec<u8> {
        let mut e = ZlibEncoder::new(Vec::new(), Compression::default());
        e.write_all(input).unwrap();
        e.finish().unwrap()
    }

    fn gzip_encode(input: &[u8]) -> Vec<u8> {
        let mut e = GzEncoder::new(Vec::new(), Compression::default());
        e.write_all(input).unwrap();
        e.finish().unwrap()
    }

    fn read_all<R: Read>(mut r: R) -> Vec<u8> {
        let mut out = Vec::new();
        r.read_to_end(&mut out).unwrap();
        out
    }

    const PLAIN_OSF5: &[u8] = b"OSF5 42\n{\"osf\":...rest of file...}";

    #[test]
    fn plain_osf5_is_passed_through() {
        let mc = detect_and_wrap(Cursor::new(PLAIN_OSF5)).unwrap();
        assert!(!mc.is_compressed());
        assert_eq!(mc.detected_format(), CompressionFormat::None);
        assert_eq!(read_all(mc), PLAIN_OSF5);
    }

    #[test]
    fn zlib_compressed_osf_is_decompressed() {
        let compressed = zlib_encode(PLAIN_OSF5);
        // zlib encoder produces the canonical 0x78 0x9C header at default
        // compression, which is exactly what we detect.
        assert_eq!(compressed[0], 0x78);
        let mc = detect_and_wrap(Cursor::new(compressed)).unwrap();
        assert!(mc.is_compressed());
        assert_eq!(mc.detected_format(), CompressionFormat::Zlib);
        assert_eq!(read_all(mc), PLAIN_OSF5);
    }

    #[test]
    fn gzip_compressed_osf_is_decompressed() {
        let compressed = gzip_encode(PLAIN_OSF5);
        assert_eq!(&compressed[..2], &[0x1F, 0x8B]);
        let mc = detect_and_wrap(Cursor::new(compressed)).unwrap();
        assert!(mc.is_compressed());
        assert_eq!(mc.detected_format(), CompressionFormat::Gzip);
        assert_eq!(read_all(mc), PLAIN_OSF5);
    }

    #[test]
    fn bytes_starting_with_0x78_but_invalid_zlib_second_byte_are_plain() {
        // 0x78 0xFF is not a valid zlib header.
        let bytes = vec![0x78u8, 0xFF, 0x00, 0x01];
        let mc = detect_and_wrap(Cursor::new(bytes.clone())).unwrap();
        assert_eq!(mc.detected_format(), CompressionFormat::None);
        assert_eq!(read_all(mc), bytes);
    }

    #[test]
    fn single_byte_stream_is_plain() {
        // 0x78 alone, then EOF. Cannot be confirmed as zlib without the
        // second byte; treat as plain so downstream layers see the
        // proper EOF when they attempt to parse a header.
        let bytes = vec![0x78u8];
        let mc = detect_and_wrap(Cursor::new(bytes.clone())).unwrap();
        assert_eq!(mc.detected_format(), CompressionFormat::None);
        assert_eq!(read_all(mc), bytes);
    }

    #[test]
    fn empty_stream_is_plain_and_yields_nothing() {
        let mc = detect_and_wrap(Cursor::new(Vec::<u8>::new())).unwrap();
        assert_eq!(mc.detected_format(), CompressionFormat::None);
        assert_eq!(read_all(mc), Vec::<u8>::new());
    }

    #[test]
    fn osf4_legacy_identifier_is_not_misclassified() {
        // OCEAN_STREAM_FORMAT4 starts with 0x4F (`O`); cannot collide
        // with either zlib (0x78) or gzip (0x1F).
        let bytes = b"OCEAN_STREAM_FORMAT4 100\n<?xml version...";
        let mc = detect_and_wrap(Cursor::new(bytes.to_vec())).unwrap();
        assert_eq!(mc.detected_format(), CompressionFormat::None);
    }
}
