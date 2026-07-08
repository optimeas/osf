// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! Integration tests for the OSF5 integrity profile, level `crc`:
//!
//! - Round-trip every shipped `.osf` file through the writer with
//!   `with_integrity(Crc32c)` and confirm the reload reports level `crc`,
//!   zero CRC failures, and byte-identical sample data.
//! - Negative cases: flip a single byte in (a) the metablock, (b) a numeric
//!   data block, (c) a string data block, and confirm the documented
//!   rejection / skip behaviour.
//! - A gzip-wrapped CRC file reads transparently.

use flate2::Compression;
use flate2::write::GzEncoder;
use osf_core::writer::{ChannelDef, WriterBuilder};
use osf_core::{
    Channel, DataManager, DataType, IntegrityProfile, NumericValues, OsfError, parse_magic_header,
};
use std::ffi::OsStr;
use std::fs;
use std::io::{Cursor, Write};
use std::path::{Path, PathBuf};

fn examples_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .and_then(Path::parent)
        .expect("crate is rooted at .../implementations/rust/osf-core")
        .join("examples")
}

fn collect_osf_files(dir: &Path, out: &mut Vec<PathBuf>) {
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_file() && path.extension() == Some(OsStr::new("osf")) {
                out.push(path);
            }
        }
    }
}

/// Write a manager back out at integrity level `crc` into a byte buffer.
fn write_with_crc(mgr: &DataManager) -> Vec<u8> {
    let mut builder = WriterBuilder::from_manager(mgr).expect("from_manager");
    builder = builder.with_integrity(IntegrityProfile::Crc32c);
    let mut buf = Vec::new();
    builder.write_to(&mut buf).expect("write_to");
    buf
}

#[test]
fn roundtrip_all_examples_with_crc() {
    let root = examples_root();
    let mut files = Vec::new();
    collect_osf_files(&root, &mut files);
    collect_osf_files(&root.join("generated"), &mut files);
    files.sort();
    assert!(!files.is_empty());

    for path in &files {
        let mgr_a = DataManager::load_from_file(path)
            .unwrap_or_else(|e| panic!("load {}: {e}", path.display()));
        let bytes = write_with_crc(&mgr_a);

        // The header must carry a crc32c token.
        let nl = bytes.iter().position(|&b| b == b'\n').unwrap();
        let header = std::str::from_utf8(&bytes[..nl]).unwrap();
        assert!(
            header.contains(" crc32c:"),
            "{}: header missing crc32c token: {header:?}",
            path.display()
        );

        let mgr_b = DataManager::load_from_reader(Cursor::new(bytes))
            .unwrap_or_else(|e| panic!("reload crc {}: {e}", path.display()));
        assert_eq!(
            mgr_b.stats.integrity,
            IntegrityProfile::Crc32c,
            "{}: integrity not reported",
            path.display()
        );
        assert_eq!(
            mgr_b.stats.blocks_crc_failed,
            0,
            "{}: unexpected CRC failures",
            path.display()
        );
        assert_same_data(&mgr_a, &mgr_b, path);
    }
}

#[test]
fn reference_crc_files_load_at_level_crc() {
    let dir = examples_root().join("generated").join("integrity");
    for (name, channels) in [
        ("osf5_crc_equidistant.osf", 3usize),
        ("osf5_crc_variable.osf", 2usize),
    ] {
        let path = dir.join(name);
        let mgr = DataManager::load_from_file(&path)
            .unwrap_or_else(|e| panic!("load {}: {e}", path.display()));
        assert_eq!(
            mgr.stats.integrity,
            IntegrityProfile::Crc32c,
            "{name}: integrity"
        );
        assert_eq!(mgr.stats.blocks_crc_failed, 0, "{name}: crc failures");
        assert_eq!(mgr.stats.verification_status(), "crc_valid", "{name}: status");
        assert_eq!(mgr.channels().len(), channels, "{name}: channel count");
    }
}

#[test]
fn corrupt_metablock_byte_rejects_file() {
    let bytes = crc_file_numeric();
    let (header_len, metablock_len) = header_and_metablock_len(&bytes);
    let mut corrupt = bytes.clone();
    // Flip a byte inside the metablock JSON.
    corrupt[header_len + metablock_len / 2] ^= 0xFF;
    let res = DataManager::load_from_reader(Cursor::new(corrupt));
    assert!(
        matches!(res, Err(OsfError::MetablockCrcMismatch { .. })),
        "expected MetablockCrcMismatch, got {:?}",
        res.as_ref().err()
    );
}

#[test]
fn corrupt_numeric_block_byte_is_skipped() {
    let bytes = crc_file_numeric();
    let (header_len, metablock_len) = header_and_metablock_len(&bytes);
    let mut corrupt = bytes.clone();
    // Flip a byte a few bytes into the first data block's payload
    // (past channel index + length field + control byte).
    let data_start = header_len + metablock_len;
    corrupt[data_start + 6] ^= 0xFF;

    let mgr = DataManager::load_from_reader(Cursor::new(corrupt)).unwrap();
    assert!(
        mgr.stats.blocks_crc_failed >= 1,
        "expected at least one CRC failure, got {}",
        mgr.stats.blocks_crc_failed
    );
    assert_eq!(mgr.stats.verification_status(), "invalid");
}

#[test]
fn corrupt_string_block_byte_is_skipped() {
    let bytes = crc_file_string();
    let (header_len, metablock_len) = header_and_metablock_len(&bytes);
    let mut corrupt = bytes.clone();
    let data_start = header_len + metablock_len;
    // Flip a byte inside the first string block's payload.
    corrupt[data_start + 6] ^= 0xFF;

    let mgr = DataManager::load_from_reader(Cursor::new(corrupt)).unwrap();
    assert!(
        mgr.stats.blocks_crc_failed >= 1,
        "expected at least one CRC failure on the string block, got {}",
        mgr.stats.blocks_crc_failed
    );
}

