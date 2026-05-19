// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! OSFZ integration tests:
//!
//! 1. `weather_station.osfz` — the field sample from a deployed
//!    Optimeas device. Confirms gzip-OSFZ end-to-end against a real
//!    file we cannot synthesise locally.
//! 2. Synthetic gzip and zlib re-wraps of `steam_loco.osf` — proves
//!    the manager produces an identical channel set whether it reads
//!    the file as plain OSF or as either OSFZ flavour.

use flate2::Compression;
use flate2::write::{GzEncoder, ZlibEncoder};
use osf_core::{Channel, CompressionFormat, DataManager};
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

fn examples_root() -> PathBuf {
    let manifest = Path::new(env!("CARGO_MANIFEST_DIR"));
    manifest
        .parent()
        .and_then(Path::parent)
        .and_then(Path::parent)
        .expect("crate is rooted at .../implementations/rust/osf-core")
        .join("examples")
}

#[test]
fn load_weather_station_osfz_field_sample() {
    let path = examples_root().join("weather_station.osfz");
    let mgr = DataManager::load_from_file(&path)
        .unwrap_or_else(|e| panic!("load {}: {e}", path.display()));

    assert!(
        mgr.stats.compressed,
        "weather_station.osfz must be detected as compressed"
    );
    assert_eq!(
        mgr.stats.compression_format,
        CompressionFormat::Gzip,
        "weather_station.osfz uses gzip compression (current Optimeas device output)"
    );
    assert!(
        !mgr.channels().is_empty(),
        "weather_station.osfz must declare at least one channel"
    );

    let channels_with_data = mgr
        .channels()
        .iter()
        .filter(|c| c.sample_count() > 0)
        .count();
    assert!(
        channels_with_data > 0,
        "weather_station.osfz must contain at least one channel with samples"
    );
}

#[test]
fn synthetic_gzip_osfz_matches_uncompressed_source() {
    let original_path = examples_root().join("steam_loco.osf");
    let original_bytes = fs::read(&original_path).unwrap();
    let original = DataManager::load_from_file(&original_path).unwrap();
    assert!(!original.stats.compressed);

    let temp = std::env::temp_dir().join("synthetic_steam_loco_gzip.osfz");
    {
        let mut enc = GzEncoder::new(Vec::new(), Compression::default());
        enc.write_all(&original_bytes).unwrap();
        let compressed = enc.finish().unwrap();
        fs::write(&temp, &compressed).unwrap();
    }

    let from_osfz = DataManager::load_from_file(&temp).unwrap();
    assert!(from_osfz.stats.compressed);
    assert_eq!(from_osfz.stats.compression_format, CompressionFormat::Gzip);
    assert_managers_equal(&original, &from_osfz);
    let _ = fs::remove_file(&temp);
}

#[test]
fn synthetic_zlib_osfz_matches_uncompressed_source() {
    let original_path = examples_root().join("steam_loco.osf");
    let original_bytes = fs::read(&original_path).unwrap();
    let original = DataManager::load_from_file(&original_path).unwrap();

    let temp = std::env::temp_dir().join("synthetic_steam_loco_zlib.osfz");
    {
        let mut enc = ZlibEncoder::new(Vec::new(), Compression::default());
        enc.write_all(&original_bytes).unwrap();
        let compressed = enc.finish().unwrap();
        fs::write(&temp, &compressed).unwrap();
    }

    let from_osfz = DataManager::load_from_file(&temp).unwrap();
    assert!(from_osfz.stats.compressed);
    assert_eq!(from_osfz.stats.compression_format, CompressionFormat::Zlib);
    assert_managers_equal(&original, &from_osfz);
    let _ = fs::remove_file(&temp);
}

#[test]
fn plain_osf_keeps_compression_flag_clear() {
    let path = examples_root().join("steam_loco.osf");
    let mgr = DataManager::load_from_file(&path).unwrap();
    assert!(!mgr.stats.compressed);
    assert_eq!(mgr.stats.compression_format, CompressionFormat::None);
}

/// Coarse comparison — channel order, names, sample counts, and a
/// spot-check on data types. The full bit-level comparison lives in
/// `roundtrip_test.rs`; here we are asserting that decompression
/// does not perturb the manager output relative to a plain read of
/// the same source bytes.
fn assert_managers_equal(a: &DataManager, b: &DataManager) {
    assert_eq!(
        a.channels().len(),
        b.channels().len(),
        "channel count differs: {} vs {}",
        a.channels().len(),
        b.channels().len()
    );
    for (chan_a, chan_b) in a.channels().iter().zip(b.channels()) {
        assert_eq!(
            chan_a.name(),
            chan_b.name(),
            "channel name differs at index {} ({:?} vs {:?})",
            chan_a.index(),
            chan_a.name(),
            chan_b.name()
        );
        assert_eq!(
            chan_a.data_type(),
            chan_b.data_type(),
            "channel {:?} data type differs ({:?} vs {:?})",
            chan_a.name(),
            chan_a.data_type(),
            chan_b.data_type()
        );
        assert_eq!(
            chan_a.sample_count(),
            chan_b.sample_count(),
            "channel {:?} sample count differs ({} vs {})",
            chan_a.name(),
            chan_a.sample_count(),
            chan_b.sample_count()
        );
        assert_eq!(
            channel_kind(chan_a),
            channel_kind(chan_b),
            "channel {:?} variant differs",
            chan_a.name()
        );
    }
}

fn channel_kind(c: &Channel) -> &'static str {
    match c {
        Channel::Equidistant(_) => "equidistant",
        Channel::Timestamped(_) => "timestamped",
        Channel::Variable(_) => "variable",
    }
}
