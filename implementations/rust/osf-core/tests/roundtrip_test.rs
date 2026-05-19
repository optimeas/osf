// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! Roundtrip integration test: read every shipped `.osf` file, write
//! it back through the OSF5 writer, read the written copy, and verify
//! that the two managers carry semantically identical channel data.
//!
//! Comparisons are exact — no ULP tolerance — because we never
//! compute on the values, just round-trip them through bytes. Any
//! drift would be a bug. To keep failure messages survivable when a
//! 19k-sample channel diverges, sample-level mismatches are limited
//! to the first five plus an "and N more" tail.

use osf_core::writer::write_to_file;
use osf_core::{
    Channel, DataManager, EquidistantChannel, NumericValues, OsfVersion, TimestampedChannel,
    VariableChannel, parse_magic_header,
};
use std::ffi::OsStr;
use std::fs::{self, File};
use std::io::{BufReader, Read};
use std::path::{Path, PathBuf};
use std::time::Instant;

const MAX_SAMPLE_MISMATCHES: usize = 5;

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
fn roundtrip_all_examples() {
    let root = examples_root();
    let generated = root.join("generated");
    let mut files = Vec::new();
    collect_osf_files(&root, &mut files);
    collect_osf_files(&generated, &mut files);
    files.sort();
    assert!(!files.is_empty());

    let tmp_dir = std::env::temp_dir();

    for path in &files {
        let stem = path
            .file_stem()
            .and_then(OsStr::to_str)
            .unwrap_or("osf_roundtrip");
        let tmp_path = tmp_dir.join(format!("osf_rt_{stem}.osf"));

        let mgr_a = DataManager::load_from_file(path)
            .unwrap_or_else(|e| panic!("load source {}: {e}", path.display()));
        write_to_file(&mgr_a, &tmp_path)
            .unwrap_or_else(|e| panic!("write {} -> {}: {e}", path.display(), tmp_path.display()));
        let mgr_b = DataManager::load_from_file(&tmp_path)
            .unwrap_or_else(|e| panic!("re-load {}: {e}", tmp_path.display()));

        assert_managers_equivalent(&mgr_a, &mgr_b, path);

        let _ = fs::remove_file(&tmp_path);
    }
}

#[test]
fn osf4_source_produces_osf5_output() {
    let path = examples_root().join("steam_loco.osf");
    let mgr = DataManager::load_from_file(&path).unwrap();
    let tmp = std::env::temp_dir().join("osf_rt_osf5_target.osf");
    write_to_file(&mgr, &tmp).unwrap();

    // Reopen and parse just the magic header to confirm OSF5.
    let mut reader = BufReader::new(File::open(&tmp).unwrap());
    let header = parse_magic_header(&mut reader).unwrap();
    assert_eq!(header.version, OsfVersion::Osf5);

    // First metablock byte must be `{` (JSON), not `<` (XML).
    let mut first = [0u8; 1];
    reader.read_exact(&mut first).unwrap();
    assert_eq!(first[0], b'{', "OSF5 metablock must start with '{{'");

    let _ = fs::remove_file(&tmp);
}

/// Performance smoke: write a steam_loco-equivalent file. Brief budget
/// is 100 ms in release / 500 ms in debug. Ignored by default; run
/// manually via `cargo test --release -- --ignored`.
#[test]
#[ignore]
fn write_steam_loco_within_budget() {
    let path = examples_root().join("steam_loco.osf");
    let mgr = DataManager::load_from_file(&path).unwrap();
    let tmp = std::env::temp_dir().join("osf_rt_perf.osf");
    // Warm up — first write may pay one-shot allocator costs.
    write_to_file(&mgr, &tmp).unwrap();

    let start = Instant::now();
    write_to_file(&mgr, &tmp).unwrap();
    let elapsed = start.elapsed();

    let budget_ms: u128 = if cfg!(debug_assertions) { 500 } else { 100 };
    println!(
        "write steam_loco: warmed run {:?}, budget {} ms",
        elapsed, budget_ms
    );
    assert!(
        elapsed.as_millis() <= budget_ms,
        "write took {elapsed:?}, budget {budget_ms} ms"
    );
    let _ = fs::remove_file(&tmp);
}

// -----------------------------------------------------------
// Comparison helpers.
// -----------------------------------------------------------

fn assert_managers_equivalent(a: &DataManager, b: &DataManager, source: &Path) {
    assert_eq!(
        a.channels().len(),
        b.channels().len(),
        "channel count differs for {}",
        source.display()
    );
    for (chan_a, chan_b) in a.channels().iter().zip(b.channels()) {
        assert_channels_equivalent(chan_a, chan_b, source);
    }
}

