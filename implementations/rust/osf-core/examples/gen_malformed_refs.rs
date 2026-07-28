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
//! writer outputs ("donor" files): everything of the first donor file, then
//! the four-byte bad frame, then the block section of the second donor file.
//! The bad frame deliberately sits *between* valid data rather than at the
//! front, so a reader test proves the scan **continues** rather than merely
//! that it starts.
//!
//! The splice is guarded, not just self-verified after the fact: before the
//! bad frame is written, the two donor files' magic headers and metablocks
//! are checked to agree (modulo `created_utc`, which the writer stamps
//! independently per file) and the channel's on-disk length-prefix width is
//! checked to be the 2 bytes the bad frame assumes. After the file is
//! written, it is read back and checked against the exact expected sample
//! sequence and the exact expected block-accounting stats — not just sample
//! *count* and skip *count*, which a wrong splice can satisfy by accident.
//!
//! Note: the writer stamps `created_utc` at write time, so re-running updates
//! only that timestamp; the channel data is deterministic.
//!
//! Run with `cargo run --example gen_malformed_refs`.

use osf_core::header::parse_magic_header;
use osf_core::writer::{ChannelDef, WriterBuilder};
use osf_core::{Channel, DataManager, DataType, MagicHeader, MetaBlock, parse_metablock};
use std::io::Cursor;
use std::path::{Path, PathBuf};

fn generated_dir() -> PathBuf {
    let mut p = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    // .../implementations/rust/osf-core -> repo root
    p.pop();
    p.pop();
    p.pop();
    p.join("examples").join("generated").join("malformed")
}

/// The one channel's sample formula, shared between the donor-file writer
/// and the corpus-file verifier so "what was written" and "what we expect to
/// read back" can never drift apart independently.
fn sample_at(i: i64) -> (i64, f64) {
    (1_000_000 + i * 1_000, i as f64 * 0.5)
}

/// Write a one-channel OSF5 "donor" file holding `count` timestamped `f64`
/// samples starting at sample number `first`, and return its bytes. Two
/// calls to this function are the raw material [`write_zero_length_block`]
/// splices together — it is never a file in its own right.
fn donor_file(first: i64, count: i64) -> Vec<u8> {
    let mut b = WriterBuilder::new().creator("osf-core:gen_malformed_refs");
    let idx = b
        .add_channel(ChannelDef {
            name: "Sensor/Double".into(),
            data_type: DataType::Double,
            ..Default::default()
        })
        .expect("add channel");
    let (timestamps, values): (Vec<i64>, Vec<f64>) =
        (first..first + count).map(sample_at).unzip();
    b.add_timestamped_samples_f64(idx, &timestamps, &values)
        .expect("add samples");
    let mut out = Vec::new();
    b.write_to(&mut out).expect("write to buffer");
    out
}

/// Parse a donor file's magic header and metablock, and return the byte
/// offset at which its block section starts (header line + metablock body).
fn parse_header_and_metablock(bytes: &[u8]) -> (MagicHeader, MetaBlock, usize) {
    let mut cur = Cursor::new(bytes);
    let hdr = parse_magic_header(&mut cur).expect("donor file has a valid magic header");
    let metablock_start = cur.position() as usize;
    let metablock_end = metablock_start + hdr.metablock_len as usize;
    let meta = parse_metablock(hdr.version, &bytes[metablock_start..metablock_end])
        .expect("donor file metablock parses");
    (hdr, meta, metablock_end)
}

