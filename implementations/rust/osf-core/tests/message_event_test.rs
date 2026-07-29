// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! Integration tests for `bcMessageEvent` (control byte 4) decoding
//! (OSF-UP4, DECISIONS §26).
//!
//! Loads the committed conformance pair:
//! - `examples/generated/osf4_message_event_string.osf` — `Demo.Message`
//!   written as `bcMessageEvent` (the deployed-firmware encoding).
//! - `examples/generated/osf4_message_event_string_equivalent.osf` — the
//!   same channel content, but `Demo.Message` written as
//!   `bcAbsTimeStampData` instead.
//!
//! Both files carry `Demo.Counter` (`uint32`, 5 samples) and `Demo.Message`
//! (`string`, 5 samples) at the same five timestamps. The two files use
//! **different block ordering** (legacy is channel-major, equivalent is
//! round-robin) — deliberate, per the generator's doc comment. Tests here
//! must never assume matching block order between the two files; only
//! decoded content is compared.

use osf_core::{Channel, DataManager};
use std::path::{Path, PathBuf};

const BASE_TIMESTAMP_NS: i64 = 1_768_478_400_000_000_000;
const TIMESTAMP_STEP_NS: i64 = 5_000_000_000;

const LONG_MESSAGE: &str = concat!(
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
);

const MESSAGE_TEXTS: [&str; 5] = [
    "OSF-DEMO-0001",
    "no signal",
    "",
    "Grüße aus Säckingen ✓",
    LONG_MESSAGE,
];

const COUNTER_VALUES: [u32; 5] = [10, 20, 30, 40, 50];

fn examples_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .and_then(Path::parent)
        .expect("crate is rooted at .../implementations/rust/osf-core")
        .join("examples")
        .join("generated")
}

fn message_event_path() -> PathBuf {
    examples_root().join("osf4_message_event_string.osf")
}

fn equivalent_path() -> PathBuf {
    examples_root().join("osf4_message_event_string_equivalent.osf")
}

fn expected_timestamps() -> Vec<i64> {
    (0..5i64).map(|i| BASE_TIMESTAMP_NS + i * TIMESTAMP_STEP_NS).collect()
}

/// Test 1: the legacy `bcMessageEvent` file must decode `Demo.Message` to
/// all five samples in order, and the terminator guard must hold — a
/// reader that wrongly reused `bcAbsTimeStampData`'s null-terminator strip
/// would lose the last character/byte of every value, which four of the
/// five samples would still "look plausible" after (only the empty string
/// and the truncated long string would look obviously wrong), so we pin
/// exact last-character / exact-length assertions rather than mere
/// presence.
#[test]
fn message_event_channel_decodes_all_five_samples() {
    let mgr = DataManager::load_from_file(message_event_path())
        .expect("message-event file loads");

    let ch = mgr.channel("Demo.Message").expect("Demo.Message declared");
    let Channel::Variable(vc) = ch else {
        panic!("Demo.Message unexpectedly not a variable channel");
    };
    let texts = vc.as_strings().expect("Demo.Message is a string channel");
    let timestamps = vc.timestamps_ns();

    assert_eq!(timestamps.len(), 5, "expected 5 Demo.Message samples");
    assert_eq!(texts.len(), 5, "expected 5 Demo.Message samples");

    let decoded: Vec<(i64, &str)> = timestamps
        .iter()
        .copied()
        .zip(texts.iter().map(String::as_str))
        .collect();
    let expected: Vec<(i64, &str)> = expected_timestamps()
        .into_iter()
        .zip(MESSAGE_TEXTS.iter().copied())
        .collect();
    assert_eq!(decoded, expected, "Demo.Message samples do not match the expected sequence");

    // Terminator guard: strip_osf4_terminator must NOT have been applied to
    // this path. If it had, every sample here would be missing its last
    // byte/char.
    assert!(
        texts[0].ends_with('1'),
        "sample 0 must end with '1' (\"OSF-DEMO-0001\"); got {:?}",
        texts[0]
    );
    assert!(
        texts[3].ends_with('✓'),
        "sample 3 must end with '✓'; got {:?}",
        texts[3]
    );
    assert_eq!(texts[4].len(), 300, "sample 4 (long message) must keep all 300 bytes");
    assert_eq!(texts[2], "", "sample 2 must be the empty string, not truncated to nothing else");

    // Demo.Counter must still decode fully — sanity check that the fix did
    // not disturb the other channel.
    let counter_ch = mgr.channel("Demo.Counter").expect("Demo.Counter declared");
    let Channel::Timestamped(tc) = counter_ch else {
        panic!("Demo.Counter unexpectedly not a timestamped channel");
    };
    assert_eq!(tc.timestamps_ns().len(), 5, "expected 5 Demo.Counter samples");
}

