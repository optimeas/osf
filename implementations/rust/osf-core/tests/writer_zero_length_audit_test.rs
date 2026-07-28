// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! OSF-UP3 writer negative proof: the Rust writer never emits a data block
//! whose per-channel length field reads `0`.
//!
//! A zero-length data block is a non-conforming writer artefact — a conforming
//! block always carries at least its control byte (DECISIONS §25). Blocks of
//! that shape were observed in real field recordings in July 2026 and the
//! producing writer is still unknown; these tests clear the Rust writer (and
//! therefore the Python bindings, which are a pass-through to it —
//! `implementations/python/src/writer.rs:167-282`).
//!
//! Two independent assertions per case:
//!
//! 1. **Byte level.** Walk the raw block stream and check every length field
//!    directly. This does not depend on the reader agreeing with the writer.
//! 2. **Reader level.** Read the file back and assert
//!    `stats.blocks_skipped_zero_length == 0` — the counter the readers gained
//!    for exactly this anomaly — plus the sample data, so a file that is
//!    "clean" only because the reader gave up early cannot pass.
//!
//! The cases are the three risk shapes the audit enumerated: chunking-loop
//! boundaries (including payloads that divide *exactly* by the chunk size),
//! empty variable-length (`string` / `binary`) samples, and channels that are
//! declared but never written to. The empty-equidistant-segment case is the
//! one place where this writer emits a block for zero samples at all
//! (`writer.rs:571` writes the `bcStartData` opener unconditionally), so it is
//! the case with genuine doubt rather than a formality.

use osf_core::writer::{ChannelDef, WriterBuilder};
use osf_core::{DataManager, DataType, parse_magic_header};
use std::io::Cursor;

/// One `[u16 channel][u16 len]` frame header of the block stream.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Frame {
    channel: u16,
    len: u16,
}

/// Walk the block stream of `bytes` and return every frame header.
///
/// Assumes `sizeoflengthvalue == 2` for all channels — asserted against the
/// metablock so a future default change fails here instead of silently
/// mis-parsing.
fn frames(bytes: &[u8]) -> Vec<Frame> {
    let mut cur = Cursor::new(bytes);
    let hdr = parse_magic_header(&mut cur).expect("valid magic header");
    let meta_start = cur.position() as usize;
    let meta_end = meta_start + hdr.metablock_len as usize;
    let meta = osf_core::parse_metablock(hdr.version, &bytes[meta_start..meta_end])
        .expect("metablock parses");
    for ch in &meta.channels {
        assert_eq!(
            ch.size_of_length_value, 2,
            "channel {} declares sizeoflengthvalue {} - this frame walker \
             assumes 2",
            ch.index, ch.size_of_length_value
        );
    }

    let mut out = Vec::new();
    let mut pos = meta_end;
    while pos + 4 <= bytes.len() {
        let channel = u16::from_le_bytes([bytes[pos], bytes[pos + 1]]);
        let len = u16::from_le_bytes([bytes[pos + 2], bytes[pos + 3]]);
        out.push(Frame { channel, len });
        // A zero-length frame would make this walk spin forever on the same
        // offset; the +4 is the frame header itself, so progress is
        // guaranteed even then, and the caller asserts len != 0.
        pos += 4 + len as usize;
    }
    assert_eq!(pos, bytes.len(), "frame walk did not land exactly on EOF");
    out
}

/// Assert that no frame carries a zero length field.
fn assert_no_zero_length_frame(bytes: &[u8]) -> Vec<Frame> {
    let fr = frames(bytes);
    for (i, f) in fr.iter().enumerate() {
        assert_ne!(
            f.len, 0,
            "frame #{i} (channel {}) has a zero length field",
            f.channel
        );
    }
    fr
}

/// Read `bytes` back and assert the reader saw no zero-length block.
fn read_back_clean(bytes: &[u8]) -> DataManager {
    let mgr = DataManager::load_from_reader(Cursor::new(bytes.to_vec()))
        .expect("written file loads");
    assert_eq!(
        mgr.stats.blocks_skipped_zero_length, 0,
        "reader reported zero-length block skips in writer output"
    );
    assert_eq!(
        mgr.stats.blocks_truncated, 0,
        "reader saw a truncated block in writer output"
    );
    mgr
}

fn dbl(name: &str) -> ChannelDef {
    ChannelDef {
        name: name.into(),
        data_type: DataType::Double,
        ..Default::default()
    }
}

fn write_to_vec(b: WriterBuilder) -> Vec<u8> {
    let mut out = Vec::new();
    b.write_to(&mut out).expect("write to buffer");
    out
}

