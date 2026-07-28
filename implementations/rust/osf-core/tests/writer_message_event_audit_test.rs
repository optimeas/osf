// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! OSF-UP4 writer negative proof: the Rust writer never emits control byte 4
//! (`bcMessageEvent`), in either its plain (`0x04`) or multi-sample (`0x84`)
//! form.
//!
//! `bcMessageEvent` became **read**-mandatory on this branch (DECISIONS §26).
//! Read obligation is not write permission — §26 says writers must never emit
//! it. Most of that guarantee is static and is cleared by reading the code
//! (`writer.rs:424-427` defines exactly three control constants: `0x05`, `0x06`
//! and `0x08`), so it is documented in `examples/README.md` rather than tested
//! here. What is *not* static, and is new since this branch started, is the
//! round trip: before OSF-UP4 a `bcMessageEvent` block was skipped and silently
//! dropped by a load-and-rewrite; now it decodes, so its content reaches
//! [`WriterBuilder::from_manager`] and gets re-emitted in *some* encoding. This
//! suite pins which one.
//!
//! Two independent assertions per case:
//!
//! 1. **Byte level.** Walk the raw block stream and read the control byte of
//!    every frame directly. This does not depend on the reader agreeing with
//!    the writer.
//! 2. **Reader level.** Read the output back and assert the sample content
//!    survives, so output that is "clean" only because the writer dropped the
//!    channel cannot pass.
//!
//! The suite opens with an anti-vacuity guard: the same detector is pointed at
//! `examples/generated/osf4_message_event_string.osf`, which carries exactly
//! five control-byte-4 frames, and is required to find all five.

use osf_core::writer::{ChannelDef, WriterBuilder};
use osf_core::{Channel, DataManager, DataType, GpsLocation, parse_magic_header};
use std::io::Cursor;
use std::path::{Path, PathBuf};

/// The block type this whole suite is about (DECISIONS §26).
const CONTROL_MESSAGE_EVENT: u8 = 0x04;
/// Bit 7 — the multi-sample flag, masked off before comparing a block type.
const MULTI_SAMPLE_FLAG: u8 = 0x80;

/// Channel index of `Demo.Message` in the committed corpus pair.
const MESSAGE_CHANNEL_INDEX: u16 = 1;

/// One frame of the block stream, with its control byte.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Frame {
    channel: u16,
    len: u32,
    control: u8,
}

impl Frame {
    /// Block type with the multi-sample bit masked off.
    fn block_type(self) -> u8 {
        self.control & !MULTI_SAMPLE_FLAG
    }
}

/// Walk the block stream of `bytes` and return every frame with its control
/// byte.
///
/// Unlike the OSF-UP3 walker this one is **width-aware per channel**: the
/// corpus pair declares `Demo.Counter` with `sizeoflengthvalue = 2` and
/// `Demo.Message` with `4`, so a walker that assumed a single width would
/// misparse the very file the anti-vacuity guard depends on. The widths come
/// from the parsed metablock, and an undeclared channel index is a hard failure
/// rather than a guess.
fn frames(bytes: &[u8]) -> Vec<Frame> {
    let mut cur = Cursor::new(bytes);
    let hdr = parse_magic_header(&mut cur).expect("valid magic header");
    let meta_start = cur.position() as usize;
    let meta_end = meta_start + hdr.metablock_len as usize;
    let meta = osf_core::parse_metablock(hdr.version, &bytes[meta_start..meta_end])
        .expect("metablock parses");

    let width_of = |channel: u16| -> usize {
        let ch = meta
            .channels
            .iter()
            .find(|c| c.index == channel)
            .unwrap_or_else(|| panic!("block stream refers to undeclared channel {channel}"));
        assert!(
            ch.size_of_length_value == 2 || ch.size_of_length_value == 4,
            "channel {channel} declares sizeoflengthvalue {}",
            ch.size_of_length_value
        );
        ch.size_of_length_value as usize
    };

    let mut out = Vec::new();
    let mut pos = meta_end;
    while pos + 2 < bytes.len() {
        let channel = u16::from_le_bytes([bytes[pos], bytes[pos + 1]]);
        let width = width_of(channel);
        assert!(
            pos + 2 + width <= bytes.len(),
            "truncated length field at offset {pos}"
        );
        let len = match width {
            2 => u32::from(u16::from_le_bytes([bytes[pos + 2], bytes[pos + 3]])),
            _ => u32::from_le_bytes([
                bytes[pos + 2],
                bytes[pos + 3],
                bytes[pos + 4],
                bytes[pos + 5],
            ]),
        };
        assert!(
            len >= 1,
            "zero-length frame on channel {channel} at offset {pos} - this \
             walker reads the control byte and cannot classify such a frame"
        );
        let control = bytes[pos + 2 + width];
        out.push(Frame {
            channel,
            len,
            control,
        });
        pos += 2 + width + len as usize;
    }
    assert_eq!(pos, bytes.len(), "frame walk did not land exactly on EOF");
    out
}

