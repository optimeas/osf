// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! Integration tests: walk every shipped `.osf` file, parse the magic
//! header, slice the metablock, and dispatch through `parse_metablock`.
//!
//! The assertion is intentionally narrow — every file must parse, every
//! file must declare at least one channel, and every channel must carry
//! a non-empty name and a `size_of_length_value` of 2 or 4. Deeper
//! schema checking (datatype-versus-block-content consistency) follows
//! when the block reader lands.

use osf_core::{ChannelType, DataType, OsfVersion, parse_magic_header, parse_metablock};
use std::ffi::OsStr;
use std::fs::{self, File};
use std::io::{BufReader, Read};
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
fn every_example_file_metablock_parses() {
    let root = examples_root();
    let generated = root.join("generated");

    let mut files = Vec::new();
    collect_osf_files(&root, &mut files);
    collect_osf_files(&generated, &mut files);
    files.sort();

    assert!(
        !files.is_empty(),
        "no .osf files found under {}",
        root.display()
    );

    for path in &files {
        let mut reader = BufReader::new(
            File::open(path).unwrap_or_else(|e| panic!("open {}: {e}", path.display())),
        );
        let header = parse_magic_header(&mut reader)
            .unwrap_or_else(|e| panic!("magic header in {}: {e}", path.display()));

        let mut body = vec![0u8; header.metablock_len as usize];
        reader
            .read_exact(&mut body)
            .unwrap_or_else(|e| panic!("read metablock in {}: {e}", path.display()));

        let mb = parse_metablock(header.version, &body)
            .unwrap_or_else(|e| panic!("parse_metablock for {}: {e}", path.display()));

        assert_eq!(
            mb.file_info.version,
            match header.version {
                OsfVersion::Osf4 => 4,
                OsfVersion::Osf5 => 5,
            },
            "version mismatch in {}",
            path.display()
        );

        assert!(
            !mb.channels.is_empty(),
            "{} declares zero channels",
            path.display()
        );

        for chan in &mb.channels {
            assert!(
                !chan.name.is_empty(),
                "{} channel index {} has empty name",
                path.display(),
                chan.index
            );
            assert!(
                matches!(chan.size_of_length_value, 2 | 4),
                "{} channel {:?} has size_of_length_value={}",
                path.display(),
                chan.name,
                chan.size_of_length_value
            );

            // Removed datatypes must never reach the metablock — the
            // parser would have errored earlier. Asserting it here keeps
            // the contract visible.
            match &chan.data_type {
                DataType::ByteArray => panic!(
                    "{} channel {:?} produced ByteArray, but parser must \
                     normalise to Binary",
                    path.display(),
                    chan.name
                ),
                DataType::Unsupported(s) => println!(
                    "info: {} channel {:?} carries Unsupported datatype {s:?}",
                    path.display(),
                    chan.name
                ),
                _ => {}
            }
            if let ChannelType::Unsupported(s) = &chan.channel_type {
                println!(
                    "info: {} channel {:?} carries Unsupported channel type {s:?}",
                    path.display(),
                    chan.name
                );
            }
        }

        println!(
            "{:<40} {:?} channels={} infos={}",
            path.file_name().unwrap().to_string_lossy(),
            header.version,
            mb.channels.len(),
            mb.infos.len()
        );
    }
}

#[test]
fn steam_loco_has_123_channels() {
    let path = examples_root().join("steam_loco.osf");
    let mut reader = BufReader::new(File::open(&path).unwrap());
    let header = parse_magic_header(&mut reader).unwrap();
    let mut body = vec![0u8; header.metablock_len as usize];
    reader.read_exact(&mut body).unwrap();
    let mb = parse_metablock(header.version, &body).unwrap();
    assert_eq!(mb.channels.len(), 123);
    // The deprecated scale/offset fields on every channel must have
    // been logged-and-dropped — they do not appear in our model.
    assert!(
        mb.channels
            .iter()
            .all(|c| matches!(c.data_type, DataType::Double)),
        "every steam_loco channel is recorded as a double"
    );
}

#[test]
fn osf5_mixed_has_four_channels_with_diverse_types() {
    let path = examples_root().join("generated/osf5_mixed.osf");
    let mut reader = BufReader::new(File::open(&path).unwrap());
    let header = parse_magic_header(&mut reader).unwrap();
    let mut body = vec![0u8; header.metablock_len as usize];
    reader.read_exact(&mut body).unwrap();
    let mb = parse_metablock(header.version, &body).unwrap();

    assert_eq!(mb.channels.len(), 4);

    let by_name: std::collections::HashMap<&str, &osf_core::MetaChannel> = mb
        .channels
        .iter()
        .map(|c| (c.name.as_str(), c))
        .collect();

    assert_eq!(
        by_name["Sensor/TemperatureC"].data_type,
        DataType::Double
    );
    assert_eq!(
        by_name["Sensor/PressureCounter"].data_type,
        DataType::Int16
    );
    assert_eq!(
        by_name["Sensor/Vibration10Hz"].time_increment_ns,
        Some(100_000_000)
    );
    assert_eq!(by_name["Sensor/AlarmFlag"].data_type, DataType::Bool);
}