/// Anti-vacuity guard for the whole file: point the same two assertions at the
/// hand-assembled corpus file that *does* carry a zero-length frame
/// (`examples/generated/malformed/osf5_zero_length_block.osf`) and prove they
/// fire. Without this, a frame walker that silently found nothing — or a
/// counter the reader never increments — would make every test above pass for
/// the wrong reason.
#[test]
fn the_detectors_fire_on_the_known_malformed_corpus_file() {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .nth(3)
        .expect("crate is rooted at .../implementations/rust/osf-core")
        .join("examples")
        .join("generated")
        .join("malformed")
        .join("osf5_zero_length_block.osf");
    let bytes = std::fs::read(&path).expect("corpus file is present");

    let fr = frames(&bytes);
    assert_eq!(
        fr.iter().filter(|f| f.len == 0).count(),
        1,
        "frame walker did not find the known zero-length frame"
    );

    let mgr = DataManager::load_from_reader(Cursor::new(bytes)).expect("corpus file loads");
    assert_eq!(
        mgr.stats.blocks_skipped_zero_length, 1,
        "reader counter did not fire on the known zero-length frame"
    );
}

/// Risk shape 3: a channel declared in the metablock but never given samples
/// must produce no block at all — not an empty one.
#[test]
fn declared_but_unwritten_channel_emits_no_block() {
    let mut b = WriterBuilder::new().creator("test:up3-audit");
    let silent = b.add_channel(dbl("Sensor/Silent")).unwrap();
    let loud = b.add_channel(dbl("Sensor/Loud")).unwrap();
    b.add_timestamped_samples_f64(loud, &[1_000, 2_000], &[1.0, 2.0])
        .unwrap();

    let bytes = write_to_vec(b);
    let fr = assert_no_zero_length_frame(&bytes);
    assert!(
        fr.iter().all(|f| f.channel != silent),
        "the silent channel got a block: {fr:?}"
    );

    let mgr = read_back_clean(&bytes);
    assert_eq!(mgr.channels().len(), 2);
}

/// Risk shape 1, degenerate end: an *empty* equidistant segment is accepted by
/// `add_equidistant_segment_f64` (there is no length guard at
/// `writer.rs:1356-1408`) and `write_equidistant_segment` writes the
/// `bcStartData` opener unconditionally (`writer.rs:571-582`). The resulting
/// block carries zero samples, but it still carries its control byte, the
/// timestamp, the rate and the `u32` count — 21 bytes, never 0.
#[test]
fn empty_equidistant_segment_emits_a_nonempty_block() {
    let mut b = WriterBuilder::new().creator("test:up3-audit");
    let i = b.add_channel(dbl("Sensor/Eq")).unwrap();
    b.add_equidistant_segment_f64(i, 1_000_000_000, 1000.0, &[])
        .unwrap();
    b.add_equidistant_segment_f64(i, 2_000_000_000, 1000.0, &[1.0, 2.0, 3.0])
        .unwrap();

    let bytes = write_to_vec(b);
    let fr = assert_no_zero_length_frame(&bytes);
    // The zero-sample opener is the multi-sample form: control + i64 ts +
    // f64 rate + u32 N = 21 bytes.
    assert_eq!(fr[0], Frame { channel: i, len: 21 });

    let mgr = read_back_clean(&bytes);
    let osf_core::Channel::Equidistant(eq) = mgr.channel("Sensor/Eq").unwrap() else {
        panic!("expected an equidistant channel");
    };
    assert_eq!(eq.as_doubles_flat().unwrap(), vec![1.0, 2.0, 3.0]);
}

/// Same shape reached through `WriterBuilder::from_manager`: unlike the C++
/// (`blockwriter.cpp:763`) and Java (`BlockWriter.java:529`) copy paths,
/// `copy_equidistant_segment` (`writer.rs:1048`) does not skip a zero-sample
/// segment, so a round trip reproduces the opener. Still never zero-length.
#[test]
fn round_tripping_an_empty_segment_stays_nonempty() {
    let mut b = WriterBuilder::new().creator("test:up3-audit");
    let i = b.add_channel(dbl("Sensor/Eq")).unwrap();
    b.add_equidistant_segment_f64(i, 1_000_000_000, 1000.0, &[])
        .unwrap();
    b.add_equidistant_segment_f64(i, 2_000_000_000, 1000.0, &[4.0, 5.0])
        .unwrap();
    let first = write_to_vec(b);
    let mgr = read_back_clean(&first);

    let second = write_to_vec(WriterBuilder::from_manager(&mgr).expect("from_manager"));
    assert_no_zero_length_frame(&second);
    read_back_clean(&second);
}

