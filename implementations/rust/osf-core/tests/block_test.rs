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

//! Integration test: drive `BlockReader` over every shipped `.osf`
//! file. Verifies that the framing layer stays aligned across all
//! supported block types and against the real-world field samples
//! (`steam_loco.osf`, `motorbike.osf`).

use osf_core::{
    BlockKind, BlockReader, NumericPayload, OsfVersion, TimestampedPayload, parse_magic_header,
    parse_metablock,
};
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

#[derive(Default)]
struct Counts {
    start_data: u64,
    continued_data: u64,
    abs_timestamp: u64,
    continued_rel: u64,
    skipped: u64,
}

fn read_all_blocks(path: &Path) -> (Counts, u64) {
    let mut reader = BufReader::new(
        File::open(path).unwrap_or_else(|e| panic!("open {}: {e}", path.display())),
    );
    let header = parse_magic_header(&mut reader).expect("magic header");
    let mut body = vec![0u8; header.metablock_len as usize];
    reader.read_exact(&mut body).expect("metablock body");
    let meta = parse_metablock(header.version, &body).expect("metablock parse");

    let mut block_reader = BlockReader::new(reader, &meta);
    let mut counts = Counts::default();
    let mut samples_total: u64 = 0;

    for block in &mut block_reader {
        let block = block.unwrap_or_else(|e| panic!("block error in {}: {e}", path.display()));
        match block.kind {
            BlockKind::StartData { samples, .. } => {
                counts.start_data += 1;
                samples_total += samples_len(&samples) as u64;
            }
            BlockKind::ContinuedData { samples } => {
                counts.continued_data += 1;
                samples_total += samples_len(&samples) as u64;
            }
            BlockKind::AbsTimestampData { samples } => {
                counts.abs_timestamp += 1;
                samples_total += ts_samples_len(&samples) as u64;
            }
            BlockKind::ContinuedRelStampData { samples: _ } => {
                counts.continued_rel += 1;
            }
            BlockKind::Skipped { .. } => {
                counts.skipped += 1;
            }
        }
    }

    assert_eq!(
        block_reader.blocks_truncated(),
        0,
        "{} should not be truncated",
        path.display()
    );
    (counts, samples_total)
}

fn samples_len(p: &NumericPayload) -> usize {
    p.len()
}

fn ts_samples_len(p: &TimestampedPayload) -> usize {
    p.len()
}

#[test]
fn every_example_file_streams_blocks_without_error() {
    let root = examples_root();
    let generated = root.join("generated");

    let mut files = Vec::new();
    collect_osf_files(&root, &mut files);
    collect_osf_files(&generated, &mut files);
    files.sort();

    assert!(!files.is_empty());

    for path in &files {
        let (counts, samples) = read_all_blocks(path);
        let total = counts.start_data
            + counts.continued_data
            + counts.abs_timestamp
            + counts.continued_rel
            + counts.skipped;
        println!(
            "{:<40} blocks={:<6} (start={}, cont={}, ts={}, rel={}, skip={}) samples={}",
            path.file_name().unwrap().to_string_lossy(),
            total,
            counts.start_data,
            counts.continued_data,
            counts.abs_timestamp,
            counts.continued_rel,
            counts.skipped,
            samples
        );
        assert!(
            total > 0,
            "{} produced zero blocks",
            path.display()
        );
    }
}

#[test]
fn osf5_scalar_int64_first_blocks_are_typed_correctly() {
    let path = examples_root().join("generated/osf5_scalar_int64.osf");
    let mut reader = BufReader::new(File::open(&path).unwrap());
    let header = parse_magic_header(&mut reader).unwrap();
    let mut body = vec![0u8; header.metablock_len as usize];
    reader.read_exact(&mut body).unwrap();
    let meta = parse_metablock(header.version, &body).unwrap();

    assert_eq!(header.version, OsfVersion::Osf5);

    let mut block_reader = BlockReader::new(reader, &meta);
    let first = block_reader.next().unwrap().unwrap();
    assert_eq!(first.channel_index, 0);
    match first.kind {
        BlockKind::AbsTimestampData {
            samples: TimestampedPayload::Int64(v),
        } => {
            assert_eq!(v.len(), 1);
            assert_eq!(v[0].1, 0);
        }
        other => panic!("expected AbsTimestampData/Int64, got {other:?}"),
    }
    // Second block should also parse cleanly with the next i64 = 1_000_000.
    let second = block_reader.next().unwrap().unwrap();
    match second.kind {
        BlockKind::AbsTimestampData {
            samples: TimestampedPayload::Int64(v),
        } => {
            assert_eq!(v[0].1, 1_000_000);
        }
        other => panic!("expected AbsTimestampData/Int64, got {other:?}"),
    }
}
