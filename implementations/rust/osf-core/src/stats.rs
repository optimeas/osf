// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! Reader-side telemetry.
//!
//! [`ReaderStats`] is populated by [`crate::reader::BlockReader`]
//! during iteration. The fields are deliberately concrete (counts and
//! sizes, not opaque metric handles) so the `stats` example, the
//! Python bindings, and any application using `osf-core` can format
//! the values without having to introspect.
//!
//! Reasons that are tracked separately rather than under a single
//! `blocks_skipped` counter:
//! - **Unsupported channels** (forward-compat skips — `Unsupported`
//!   data types or channel types) usually mean the file uses a
//!   future-spec datatype this build does not know yet.
//! - **Deprecated block types** (`bcTrustedTimestamp`,
//!   `bcStatusEvent`, `bcMessageEvent`) appear in older field files
//!   such as `examples/motorbike.osf` and tell you the file predates
//!   spec rev 2026-05-04.
//! - **Reserved block types** (`bcReserved`, `bcTimebaseRealign`,
//!   anything with bits 0–6 ≥ 9) are either spec-internal or genuinely
//!   unknown.

use crate::integrity::IntegrityProfile;
use std::collections::HashMap;
use std::fmt;
use std::time::Duration;

/// Aggregated counts and timings produced by reading an OSF file.
#[derive(Debug, Clone, Default)]
pub struct ReaderStats {
    /// Size of the source file in bytes, when known. `None` for
    /// streaming sources (e.g. a network socket).
    pub file_size_bytes: Option<u64>,
    /// Bytes consumed by the magic-header line (identifier + space +
    /// length + `\n`). Populated by [`crate::read_file`]; the
    /// `BlockReader` does not see the header itself.
    pub header_size_bytes: u64,
    /// Bytes consumed by the metablock body (matches the
    /// `metablock_len` field of the magic header).
    pub metablock_size_bytes: u64,
    /// Bytes consumed by the block-stream section. Includes channel
    /// indices, length prefixes, control bytes, and payloads, but
    /// excludes the magic-header and metablock bytes.
    pub data_section_size_bytes: u64,
    /// Wall-clock time taken to read the file from the moment the
    /// `BlockReader` was constructed until iteration ended.
    pub elapsed: Duration,

    /// Number of channels declared in the metablock.
    pub channels_total: usize,
    /// Channels that produced at least one block during iteration.
    pub channels_with_data: usize,
    /// Channels declared with `DataType::Unsupported` or
    /// `ChannelType::Unsupported` — every block on such a channel is
    /// counted as `blocks_skipped_unsupported`.
    pub channels_unsupported: usize,

    /// Total number of blocks observed (`blocks_read +
    /// blocks_skipped_*`).
    pub blocks_total: u64,
    /// Blocks the reader produced as typed `BlockKind` variants
    /// (everything except `Skipped`).
    pub blocks_read: u64,
    /// Blocks skipped because the channel's data or channel type was
    /// `Unsupported`.
    pub blocks_skipped_unsupported: u64,
    /// Blocks skipped because the control byte identified a deprecated
    /// block type (`bcTrustedTimestamp`, `bcStatusEvent`,
    /// `bcMessageEvent`).
    pub blocks_skipped_deprecated_type: u64,
    /// Blocks skipped because the control byte identified a reserved
    /// block type (`bcReserved`, `bcTimebaseRealign`, or any value ≥
    /// 9 that the spec does not currently define).
    pub blocks_skipped_reserved_type: u64,
    /// Number of blocks the reader could not finish before the stream
    /// ended. Capped at 1 by construction (no useful block can follow
    /// a partial one).
    pub blocks_truncated: u64,
    /// Whether the optional `0xFFFF` info-data block was encountered.
    pub trailer_seen: bool,

    /// Whether the source stream was OSFZ-compressed (gzip or zlib)
    /// and therefore went through transparent decompression on read.
    /// `false` for plain OSF files.
    pub compressed: bool,

    /// Detected compression format on the source stream.
    pub compression_format: CompressionFormat,

    /// Integrity level declared by the file's magic-header token
    /// (`none` when the file carries no integrity profile).
    pub integrity: IntegrityProfile,
    /// Blocks dropped because their frame CRC (level `crc`) did not match.
    pub blocks_crc_failed: u64,
    /// Integrity signature blocks (channel `0xFFFE`, control byte 9) skipped
    /// because this crate reads level `crc` but does not verify signatures.
    pub blocks_signature_skipped: u64,

    /// Per-channel detail keyed by channel index.
    pub per_channel: HashMap<u16, ChannelStats>,
}

