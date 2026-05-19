// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! Integration tests: exercise `parse_magic_header` against every `.osf`
//! file shipped in `examples/` and `examples/generated/`.

use osf_core::{OsfVersion, parse_magic_header};
use std::ffi::OsStr;
use std::fs::{self, File};
use std::io::BufReader;
use std::path::{Path, PathBuf};

fn examples_root() -> PathBuf {
    // CARGO_MANIFEST_DIR points at .../implementations/rust/osf-core/.
    // Examples live two levels up at .../examples/.
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
fn every_example_file_has_a_parseable_magic_header() {
    let root = examples_root();
    let generated = root.join("generated");

    let mut files = Vec::new();
    collect_osf_files(&root, &mut files);
    collect_osf_files(&generated, &mut files);
    files.sort();

    assert!(
        !files.is_empty(),
        "no .osf files found under {}; the test needs the reference set",
        root.display()
    );

    let mut osf4_count = 0;
    let mut osf5_count = 0;

    for path in &files {
        let file = File::open(path).unwrap_or_else(|e| {
            panic!("cannot open {}: {e}", path.display());
        });
        let mut reader = BufReader::new(file);
        let header = parse_magic_header(&mut reader).unwrap_or_else(|e| {
            panic!("parse_magic_header failed for {}: {e}", path.display());
        });

        match header.version {
            OsfVersion::Osf4 => osf4_count += 1,
            OsfVersion::Osf5 => osf5_count += 1,
        }

        println!(
            "{:<60} version={:?} metablock_len={}",
            path.file_name().unwrap().to_string_lossy(),
            header.version,
            header.metablock_len
        );

        assert!(
            header.metablock_len > 0,
            "{} reports metablock_len=0",
            path.display()
        );
    }

    assert!(
        osf4_count >= 1,
        "expected at least one OSF4 file in the reference set"
    );
    assert!(
        osf5_count >= 1,
        "expected at least one OSF5 file in the reference set"
    );
}

#[test]
fn steam_loco_is_recognized_as_osf4() {
    let path = examples_root().join("steam_loco.osf");
    let file = File::open(&path).unwrap_or_else(|e| {
        panic!("cannot open {}: {e}", path.display());
    });
    let mut reader = BufReader::new(file);
    let header = parse_magic_header(&mut reader).unwrap();
    assert_eq!(header.version, OsfVersion::Osf4);
    assert!(header.metablock_len > 0);
}