fn assert_channels_equivalent(a: &Channel, b: &Channel, source: &Path) {
    assert_eq!(
        a.name(),
        b.name(),
        "channel name differs for {}: {:?} vs {:?}",
        source.display(),
        a.name(),
        b.name(),
    );
    // Note: channel index is not part of the roundtrip contract.
    // The writer assigns sequential indices 0..N to maintain order;
    // sources with sparse indices (e.g. steam_loco.osf has index 52
    // for GPS.PosFixMode) get re-numbered without changing semantics.
    assert_eq!(
        a.data_type(),
        b.data_type(),
        "channel {:?} ({}): data type differs",
        a.name(),
        source.display()
    );
    assert_eq!(
        a.sample_count(),
        b.sample_count(),
        "channel {:?} ({}): sample count differs",
        a.name(),
        source.display()
    );

    match (a, b) {
        (Channel::Equidistant(ea), Channel::Equidistant(eb)) => {
            assert_equidistant_equivalent(ea, eb, source);
        }
        (Channel::Timestamped(ta), Channel::Timestamped(tb)) => {
            assert_timestamped_equivalent(ta, tb, source);
        }
        (Channel::Variable(va), Channel::Variable(vb)) => {
            assert_variable_equivalent(va, vb, source);
        }
        (a, b) => panic!(
            "channel {:?} ({}): variant differs after roundtrip ({:?} vs {:?})",
            a.name(),
            source.display(),
            channel_kind(a),
            channel_kind(b)
        ),
    }
}

fn assert_equidistant_equivalent(a: &EquidistantChannel, b: &EquidistantChannel, source: &Path) {
    assert_eq!(
        a.segments().len(),
        b.segments().len(),
        "channel {:?} ({}): segment count differs",
        a.name,
        source.display()
    );
    for (i, (sa, sb)) in a.segments().iter().zip(b.segments()).enumerate() {
        assert_eq!(
            sa.start_timestamp_ns, sb.start_timestamp_ns,
            "channel {:?} ({}): segment {i} start_timestamp differs",
            a.name,
            source.display()
        );
        assert!(
            sa.sample_rate_hz.to_bits() == sb.sample_rate_hz.to_bits(),
            "channel {:?} ({}): segment {i} sample_rate_hz differs ({} vs {})",
            a.name,
            source.display(),
            sa.sample_rate_hz,
            sb.sample_rate_hz
        );
        assert_eq!(
            sa.sample_count, sb.sample_count,
            "channel {:?} ({}): segment {i} sample_count differs",
            a.name,
            source.display()
        );
    }
    assert_numeric_values_equal(a.values(), b.values(), &a.name, source);
}

fn assert_timestamped_equivalent(a: &TimestampedChannel, b: &TimestampedChannel, source: &Path) {
    assert_eq!(
        a.timestamps_ns(),
        b.timestamps_ns(),
        "channel {:?} ({}): timestamps differ (showing only metadata; \
         see stack for index)",
        a.name,
        source.display()
    );
    assert_numeric_values_equal(a.values(), b.values(), &a.name, source);
}

fn assert_variable_equivalent(a: &VariableChannel, b: &VariableChannel, source: &Path) {
    assert_eq!(
        a.timestamps_ns(),
        b.timestamps_ns(),
        "channel {:?} ({}): timestamps differ",
        a.name,
        source.display()
    );
    if let (Ok(sa), Ok(sb)) = (a.as_strings(), b.as_strings()) {
        let mismatches = collect_mismatches(sa, sb, |a, b| a == b);
        if !mismatches.is_empty() {
            panic!(
                "channel {:?} ({}): string mismatches: {}",
                a.name,
                source.display(),
                format_mismatches(&mismatches, sa.len(), |idx| {
                    format!("[{idx}] left={:?} right={:?}", sa[idx], sb[idx])
                })
            );
        }
    } else if let (Ok(ba), Ok(bb)) = (a.as_binaries(), b.as_binaries()) {
        let mismatches = collect_mismatches(ba, bb, |a, b| a == b);
        if !mismatches.is_empty() {
            panic!(
                "channel {:?} ({}): binary mismatches: {}",
                a.name,
                source.display(),
                format_mismatches(&mismatches, ba.len(), |idx| {
                    format!(
                        "[{idx}] left=<{} bytes> right=<{} bytes>",
                        ba[idx].len(),
                        bb[idx].len()
                    )
                })
            );
        }
    } else {
        panic!(
            "channel {:?} ({}): variable storage variants differ between sides",
            a.name,
            source.display()
        );
    }
}