fn write_zero_length_block(dir: &Path) {
    let donor_a = donor_file(0, 5);
    let donor_b = donor_file(5, 5);

    let (header_a, meta_a, _block_start_a) = parse_header_and_metablock(&donor_a);
    let (header_b, meta_b, block_start_b) = parse_header_and_metablock(&donor_b);

    // Guard: the two donor files must agree on everything except
    // `created_utc` (stamped independently per write). Without this, an
    // innocuous-looking future edit to `donor_file` or `sample_at` — a
    // renamed channel, a second channel, a different data type in one half
    // — silently produces a corpus file whose two halves were authored
    // against different channel definitions, while the sample-count and
    // skip-count self-checks below would still pass (proven: renaming the
    // second half's channel to `Other/Name` does not trip either of them).
    assert_eq!(
        header_a.version, header_b.version,
        "donor files disagree on OSF version"
    );
    assert_eq!(
        header_a.metablock_len, header_b.metablock_len,
        "donor files' metablocks differ in length - they must declare the \
         same single channel"
    );
    let mut meta_b_masked = meta_b.clone();
    meta_b_masked.file_info.created_utc = meta_a.file_info.created_utc.clone();
    assert_eq!(
        meta_a, meta_b_masked,
        "donor files' metablocks diverge beyond created_utc - donor_file() \
         must write an identical channel definition for both halves"
    );

    // Guard: the bad frame below is exactly 4 bytes - a channel index (u16)
    // followed by a length field (u16) - which is only correct when the
    // channel's on-disk length prefix is 2 bytes wide. Check rather than
    // assume in a comment, so a future change to
    // `ChannelDef::size_of_length_value` fails here with a direct message
    // instead of a confusing downstream reader error (verified: with
    // `size_of_length_value: 4` the reader instead reports "block
    // references unknown channel index 85" and the self-verify aborts,
    // which is correct but far less direct).
    let size_of_length_value = meta_a.channels[0].size_of_length_value;
    assert_eq!(
        size_of_length_value, 2,
        "the bad frame assumes sizeoflengthvalue = 2, but donor_file() now \
         writes {size_of_length_value} - update the frame construction below"
    );

    let mut out = donor_a;
    // The non-conforming frame itself: a channel index followed by a length
    // field of 0 and nothing else - no control byte. Per OSF-UP3 /
    // DECISIONS §25, a conforming block always carries at least its control
    // byte, so a length field of 0 is never valid; this is exactly the
    // anomaly the corpus file exists to exercise.
    let bad_frame_channel_index: u16 = 0;
    let bad_frame_length_field: u16 = 0;
    out.extend_from_slice(&bad_frame_channel_index.to_le_bytes());
    out.extend_from_slice(&bad_frame_length_field.to_le_bytes());
    out.extend_from_slice(&donor_b[block_start_b..]);

    let path = dir.join("osf5_zero_length_block.osf");
    std::fs::write(&path, &out).expect("write corpus file");

    // Self-verify: read the file back and check it against the exact
    // expected sample sequence and the exact expected block accounting -
    // not just a sample count and a skip count, which a wrong splice can
    // satisfy by accident (proven: splicing `donor_file(0, 5)` with itself
    // by mistake still yields "10 samples" - five timestamps repeated
    // twice - and "1 zero-length skip"; only comparing the decoded values
    // against the known-good sequence catches that).
    let mgr = DataManager::load_from_file(&path).expect("corpus file loads");

    let ch = mgr.channel("Sensor/Double").expect("channel present");
    let Channel::Timestamped(tc) = ch else {
        panic!("Sensor/Double unexpectedly not a timestamped channel");
    };
    let decoded = tc.as_doubles_flat().expect("channel holds double samples");
    let expected: Vec<(i64, f64)> = (0..10).map(sample_at).collect();
    assert_eq!(
        decoded, expected,
        "decoded samples do not match the expected splice"
    );

    let stats = &mgr.stats;
    assert_eq!(stats.blocks_total, 3, "expected 2 real blocks + 1 bad frame");
    assert_eq!(stats.blocks_read, 2, "expected exactly the two donor blocks");
    assert_eq!(
        stats.blocks_skipped_zero_length, 1,
        "expected exactly one zero-length skip"
    );
    assert_eq!(stats.blocks_skipped_unsupported, 0);
    assert_eq!(stats.blocks_skipped_deprecated_type, 0);
    assert_eq!(stats.blocks_skipped_reserved_type, 0);
    assert_eq!(stats.blocks_truncated, 0);
    assert_eq!(stats.blocks_crc_failed, 0);
    assert_eq!(stats.blocks_signature_skipped, 0);
    // Guards against a future donor-file change introducing the optional
    // 0xFFFF info-data trailer block: it would land inside the spliced
    // block stream (taking "everything of the first donor file" relies on
    // OSF5 emitting no trailer) and neither check above would notice it -
    // it is neither a sample nor a zero-length skip.
    assert!(
        !stats.trailer_seen,
        "donor file unexpectedly carries a trailer block"
    );

    println!("wrote {}", path.display());
}

fn main() {
    let dir = generated_dir();
    std::fs::create_dir_all(&dir).expect("create malformed dir");
    write_zero_length_block(&dir);
}