/// Compression format detected on the input stream — mirrors
/// [`crate::compression::CompressionFormat`] in the public stats API
/// so callers do not need to import the lower-level type.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum CompressionFormat {
    /// No compression — stream was a regular OSF file.
    #[default]
    None,
    /// zlib stream (RFC 1950).
    Zlib,
    /// gzip stream (RFC 1952). Optimeas devices' current OSFZ wire
    /// format.
    Gzip,
}

impl From<crate::compression::CompressionFormat> for CompressionFormat {
    fn from(value: crate::compression::CompressionFormat) -> Self {
        match value {
            crate::compression::CompressionFormat::None => Self::None,
            crate::compression::CompressionFormat::Zlib => Self::Zlib,
            crate::compression::CompressionFormat::Gzip => Self::Gzip,
        }
    }
}

impl std::fmt::Display for CompressionFormat {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::None => f.write_str("none"),
            Self::Zlib => f.write_str("zlib"),
            Self::Gzip => f.write_str("gzip"),
        }
    }
}

/// Per-channel reader telemetry.
#[derive(Debug, Clone, Default)]
pub struct ChannelStats {
    /// Channel name from the metablock; copied here so per-channel
    /// stats can be rendered without a back-pointer to the metablock.
    pub name: String,
    /// Blocks the reader returned as typed variants for this channel.
    pub blocks_read: u64,
    /// Blocks the reader skipped on this channel (any reason).
    pub blocks_skipped: u64,
    /// Sum of sample counts across all blocks of this channel
    /// (string / binary count one sample per block).
    pub samples_total: u64,
    /// Sum of length-field values across all blocks of this channel.
    /// Useful as a rough payload-size proxy.
    pub bytes_payload: u64,
    /// Number of `bcStartData` blocks observed (= number of distinct
    /// equidistant segments).
    pub segments: u32,
    /// Earliest and latest absolute timestamp the reader observed for
    /// this channel. `None` if the channel only produced blocks
    /// without absolute timestamps (`bcContinuedData` or
    /// `bcContinuedRelStampData` after a missed `bcStartData`).
    pub time_range_ns: Option<(i64, i64)>,
}

impl ChannelStats {
    /// Extend the recorded time range to include the given timestamp.
    pub(crate) fn observe_timestamp(&mut self, ts: i64) {
        match &mut self.time_range_ns {
            Some((first, last)) => {
                if ts < *first {
                    *first = ts;
                }
                if ts > *last {
                    *last = ts;
                }
            }
            None => self.time_range_ns = Some((ts, ts)),
        }
    }
}

fn fmt_bytes(b: u64) -> String {
    const KB: u64 = 1024;
    const MB: u64 = 1024 * KB;
    const GB: u64 = 1024 * MB;
    if b >= GB {
        format!("{:.2} GB", b as f64 / GB as f64)
    } else if b >= MB {
        format!("{:.2} MB", b as f64 / MB as f64)
    } else if b >= KB {
        format!("{:.2} KB", b as f64 / KB as f64)
    } else {
        format!("{b} B")
    }
}

fn fmt_duration(d: Duration) -> String {
    let secs = d.as_secs_f64();
    if secs < 1.0 {
        format!("{:.0} ms", secs * 1000.0)
    } else {
        format!("{secs:.2} s")
    }
}

impl ReaderStats {
    /// Overall integrity verification status, using the vocabulary defined by
    /// the OSF5 integrity profile.
    ///
    /// This crate implements level `crc`; it reads level `signed` files but
    /// does not verify signatures, so a signed file always reports
    /// `signature_unverifiable` (its CRC layer is still checked and the file
    /// stays readable).
    ///
    /// - `none` — no integrity profile declared.
    /// - `crc_valid` — level `crc`, every block CRC verified.
    /// - `invalid` — level `crc`, at least one block failed its frame CRC.
    /// - `signature_unverifiable` — level `signed`; signatures are not
    ///   verified by this crate.
    #[must_use]
    pub fn verification_status(&self) -> &'static str {
        match self.integrity {
            IntegrityProfile::None => "none",
            IntegrityProfile::Ed25519 => "signature_unverifiable",
            IntegrityProfile::Crc32c => {
                if self.blocks_crc_failed > 0 {
                    "invalid"
                } else {
                    "crc_valid"
                }
            }
        }
    }
}