/// Risk shape 2: an empty `string` / `binary` sample. OSF5 appends no trailing
/// `0x00`, so this is the version where an empty payload could plausibly reach
/// length 0 — it does not: `variable_payload_size` (`writer.rs:778`) is
/// `1 + 8 + n`, i.e. 9 bytes for `n == 0`.
#[test]
fn empty_string_and_binary_samples_emit_nine_byte_blocks() {
    let mut b = WriterBuilder::new().creator("test:up3-audit");
    let s = b
        .add_channel(ChannelDef {
            name: "Sensor/Str".into(),
            data_type: DataType::String,
            ..Default::default()
        })
        .unwrap();
    let bin = b
        .add_channel(ChannelDef {
            name: "Sensor/Bin".into(),
            data_type: DataType::Binary,
            ..Default::default()
        })
        .unwrap();
    b.add_string_samples(s, &[1_000, 2_000], &[String::new(), "x".to_string()])
        .unwrap();
    b.add_binary_samples(bin, &[1_000, 2_000], &[Vec::new(), vec![0xAA]])
        .unwrap();

    let bytes = write_to_vec(b);
    let fr = assert_no_zero_length_frame(&bytes);
    assert_eq!(fr[0], Frame { channel: s, len: 9 });
    assert_eq!(fr[2], Frame { channel: bin, len: 9 });

    let mgr = read_back_clean(&bytes);
    let osf_core::Channel::Variable(var) = mgr.channel("Sensor/Str").unwrap() else {
        panic!("expected a variable channel");
    };
    assert_eq!(var.as_strings().unwrap(), &["".to_string(), "x".to_string()]);
    let osf_core::Channel::Variable(var) = mgr.channel("Sensor/Bin").unwrap() else {
        panic!("expected a variable channel");
    };
    assert_eq!(var.as_binaries().unwrap(), &[Vec::new(), vec![0xAA_u8]]);
}

/// Risk shape 1, the boundary that actually matters: sample counts chosen so
/// the payload divides *exactly* by the chunk size. With
/// `sizeoflengthvalue = 2` and `f64` samples the equidistant opener holds
/// `(65535 - 21) / 8 = 8189` samples and each continuation `(65535 - 5) / 8 =
/// 8191`, so `8189 + 2 * 8191` fills three blocks to the brim and must leave
/// no trailing empty fourth.
#[test]
fn equidistant_exact_chunk_multiple_emits_no_trailing_empty_block() {
    const MAX_START: usize = (65535 - 21) / 8;
    const MAX_CONT: usize = (65535 - 5) / 8;
    let total = MAX_START + 2 * MAX_CONT;

    let mut b = WriterBuilder::new().creator("test:up3-audit");
    let i = b.add_channel(dbl("Sensor/Eq")).unwrap();
    let samples: Vec<f64> = (0..total).map(|x| x as f64).collect();
    b.add_equidistant_segment_f64(i, 1_000_000_000, 1000.0, &samples)
        .unwrap();

    let bytes = write_to_vec(b);
    let fr = assert_no_zero_length_frame(&bytes);
    assert_eq!(fr.len(), 3, "expected exactly three full blocks: {fr:?}");

    let mgr = read_back_clean(&bytes);
    let osf_core::Channel::Equidistant(eq) = mgr.channel("Sensor/Eq").unwrap() else {
        panic!("expected an equidistant channel");
    };
    assert_eq!(eq.as_doubles_flat().unwrap(), samples);
}

/// Same boundary for the timestamped path: per sample `8 + 8 = 16` bytes and
/// overhead 5, so a block holds `(65535 - 5) / 16 = 4095` samples. Two exact
/// blockfuls must produce two blocks and no empty third.
#[test]
fn timestamped_exact_chunk_multiple_emits_no_trailing_empty_block() {
    const MAX_PER: usize = (65535 - 5) / 16;
    let total = 2 * MAX_PER;

    let mut b = WriterBuilder::new().creator("test:up3-audit");
    let i = b.add_channel(dbl("Sensor/Ts")).unwrap();
    let timestamps: Vec<i64> = (0..total as i64).map(|x| 1_000 + x).collect();
    let values: Vec<f64> = (0..total).map(|x| x as f64 * 0.25).collect();
    b.add_timestamped_samples_f64(i, &timestamps, &values)
        .unwrap();

    let bytes = write_to_vec(b);
    let fr = assert_no_zero_length_frame(&bytes);
    assert_eq!(fr.len(), 2, "expected exactly two full blocks: {fr:?}");

    let mgr = read_back_clean(&bytes);
    let osf_core::Channel::Timestamped(ts) = mgr.channel("Sensor/Ts").unwrap() else {
        panic!("expected a timestamped channel");
    };
    assert_eq!(ts.timestamps_ns(), timestamps);
}