/// Every frame whose block type is `bcMessageEvent`, multi-sample bit ignored.
fn message_event_frames(bytes: &[u8]) -> Vec<Frame> {
    frames(bytes)
        .into_iter()
        .filter(|f| f.block_type() == CONTROL_MESSAGE_EVENT)
        .collect()
}

fn examples_generated() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .nth(3)
        .expect("crate is rooted at .../implementations/rust/osf-core")
        .join("examples")
        .join("generated")
}

fn write_to_vec(b: WriterBuilder) -> Vec<u8> {
    let mut out = Vec::new();
    b.write_to(&mut out).expect("write to buffer");
    out
}

/// The five `Demo.Message` samples of the corpus pair, as `(timestamp, text)`.
fn message_samples(mgr: &DataManager) -> Vec<(i64, String)> {
    let Some(Channel::Variable(vc)) = mgr.channel("Demo.Message") else {
        panic!("Demo.Message missing or not a variable channel");
    };
    let texts = vc.as_strings().expect("Demo.Message is a string channel");
    vc.timestamps_ns()
        .iter()
        .copied()
        .zip(texts.iter().cloned())
        .collect()
}

// -------------------------------------------------------------------
// Anti-vacuity guard.
// -------------------------------------------------------------------

/// Point the detector at the corpus file that *does* carry control byte 4 and
/// require it to find all five frames. Without this, a walker that silently
/// found nothing would make every assertion below pass for the wrong reason.
///
/// The equivalent file is checked in the same test as a negative control: it
/// holds the same five samples encoded as `bcAbsTimeStampData`, so a detector
/// that merely returned "everything" would fail here.
#[test]
fn the_detector_fires_on_the_known_message_event_corpus_file() {
    let legacy = std::fs::read(examples_generated().join("osf4_message_event_string.osf"))
        .expect("corpus file is present");
    let found = message_event_frames(&legacy);
    assert_eq!(
        found.len(),
        5,
        "detector did not find the five known control-byte-4 frames: {found:?}"
    );
    for f in &found {
        assert_eq!(f.channel, MESSAGE_CHANNEL_INDEX, "on Demo.Message");
        assert_eq!(f.control, CONTROL_MESSAGE_EVENT, "bit 7 clear in the corpus");
    }

    let equivalent =
        std::fs::read(examples_generated().join("osf4_message_event_string_equivalent.osf"))
            .expect("corpus file is present");
    assert!(
        message_event_frames(&equivalent).is_empty(),
        "the equivalent file encodes the same samples as bcAbsTimeStampData, \
         so the detector must find nothing in it"
    );
}

// -------------------------------------------------------------------
// The round trip — the one path where read support could leak into
// write output.
// -------------------------------------------------------------------

