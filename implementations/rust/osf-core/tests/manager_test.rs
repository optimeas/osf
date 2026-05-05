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

//! Integration tests: drive `DataManager::load_from_file` over every
//! shipped `.osf` file and verify the assembled channel list.
//!
//! Plus a `#[ignore]`-gated performance smoke test that confirms
//! `steam_loco.osf` loads inside the brief's budget (≤ 100 ms in
//! release, ≤ 200 ms in debug). Run it manually:
//!
//! ```bash
//! cargo test -- --ignored
//! cargo test --release -- --ignored
//! ```

use osf_core::{Channel, DataManager};
use std::ffi::OsStr;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::Instant;

fn examples_root() -> PathBuf {
    let manifest = Path::new(env!("CARGO_MANIFEST_DIR"));
    manifest
        .parent()
        .and_then(Path::parent)
        .and_then(Path::parent)
        .expect("crate is rooted at .../implementations/rust/osf-core")
        .join("examples")
}

fn collect_osf_files(dir: &Path, out: &mut Vec<PathBuf>) {
    let entries = match fs::read_dir(dir) {
        Ok(e) => e,
        Err(_) => return,
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_file() && path.extension() == Some(OsStr::new("osf")) {
            out.push(path);
        }
    }
}

#[test]
fn every_example_file_loads_into_data_manager() {
    let root = examples_root();
    let generated = root.join("generated");

    let mut files = Vec::new();
    collect_osf_files(&root, &mut files);
    collect_osf_files(&generated, &mut files);
    files.sort();
    assert!(!files.is_empty());

    for path in &files {
        let mgr = DataManager::load_from_file(path)
            .unwrap_or_else(|e| panic!("load {}: {e}", path.display()));

        assert!(
            !mgr.channels().is_empty(),
            "{} produced no channels",
            path.display()
        );

        let total_samples: usize = mgr.channels().iter().map(Channel::sample_count).sum();
        assert!(
            mgr.channels().iter().any(|c| c.sample_count() > 0)
                || total_samples == 0
                    && mgr.stats.blocks_total == mgr.stats.blocks_skipped_unsupported
                        + mgr.stats.blocks_skipped_deprecated_type
                        + mgr.stats.blocks_skipped_reserved_type,
            "{} declared channels but no sample reached any of them",
            path.display()
        );

        // Every channel must be addressable by its name and by its
        // index (DECISIONS §10).
        for chan in mgr.channels() {
            let by_name = mgr.channel(chan.name()).expect("channel by name");
            let by_index = mgr
                .channel_by_index(chan.index())
                .expect("channel by index");
            assert_eq!(by_name as *const _, by_index as *const _);
        }

        println!(
            "{:<40} channels={:<3} total_samples={}",
            path.file_name().unwrap().to_string_lossy(),
            mgr.channels().len(),
            total_samples
        );
    }
}

#[test]
fn steam_loco_channel_lookup_by_known_name() {
    let path = examples_root().join("steam_loco.osf");
    let mgr = DataManager::load_from_file(&path).unwrap();
    // GPS.PosFixMode is one of the well-known channels in this field
    // sample. If the metablock parser ever changes how it normalises
    // names this test will catch the regression.
    let gps = mgr.channel("GPS.PosFixMode").expect("GPS.PosFixMode missing");
    assert_eq!(gps.name(), "GPS.PosFixMode");
    assert!(gps.sample_count() > 0);
}

#[test]
fn osf5_mixed_extended_has_expected_channels() {
    let path = examples_root().join("generated/osf5_mixed_extended.osf");
    let mgr = DataManager::load_from_file(&path).unwrap();
    assert_eq!(mgr.channels().len(), 5);
    // All five channels exist; some may be empty depending on the
    // generator's choices, but at least one must hold samples.
    assert!(mgr.channels().iter().any(|c| c.sample_count() > 0));
}

/// Performance smoke. Bound is the brief's budget. Ignored by default
/// so CI never sees it; run manually via
/// `cargo test --release -- --ignored`.
#[test]
#[ignore]
fn steam_loco_load_time_within_budget() {
    let path = examples_root().join("steam_loco.osf");
    let start = Instant::now();
    let mgr = DataManager::load_from_file(&path).unwrap();
    let elapsed = start.elapsed();

    // Warm-up amortises file-system caches; do a second run as the
    // measurement.
    let start = Instant::now();
    let _mgr2 = DataManager::load_from_file(&path).unwrap();
    let elapsed2 = start.elapsed();

    let budget_ms = if cfg!(debug_assertions) { 200 } else { 100 };
    println!(
        "steam_loco load: warm-up {:?}, measured {:?}, budget {} ms",
        elapsed, elapsed2, budget_ms
    );
    assert!(
        elapsed2.as_millis() <= budget_ms,
        "steam_loco load took {:?}, budget {} ms",
        elapsed2,
        budget_ms
    );
    assert_eq!(mgr.channels().len(), 123);
}