#[test]
fn gzip_wrapped_crc_file_reads_transparently() {
    let bytes = crc_file_numeric();
    let mut encoder = GzEncoder::new(Vec::new(), Compression::default());
    encoder.write_all(&bytes).unwrap();
    let gz = encoder.finish().unwrap();

    let mgr = DataManager::load_from_reader(Cursor::new(gz)).unwrap();
    assert!(mgr.stats.compressed, "should be detected as compressed");
    assert_eq!(mgr.stats.integrity, IntegrityProfile::Crc32c);
    assert_eq!(mgr.stats.blocks_crc_failed, 0);
    assert_eq!(mgr.channels().len(), 1);
}

// ------------------------------------------------------------------
// Fixtures.
// ------------------------------------------------------------------

/// A minimal single-channel numeric (equidistant f64) CRC file.
fn crc_file_numeric() -> Vec<u8> {
    let mut b = WriterBuilder::new();
    let c = b
        .add_channel(ChannelDef {
            name: "Sensor/Value".into(),
            data_type: DataType::Double,
            ..Default::default()
        })
        .unwrap();
    b.add_equidistant_segment_f64(c, 1_000, 100.0, &[1.0, 2.0, 3.0, 4.0, 5.0])
        .unwrap();
    let mut buf = Vec::new();
    b.with_integrity(IntegrityProfile::Crc32c)
        .write_to(&mut buf)
        .unwrap();
    buf
}

/// A minimal single-channel string CRC file.
fn crc_file_string() -> Vec<u8> {
    let mut b = WriterBuilder::new();
    let c = b
        .add_channel(ChannelDef {
            name: "Sensor/Log".into(),
            data_type: DataType::String,
            ..Default::default()
        })
        .unwrap();
    b.add_string_samples(
        c,
        &[10, 20, 30],
        &["alpha".to_string(), "beta".to_string(), "gamma".to_string()],
    )
    .unwrap();
    let mut buf = Vec::new();
    b.with_integrity(IntegrityProfile::Crc32c)
        .write_to(&mut buf)
        .unwrap();
    buf
}

/// Parse the header length (bytes up to and including the `\n`) and the
/// metablock byte length from a file image.
fn header_and_metablock_len(bytes: &[u8]) -> (usize, usize) {
    let nl = bytes.iter().position(|&b| b == b'\n').unwrap();
    let header_len = nl + 1;
    let mut cur = Cursor::new(bytes);
    let header = parse_magic_header(&mut cur).unwrap();
    (header_len, header.metablock_len as usize)
}

// ------------------------------------------------------------------
// Comparison.
// ------------------------------------------------------------------

fn assert_same_data(a: &DataManager, b: &DataManager, source: &Path) {
    assert_eq!(
        a.channels().len(),
        b.channels().len(),
        "{}: channel count differs",
        source.display()
    );
    for (ca, cb) in a.channels().iter().zip(b.channels()) {
        assert_eq!(ca.name(), cb.name(), "{}: channel name", source.display());
        assert_eq!(
            ca.data_type(),
            cb.data_type(),
            "{}: {:?} data type",
            source.display(),
            ca.name()
        );
        assert_eq!(
            ca.sample_count(),
            cb.sample_count(),
            "{}: {:?} sample count",
            source.display(),
            ca.name()
        );
        match (ca, cb) {
            (Channel::Equidistant(ea), Channel::Equidistant(eb)) => {
                assert_numeric_eq(ea.values(), eb.values(), ca.name(), source);
            }
            (Channel::Timestamped(ta), Channel::Timestamped(tb)) => {
                assert_eq!(
                    ta.timestamps_ns(),
                    tb.timestamps_ns(),
                    "{}: {:?} timestamps",
                    source.display(),
                    ca.name()
                );
                assert_numeric_eq(ta.values(), tb.values(), ca.name(), source);
            }
            (Channel::Variable(va), Channel::Variable(vb)) => {
                assert_eq!(
                    va.timestamps_ns(),
                    vb.timestamps_ns(),
                    "{}: {:?} timestamps",
                    source.display(),
                    ca.name()
                );
                if let (Ok(sa), Ok(sb)) = (va.as_strings(), vb.as_strings()) {
                    assert_eq!(sa, sb, "{}: {:?} strings", source.display(), ca.name());
                } else if let (Ok(ba), Ok(bb)) = (va.as_binaries(), vb.as_binaries()) {
                    assert_eq!(ba, bb, "{}: {:?} binaries", source.display(), ca.name());
                } else {
                    panic!("{}: {:?} variable variant differs", source.display(), ca.name());
                }
            }
            _ => panic!("{}: {:?} channel variant differs", source.display(), ca.name()),
        }
    }
}

fn assert_numeric_eq(a: &NumericValues, b: &NumericValues, name: &str, source: &Path) {
    use NumericValues::{Double, Float};
    let ok = match (a, b) {
        // Bit-compare floats so NaN payloads are treated as equal iff identical.
        (Double(x), Double(y)) => x.iter().map(|v| v.to_bits()).eq(y.iter().map(|v| v.to_bits())),
        (Float(x), Float(y)) => x.iter().map(|v| v.to_bits()).eq(y.iter().map(|v| v.to_bits())),
        _ => a == b,
    };
    assert!(ok, "{}: {name:?} numeric values differ", source.display());
}