/// Load the `bcMessageEvent` corpus file, write it back out through
/// [`WriterBuilder::from_manager`], and pin what the output carries.
///
/// Before OSF-UP4 the block was skipped, so this round trip lost the channel
/// silently. Now it decodes into the existing time-stamped representation
/// (§26), and the copy path re-emits it through `add_string_samples`, i.e. as
/// `bcAbsTimeStampData` — `writer.rs:783` writes `CONTROL_ABS_TIMESTAMP`
/// unconditionally on the variable-length path. Control byte 4 does not
/// survive the round trip, and neither does the loss.
#[test]
fn round_tripping_the_message_event_corpus_emits_no_control_byte_four() {
    let source = examples_generated().join("osf4_message_event_string.osf");
    let mgr = DataManager::load_from_file(&source).expect("corpus file loads");
    let before = message_samples(&mgr);
    assert_eq!(before.len(), 5, "corpus must decode five samples to re-emit");

    let out = write_to_vec(WriterBuilder::from_manager(&mgr).expect("from_manager"));

    // Byte level: no control byte 4 anywhere in the output, and the
    // Demo.Message frames specifically are bcAbsTimeStampData.
    let fr = frames(&out);
    assert!(
        message_event_frames(&out).is_empty(),
        "round-trip output carries control byte 4: {:?}",
        message_event_frames(&out)
    );
    let msg_index = mgr
        .channels()
        .iter()
        .position(|c| c.name() == "Demo.Message")
        .expect("Demo.Message present") as u16;
    let msg_frames: Vec<Frame> = fr.iter().copied().filter(|f| f.channel == msg_index).collect();
    assert_eq!(
        msg_frames.len(),
        5,
        "expected one block per Demo.Message sample: {msg_frames:?}"
    );
    for f in &msg_frames {
        assert_eq!(
            f.control, 0x08,
            "Demo.Message must be re-emitted as bcAbsTimeStampData, bit 7 clear"
        );
    }

    // Reader level: the content survived. An output with no control byte 4
    // because the channel was dropped would fail here.
    let round_tripped =
        DataManager::load_from_reader(Cursor::new(out)).expect("round-trip output loads");
    assert_eq!(
        message_samples(&round_tripped),
        before,
        "Demo.Message content changed across the round trip"
    );
    assert_eq!(
        round_tripped.stats.blocks_skipped_deprecated_type, 0,
        "round-trip output must contain no deprecated block type"
    );
    assert_eq!(
        round_tripped.stats.blocks_skipped_reserved_type, 0,
        "round-trip output must contain no reserved/unspecified block shape"
    );
}

/// The same round trip for `datatype=binary`. §26 admits `binary` over
/// `bcMessageEvent` too, and the corpus pair only covers `string`, so the
/// binary half of the copy path (`copy_channel_data` → `add_binary_samples`)
/// would otherwise be untested. Built here rather than read from the corpus:
/// what matters is the writer's output, not the input encoding.
#[test]
fn round_tripping_a_binary_variable_channel_emits_no_control_byte_four() {
    let mut b = WriterBuilder::new().creator("test:up4-audit");
    let bin = b
        .add_channel(ChannelDef {
            name: "Demo.Blob".into(),
            data_type: DataType::Binary,
            ..Default::default()
        })
        .unwrap();
    b.add_binary_samples(
        bin,
        &[1_000, 2_000, 3_000],
        &[Vec::new(), vec![0x04], vec![0x84, 0x04, 0xFF]],
    )
    .unwrap();
    let first = write_to_vec(b);

    let mgr = DataManager::load_from_reader(Cursor::new(first)).expect("first pass loads");
    let out = write_to_vec(WriterBuilder::from_manager(&mgr).expect("from_manager"));

    assert!(
        message_event_frames(&out).is_empty(),
        "binary round-trip output carries control byte 4"
    );
    let round_tripped =
        DataManager::load_from_reader(Cursor::new(out)).expect("round-trip output loads");
    let Some(Channel::Variable(vc)) = round_tripped.channel("Demo.Blob") else {
        panic!("Demo.Blob missing or not a variable channel");
    };
    assert_eq!(
        vc.as_binaries().unwrap(),
        &[Vec::new(), vec![0x04_u8], vec![0x84_u8, 0x04, 0xFF]],
        "binary payload bytes that happen to equal 0x04 / 0x84 must survive \
         unchanged - they are data, not control bytes"
    );
}

// -------------------------------------------------------------------
// Every writer entry point at once.
// -------------------------------------------------------------------