/// Test 2: block-count invariants. This is the counter-bookkeeping guard —
/// `blocks_total` is a recomputed sum in `BlockReader::stats()`, and
/// omitting a new term from that sum silently went wrong twice before
/// (once per prior implementation piece of work touching it). These
/// assertions go in from the outset, not bolted on after the fact.
#[test]
fn message_event_counts_as_read_not_skipped() {
    let mgr = DataManager::load_from_file(message_event_path())
        .expect("message-event file loads");
    let stats = &mgr.stats;

    assert_eq!(stats.blocks_read, 10, "all 10 blocks (5 counter + 5 message) must be read");
    assert_eq!(
        stats.blocks_skipped_deprecated_type, 0,
        "bcMessageEvent blocks must no longer be counted as deprecated skips"
    );
    assert_eq!(stats.blocks_total, 10, "blocks_total must equal blocks_read here");
    assert_eq!(stats.blocks_truncated, 0);
}

/// Test 3: the two encodings must decode to the same channel-for-channel
/// content. The two files use different block orderings on disk
/// (channel-major vs. round-robin) — this test compares decoded content
/// only, never raw block sequence or byte layout.
#[test]
fn both_encodings_decode_identically() {
    let legacy = DataManager::load_from_file(message_event_path())
        .expect("message-event file loads");
    let equivalent = DataManager::load_from_file(equivalent_path())
        .expect("equivalent file loads");

    let legacy_names: Vec<&str> = legacy.channels().iter().map(channel_name).collect();
    let equivalent_names: Vec<&str> = equivalent.channels().iter().map(channel_name).collect();
    assert_eq!(legacy_names, equivalent_names, "same channel names in both files");

    // Demo.Counter: same sample count + timestamp sequence in both.
    let legacy_counter = as_timestamped(&legacy, "Demo.Counter");
    let equivalent_counter = as_timestamped(&equivalent, "Demo.Counter");
    assert_eq!(legacy_counter.timestamps_ns().len(), equivalent_counter.timestamps_ns().len());
    assert_eq!(legacy_counter.timestamps_ns(), equivalent_counter.timestamps_ns());
    let legacy_counter_values: Vec<u32> = legacy_counter
        .as_uint32_flat()
        .expect("Demo.Counter is uint32")
        .into_iter()
        .map(|(_, v)| v)
        .collect();
    let equivalent_counter_values: Vec<u32> = equivalent_counter
        .as_uint32_flat()
        .expect("Demo.Counter is uint32")
        .into_iter()
        .map(|(_, v)| v)
        .collect();
    assert_eq!(legacy_counter_values, equivalent_counter_values);
    assert_eq!(legacy_counter_values, COUNTER_VALUES.to_vec());

    // Demo.Message: same sample count, same timestamp sequence, same
    // decoded values.
    let legacy_message = as_variable(&legacy, "Demo.Message");
    let equivalent_message = as_variable(&equivalent, "Demo.Message");
    assert_eq!(legacy_message.timestamps_ns().len(), equivalent_message.timestamps_ns().len());
    assert_eq!(legacy_message.timestamps_ns(), equivalent_message.timestamps_ns());
    let legacy_texts = legacy_message.as_strings().expect("Demo.Message is string");
    let equivalent_texts = equivalent_message.as_strings().expect("Demo.Message is string");
    assert_eq!(
        legacy_texts, equivalent_texts,
        "Demo.Message must decode to identical values from both encodings"
    );
    assert_eq!(legacy_texts, MESSAGE_TEXTS.as_slice());
}

fn channel_name(ch: &Channel) -> &str {
    match ch {
        Channel::Equidistant(c) => &c.name,
        Channel::Timestamped(c) => &c.name,
        Channel::Variable(c) => &c.name,
    }
}

fn as_timestamped<'a>(
    mgr: &'a DataManager,
    name: &str,
) -> &'a osf_core::TimestampedChannel {
    match mgr.channel(name).unwrap_or_else(|| panic!("{name} declared")) {
        Channel::Timestamped(tc) => tc,
        other => panic!("{name} unexpectedly not timestamped: {other:?}"),
    }
}

fn as_variable<'a>(mgr: &'a DataManager, name: &str) -> &'a osf_core::VariableChannel {
    match mgr.channel(name).unwrap_or_else(|| panic!("{name} declared")) {
        Channel::Variable(vc) => vc,
        other => panic!("{name} unexpectedly not variable: {other:?}"),
    }
}