impl fmt::Display for ReaderStats {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(
            f,
            "File size:       {}",
            self.file_size_bytes
                .map_or_else(|| "(streaming)".to_string(), fmt_bytes)
        )?;
        writeln!(f, "Header:          {}", fmt_bytes(self.header_size_bytes))?;
        writeln!(
            f,
            "Metablock:       {}",
            fmt_bytes(self.metablock_size_bytes)
        )?;
        writeln!(
            f,
            "Data section:    {}",
            fmt_bytes(self.data_section_size_bytes)
        )?;
        writeln!(f, "Read in:         {}", fmt_duration(self.elapsed))?;
        writeln!(f)?;
        writeln!(f, "Channels total:        {}", self.channels_total)?;
        writeln!(f, "With data:             {}", self.channels_with_data)?;
        writeln!(f, "Unsupported:           {}", self.channels_unsupported)?;
        writeln!(f)?;
        writeln!(f, "Blocks total:          {}", self.blocks_total)?;
        writeln!(f, "Read:                  {}", self.blocks_read)?;
        writeln!(
            f,
            "Skipped (unsupp.):     {}",
            self.blocks_skipped_unsupported
        )?;
        writeln!(
            f,
            "Skipped (deprec.):     {}",
            self.blocks_skipped_deprecated_type
        )?;
        writeln!(
            f,
            "Skipped (reserved):    {}",
            self.blocks_skipped_reserved_type
        )?;
        writeln!(f, "Truncated:             {}", self.blocks_truncated)?;
        if self.trailer_seen {
            writeln!(f, "Trailer block:         present")?;
        }
        if self.compressed {
            writeln!(f, "Compressed:            yes ({})", self.compression_format)?;
        }
        if self.integrity != IntegrityProfile::None {
            writeln!(f, "Integrity:             {}", self.integrity)?;
            writeln!(f, "Blocks CRC-failed:     {}", self.blocks_crc_failed)?;
            if self.blocks_signature_skipped > 0 {
                writeln!(
                    f,
                    "Signature blocks:      {} (skipped, unverified)",
                    self.blocks_signature_skipped
                )?;
            }
        }
        Ok(())
    }
}

impl fmt::Display for ChannelStats {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "blocks={}+{}skipped samples={} bytes={} segments={} ts={}",
            self.blocks_read,
            self.blocks_skipped,
            self.samples_total,
            fmt_bytes(self.bytes_payload),
            self.segments,
            self.time_range_ns
                .map_or_else(|| "-".to_string(), |(a, b)| format!("{a}..{b}")),
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn verification_status_reflects_integrity_and_failures() {
        let base = ReaderStats::default();
        assert_eq!(base.verification_status(), "none");

        let crc_ok = ReaderStats {
            integrity: IntegrityProfile::Crc32c,
            ..ReaderStats::default()
        };
        assert_eq!(crc_ok.verification_status(), "crc_valid");

        let crc_bad = ReaderStats {
            integrity: IntegrityProfile::Crc32c,
            blocks_crc_failed: 1,
            ..ReaderStats::default()
        };
        assert_eq!(crc_bad.verification_status(), "invalid");

        let signed = ReaderStats {
            integrity: IntegrityProfile::Ed25519,
            ..ReaderStats::default()
        };
        assert_eq!(signed.verification_status(), "signature_unverifiable");
    }

    #[test]
    fn observe_timestamp_grows_range_in_both_directions() {
        let mut s = ChannelStats::default();
        s.observe_timestamp(100);
        s.observe_timestamp(200);
        s.observe_timestamp(50);
        assert_eq!(s.time_range_ns, Some((50, 200)));
    }

    #[test]
    fn fmt_bytes_picks_unit_thresholds() {
        assert_eq!(fmt_bytes(0), "0 B");
        assert_eq!(fmt_bytes(1023), "1023 B");
        assert_eq!(fmt_bytes(1024), "1.00 KB");
        assert_eq!(fmt_bytes(1024 * 1024), "1.00 MB");
        assert_eq!(fmt_bytes(1024 * 1024 * 1024), "1.00 GB");
    }

    #[test]
    fn display_emits_expected_lines() {
        let mut stats = ReaderStats {
            file_size_bytes: Some(2048),
            header_size_bytes: 24,
            metablock_size_bytes: 800,
            data_section_size_bytes: 1224,
            elapsed: Duration::from_millis(7),
            channels_total: 5,
            channels_with_data: 4,
            channels_unsupported: 1,
            blocks_total: 100,
            blocks_read: 99,
            blocks_skipped_unsupported: 1,
            ..ReaderStats::default()
        };
        stats.trailer_seen = true;
        let s = format!("{stats}");
        assert!(s.contains("File size:"));
        assert!(s.contains("Channels total:        5"));
        assert!(s.contains("Trailer block:         present"));
    }
}
