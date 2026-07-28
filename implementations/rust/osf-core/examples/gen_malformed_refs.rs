// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! Generate the non-conforming reference file into
//! `examples/generated/malformed/`:
//!
//! - `osf5_zero_length_block.osf` — one timestamped `double` channel carrying
//!   ten samples, with a zero-length data block sitting between the fifth and
//!   the sixth.
//!
//! A zero-length data block (per-channel length field `0`) is a non-conforming
//! writer artefact: a conforming block always carries at least its control
//! byte. Readers must skip it and keep scanning (OSF-UP3, DECISIONS §25).
//!
//! No OSF writer can produce such a block, so the file is assembled from two
//! writer outputs: everything of the first file, then the four-byte bad frame,
//! then the block section of the second file. The bad frame deliberately sits
//! *between* valid data rather than at the front, so a reader test proves the
//! scan **continues** rather than merely that it starts.
//!
//! Run with `cargo run --example gen_malformed_refs`.

use osf_core::header::parse_magic_header;
use osf_core::writer::{ChannelDef, WriterBuilder};
use osf_core::{DataManager, DataType};
use std::io::Cursor;
use std::path::PathBuf;

fn generated_dir() -> PathBuf {
    let mut p = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    // .../implementations/rust/osf-core -> repo root
    p.pop();
    p.pop();
    p.pop();
    p.join("examples").join("generated").join("malformed")
}

/// Write a one-channel OSF5 file holding `count` timestamped `f64` samples
/// starting at sample number `first`, and return its bytes.
fn half(first: i64, count: i64) -> Vec<u8> {
    let mut b = WriterBuilder::new().creator("osf-core:gen_malformed_refs");
    let idx = b
        .add_channel(ChannelDef {
            name: "Sensor/Double".into(),
            data_type: DataType::Double,
            ..Default::default()
        })
        .expect("add channel");
    let timestamps: Vec<i64> = (first..first + count)
        .map(|i| 1_000_000 + i * 1_000)
        .collect();
    let values: Vec<f64> = (first..first + count).map(|i| i as f64 * 0.5).collect();
    b.add_timestamped_samples_f64(idx, &timestamps, &values)
        .expect("add samples");
    let mut out = Vec::new();
    b.write_to(&mut out).expect("write to buffer");
    out
}

/// Byte offset at which an OSF5 file's block section starts: the magic-header
/// line plus the metablock.
fn block_section_start(bytes: &[u8]) -> usize {
    let mut cur = Cursor::new(bytes);
    let hdr = parse_magic_header(&mut cur).expect("generated file has a valid magic header");
    cur.position() as usize + hdr.metablock_len as usize
}

fn main() {
    let dir = generated_dir();
    std::fs::create_dir_all(&dir).expect("create malformed dir");

    let first = half(0, 5);
    let second = half(5, 5);

    let mut out = first;
    // The non-conforming frame: channel index 0, length field 0. Both halves
    // declare the default sizeoflengthvalue of 2, so the frame is four bytes
    // and carries no control byte at all.
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&second[block_section_start(&second)..]);

    let path = dir.join("osf5_zero_length_block.osf");
    std::fs::write(&path, &out).expect("write corpus file");

    // Self-verify: the file must read back as ten samples with exactly one
    // zero-length skip. If this ever fails the generator produced something
    // other than the intended anomaly.
    let mgr = DataManager::load_from_file(&path).expect("corpus file loads");
    assert_eq!(
        mgr.stats.blocks_skipped_zero_length, 1,
        "expected exactly one zero-length skip"
    );
    let ch = mgr.channel("Sensor/Double").expect("channel present");
    assert_eq!(ch.sample_count(), 10, "expected ten readable samples");

    println!("wrote {}", path.display());
}