fn assert_numeric_values_equal(
    a: &NumericValues,
    b: &NumericValues,
    name: &str,
    source: &Path,
) {
    macro_rules! cmp_typed {
        ($a:expr, $b:expr, $eq:expr, $fmt:expr) => {{
            let mismatches = collect_mismatches($a, $b, $eq);
            if !mismatches.is_empty() {
                let total = $a.len();
                panic!(
                    "channel {name:?} ({}): numeric mismatches: {}",
                    source.display(),
                    format_mismatches(&mismatches, total, $fmt)
                );
            }
        }};
    }

    match (a, b) {
        (NumericValues::Bool(va), NumericValues::Bool(vb)) => {
            cmp_typed!(va, vb, |a, b| a == b, |idx| format!(
                "[{idx}] left={} right={}",
                va[idx], vb[idx]
            ));
        }
        (NumericValues::Int8(va), NumericValues::Int8(vb)) => {
            cmp_typed!(va, vb, |a, b| a == b, |idx| format!(
                "[{idx}] left={} right={}",
                va[idx], vb[idx]
            ));
        }
        (NumericValues::Int16(va), NumericValues::Int16(vb)) => {
            cmp_typed!(va, vb, |a, b| a == b, |idx| format!(
                "[{idx}] left={} right={}",
                va[idx], vb[idx]
            ));
        }
        (NumericValues::Int32(va), NumericValues::Int32(vb)) => {
            cmp_typed!(va, vb, |a, b| a == b, |idx| format!(
                "[{idx}] left={} right={}",
                va[idx], vb[idx]
            ));
        }
        (NumericValues::Int64(va), NumericValues::Int64(vb)) => {
            cmp_typed!(va, vb, |a, b| a == b, |idx| format!(
                "[{idx}] left={} right={}",
                va[idx], vb[idx]
            ));
        }
        (NumericValues::UInt8(va), NumericValues::UInt8(vb)) => {
            cmp_typed!(va, vb, |a, b| a == b, |idx| format!(
                "[{idx}] left={} right={}",
                va[idx], vb[idx]
            ));
        }
        (NumericValues::UInt16(va), NumericValues::UInt16(vb)) => {
            cmp_typed!(va, vb, |a, b| a == b, |idx| format!(
                "[{idx}] left={} right={}",
                va[idx], vb[idx]
            ));
        }
        (NumericValues::UInt32(va), NumericValues::UInt32(vb)) => {
            cmp_typed!(va, vb, |a, b| a == b, |idx| format!(
                "[{idx}] left={} right={}",
                va[idx], vb[idx]
            ));
        }
        (NumericValues::UInt64(va), NumericValues::UInt64(vb)) => {
            cmp_typed!(va, vb, |a, b| a == b, |idx| format!(
                "[{idx}] left={} right={}",
                va[idx], vb[idx]
            ));
        }
        (NumericValues::Float(va), NumericValues::Float(vb)) => {
            // Bitwise compare to catch NaN payloads identically.
            cmp_typed!(va, vb, |a, b| a.to_bits() == b.to_bits(), |idx| format!(
                "[{idx}] left={} right={}",
                va[idx], vb[idx]
            ));
        }
        (NumericValues::Double(va), NumericValues::Double(vb)) => {
            cmp_typed!(va, vb, |a, b| a.to_bits() == b.to_bits(), |idx| format!(
                "[{idx}] left={} right={}",
                va[idx], vb[idx]
            ));
        }
        (NumericValues::GpsLocation(va), NumericValues::GpsLocation(vb)) => {
            cmp_typed!(
                va,
                vb,
                |a, b| a.latitude.to_bits() == b.latitude.to_bits()
                    && a.longitude.to_bits() == b.longitude.to_bits()
                    && a.altitude.to_bits() == b.altitude.to_bits(),
                |idx| format!(
                    "[{idx}] left=({},{},{}) right=({},{},{})",
                    va[idx].latitude,
                    va[idx].longitude,
                    va[idx].altitude,
                    vb[idx].latitude,
                    vb[idx].longitude,
                    vb[idx].altitude
                )
            );
        }
        (a, b) => panic!(
            "channel {name:?} ({}): NumericValues variant differs ({:?} vs {:?})",
            source.display(),
            a.data_type(),
            b.data_type()
        ),
    }
}

/// Collect indices where two slices differ. Stops scanning after the
/// limit; the total mismatch count is recoverable from `(slice.len()
/// - matches)` so callers can show "and N more".
fn collect_mismatches<T, F>(a: &[T], b: &[T], eq: F) -> Vec<usize>
where
    F: Fn(&T, &T) -> bool,
{
    if a.len() != b.len() {
        return (0..a.len().min(b.len())).collect();
    }
    let mut mismatches = Vec::new();
    for i in 0..a.len() {
        if !eq(&a[i], &b[i]) {
            mismatches.push(i);
            if mismatches.len() > MAX_SAMPLE_MISMATCHES {
                // Keep one extra so we can detect "more than 5".
                break;
            }
        }
    }
    mismatches
}

fn format_mismatches<F>(mismatches: &[usize], total: usize, fmt: F) -> String
where
    F: Fn(usize) -> String,
{
    let shown = mismatches.iter().take(MAX_SAMPLE_MISMATCHES);
    let mut out = String::new();
    for idx in shown {
        if !out.is_empty() {
            out.push_str("; ");
        }
        out.push_str(&fmt(*idx));
    }
    if mismatches.len() > MAX_SAMPLE_MISMATCHES {
        // We collected MAX+1 to know "more than 5"; the actual total
        // could be higher, but we know at least 6 differ.
        out.push_str(&format!(
            "; and at least {} more (channel has {total} samples)",
            mismatches.len() - MAX_SAMPLE_MISMATCHES
        ));
    }
    out
}

fn channel_kind(c: &Channel) -> &'static str {
    match c {
        Channel::Equidistant(_) => "Equidistant",
        Channel::Timestamped(_) => "Timestamped",
        Channel::Variable(_) => "Variable",
    }
}
