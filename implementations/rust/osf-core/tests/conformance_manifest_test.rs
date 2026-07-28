// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! Manifest-driven cross-implementation conformance test.
//!
//! Reads the shared `examples/reference_manifest.json` — the single source of
//! truth for the expected decoded contents of every reference file — and asserts
//! that `DataManager` decodes each listed file to match: version (via the
//! filename prefix), channel count, and per-channel index/name/datatype/
//! sample-count/mode, plus the integrity profile for entries that declare one,
//! plus the deliberate-anomaly counts (currently `zeroLengthBlocks`) declared
//! under the optional `anomalies` field. The anomaly assertion runs on every
//! entry, not only those with `anomalies` present: absent means the file is
//! well-formed and must report a zero count, present means the stated count is
//! exact — so a well-formed file that unexpectedly reports a skip is itself a
//! finding, not a silently-ignored corner case.
//!
//! Manifest keys may be sub-paths (e.g. `integrity/osf5_crc_equidistant.osf`);
//! they resolve under `examples/generated/`. Keeping this list in the manifest
//! (rather than hard-coded here) is what makes it a genuine cross-language
//! contract shared with the Java/C++/Delphi conformance tests.

use osf_core::{Channel, DataManager, DataType, IntegrityProfile, parse_data_type};
use serde::Deserialize;
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

fn examples_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .and_then(Path::parent)
        .expect("crate is rooted at .../implementations/rust/osf-core")
        .join("examples")
}

#[derive(Deserialize)]
struct FileEntry {
    version: u32,
    #[serde(default)]
    integrity: Option<String>,
    #[serde(default)]
    anomalies: Option<Anomalies>,
    channels: Vec<ChannelEntry>,
}

/// Deliberate non-conformances a corpus file carries. Absent for well-formed
/// files. Each field is the exact count a conforming reader must report.
#[derive(Deserialize)]
struct Anomalies {
    // No `#[serde(default)]` here, deliberately: once `anomalies` is present
    // in an entry, `zeroLengthBlocks` is required. Defaulting would let a
    // typo'd key silently deserialise to 0 and pass, which matters once
    // other-language ports re-spell this key by hand without yet having a
    // reader counter behind it.
    #[serde(rename = "zeroLengthBlocks")]
    zero_length_blocks: u64,
}

#[derive(Deserialize)]
struct ChannelEntry {
    index: u16,
    name: String,
    #[serde(rename = "dataType")]
    data_type: String,
    #[serde(rename = "sampleCount")]
    sample_count: usize,
    mode: String,
}

fn mode_of(ch: &Channel) -> &'static str {
    match ch {
        Channel::Equidistant(_) => "equidistant",
        Channel::Timestamped(_) => "timestamped",
        Channel::Variable(_) => "variable",
    }
}

fn integrity_of(token: &str) -> IntegrityProfile {
    match token {
        "crc32c" => IntegrityProfile::Crc32c,
        "ed25519" => IntegrityProfile::Ed25519,
        _ => IntegrityProfile::None,
    }
}

#[test]
fn conforms_to_reference_manifest() {
    let root = examples_root();
    let generated = root.join("generated");
    let manifest_path = root.join("reference_manifest.json");
    let text = std::fs::read_to_string(&manifest_path)
        .unwrap_or_else(|e| panic!("read {}: {e}", manifest_path.display()));
    let manifest: BTreeMap<String, FileEntry> =
        serde_json::from_str(&text).expect("reference_manifest.json is valid");
    assert!(!manifest.is_empty());

    for (key, entry) in &manifest {
        let path = generated.join(key);
        let mgr = DataManager::load_from_file(&path)
            .unwrap_or_else(|e| panic!("load {key}: {e}"));

        // Version inferred from the filename prefix.
        let base = Path::new(key).file_name().unwrap().to_str().unwrap();
        let want_version = if base.starts_with("osf4_") { 4 } else { 5 };
        assert_eq!(entry.version, want_version, "{key}: manifest version");

        // Integrity profile (optional).
        if let Some(token) = &entry.integrity {
            assert_eq!(mgr.stats.integrity, integrity_of(token), "{key}: integrity");
            assert_eq!(mgr.stats.blocks_crc_failed, 0, "{key}: crc failures");
        }

        // Deliberate non-conformances (optional). A file that declares none
        // must report none — that is what keeps a well-formed corpus honest.
        // Unlike the integrity check above (only asserted when `integrity` is
        // declared, since a file with no integrity profile has no frame CRCs
        // to fail), this runs unconditionally: any file at all, declared or
        // not, can carry a zero-length block, so "not declared" must mean
        // zero rather than "not checked".
        let want_zero_len = entry.anomalies.as_ref().map_or(0, |a| a.zero_length_blocks);
        assert_eq!(
            mgr.stats.blocks_skipped_zero_length, want_zero_len,
            "{key}: anomalies.zeroLengthBlocks (left = reader, right = manifest)"
        );

        let channels = mgr.channels();
        assert_eq!(channels.len(), entry.channels.len(), "{key}: channel count");

        for ce in &entry.channels {
            let ch = channels
                .iter()
                .find(|c| c.index() == ce.index)
                .unwrap_or_else(|| panic!("{key}: channel index {}", ce.index));
            assert_eq!(ch.name(), ce.name, "{key}: name of channel {}", ce.index);
            let want_dt: DataType = parse_data_type(&ce.data_type)
                .unwrap_or_else(|e| panic!("{key}: bad manifest dataType {:?}: {e}", ce.data_type));
            assert_eq!(ch.data_type(), want_dt, "{key}: dataType of channel {}", ce.index);
            assert_eq!(ch.sample_count(), ce.sample_count, "{key}: sampleCount of channel {}", ce.index);
            assert_eq!(mode_of(ch), ce.mode, "{key}: mode of channel {}", ce.index);
        }
    }
}