/// Exercise every block-producing entry point of the builder in one file and
/// assert the emitted control bytes come from the closed set
/// `{bcContinuedData, bcStartData, bcAbsTimeStampData}`.
///
/// This is the executable counterpart to the static reading: `writer.rs`
/// defines exactly three control constants and never names `ControlKind`
/// (the reader-side enum that *does* have a `MessageEvent` variant), so no
/// builder input can select block type 4. The test covers the equidistant,
/// timestamped-numeric, GPS, string and binary paths — including the GPS
/// encoder, which the OSF-UP3 audit left with no executable coverage at all.
#[test]
fn no_writer_entry_point_emits_a_block_type_outside_the_expected_set() {
    let mut b = WriterBuilder::new().creator("test:up4-audit");

    let eq64 = b
        .add_channel(ChannelDef {
            name: "Demo.Eq64".into(),
            data_type: DataType::Double,
            time_increment_ns: Some(1_000_000),
            ..Default::default()
        })
        .unwrap();
    let eq32 = b
        .add_channel(ChannelDef {
            name: "Demo.Eq32".into(),
            data_type: DataType::Float,
            time_increment_ns: Some(1_000_000),
            ..Default::default()
        })
        .unwrap();
    let ts = b
        .add_channel(ChannelDef {
            name: "Demo.Ts".into(),
            data_type: DataType::Double,
            ..Default::default()
        })
        .unwrap();
    let u32ch = b
        .add_channel(ChannelDef {
            name: "Demo.Counter".into(),
            data_type: DataType::UInt32,
            ..Default::default()
        })
        .unwrap();
    let gps = b
        .add_channel(ChannelDef {
            name: "Demo.Gps".into(),
            data_type: DataType::GpsLocation,
            ..Default::default()
        })
        .unwrap();
    let s = b
        .add_channel(ChannelDef {
            name: "Demo.Message".into(),
            data_type: DataType::String,
            ..Default::default()
        })
        .unwrap();
    let bin = b
        .add_channel(ChannelDef {
            name: "Demo.Blob".into(),
            data_type: DataType::Binary,
            ..Default::default()
        })
        .unwrap();

    b.add_equidistant_segment_f64(eq64, 1_000_000_000, 1000.0, &[1.0, 2.0, 3.0])
        .unwrap();
    // Second segment: forces the bcContinuedData / bcStartData pair.
    b.add_equidistant_segment_f64(eq64, 2_000_000_000, 1000.0, &[4.0])
        .unwrap();
    b.add_equidistant_segment_f32(eq32, 1_000_000_000, 500.0, &[1.5, 2.5])
        .unwrap();
    b.add_timestamped_samples_f64(ts, &[10, 20], &[0.5, 1.5])
        .unwrap();
    // Single sample: the bit-7-clear form, a different code path from the run.
    b.add_timestamped_samples_u32(u32ch, &[30], &[7]).unwrap();
    b.add_timestamped_gps_samples(
        gps,
        &[40, 50],
        &[
            GpsLocation {
                latitude: 47.55,
                longitude: 7.94,
                altitude: 290.0,
            },
            GpsLocation {
                latitude: 47.56,
                longitude: 7.95,
                altitude: 291.0,
            },
        ],
    )
    .unwrap();
    b.add_string_samples(s, &[60, 70], &["hello".to_string(), String::new()])
        .unwrap();
    b.add_binary_samples(bin, &[80], &[vec![0xAA, 0xBB]]).unwrap();

    let out = write_to_vec(b);
    let fr = frames(&out);
    assert!(!fr.is_empty(), "expected blocks in the output");
    for f in &fr {
        assert!(
            matches!(f.block_type(), 0x05 | 0x06 | 0x08),
            "frame on channel {} carries block type {:#04x} (control {:#04x}) - \
             the writer defines only bcContinuedData (5), bcStartData (6) and \
             bcAbsTimeStampData (8)",
            f.channel,
            f.block_type(),
            f.control
        );
    }
    assert!(
        message_event_frames(&out).is_empty(),
        "writer output carries control byte 4"
    );

    // Round trip the same file: the copy path must not introduce one either.
    let mgr = DataManager::load_from_reader(Cursor::new(out)).expect("output loads");
    let second = write_to_vec(WriterBuilder::from_manager(&mgr).expect("from_manager"));
    for f in frames(&second) {
        assert!(
            matches!(f.block_type(), 0x05 | 0x06 | 0x08),
            "round-trip frame on channel {} carries block type {:#04x}",
            f.channel,
            f.block_type()
        );
    }
}
