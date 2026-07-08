// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! Generate the OSF5 integrity-profile (level `crc`) reference files into
//! `examples/generated/integrity/`:
//!
//! - `osf5_crc_equidistant.osf` — three equidistant `double` channels.
//! - `osf5_crc_variable.osf`    — one `string` and one `binary` channel.
//!
//! Both are written through the Rust writer with `with_integrity(Crc32c)`, so
//! every block carries a frame CRC32C and the header carries the metablock
//! `crc32c` token. Other implementations read these to cross-validate their
//! CRC readers. Run with `cargo run --example gen_crc_refs`.
//!
//! They live in a dedicated `integrity/` subdirectory (not directly in
//! `examples/generated/`) so that integrity-unaware low-level test harnesses,
//! which glob `examples/generated/*.osf` non-recursively, do not read them
//! without honouring the frame CRC (a CRC-unaware reader would fold the CRC
//! bytes into a string/binary value — exactly the fail-open case the profile
//! prevents).
//!
//! Note: the writer stamps `created_utc` at write time, so re-running updates
//! only that timestamp; the channel data is deterministic.

use osf_core::writer::{ChannelDef, WriterBuilder};
use osf_core::{DataType, IntegrityProfile};
use std::path::PathBuf;

fn generated_dir() -> PathBuf {
    let mut p = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    // .../implementations/rust/osf-core -> repo root
    p.pop();
    p.pop();
    p.pop();
    p.join("examples").join("generated").join("integrity")
}

fn double_channel(name: &str) -> ChannelDef {
    ChannelDef {
        name: name.into(),
        data_type: DataType::Double,
        ..Default::default()
    }
}

fn write_equidistant(dir: &std::path::Path) {
    let mut b = WriterBuilder::new().creator("osf-core:gen_crc_refs");
    let names = ["Sensor/Vibration100Hz", "Sensor/Vibration1kHz", "Sensor/Vibration10kHz"];
    let rates = [100.0_f64, 1000.0, 10_000.0];
    for (name, rate) in names.iter().zip(rates) {
        let idx = b.add_channel(double_channel(name)).unwrap();
        let samples: Vec<f64> = (0..100)
            .map(|i| (i as f64 * 0.1 * rate.log10()).sin())
            .collect();
        b.add_equidistant_segment_f64(idx, 1_000_000, rate, &samples)
            .unwrap();
    }
    let path = dir.join("osf5_crc_equidistant.osf");
    b.with_integrity(IntegrityProfile::Crc32c)
        .write_to_file(&path)
        .unwrap();
    println!("wrote {}", path.display());
}

fn write_variable(dir: &std::path::Path) {
    let mut b = WriterBuilder::new().creator("osf-core:gen_crc_refs");

    let s = b
        .add_channel(ChannelDef {
            name: "Sensor/Log".into(),
            data_type: DataType::String,
            ..Default::default()
        })
        .unwrap();
    let ts: Vec<i64> = (0..10).map(|i| 1_000_000 + i * 1_000_000).collect();
    let strings: Vec<String> = (0..10).map(|i| format!("event-{i:03}")).collect();
    b.add_string_samples(s, &ts, &strings).unwrap();

    let bin = b
        .add_channel(ChannelDef {
            name: "Sensor/Blob".into(),
            data_type: DataType::Binary,
            ..Default::default()
        })
        .unwrap();
    let binaries: Vec<Vec<u8>> = (0..10u8).map(|i| vec![i, i.wrapping_add(1), i.wrapping_add(2)]).collect();
    b.add_binary_samples(bin, &ts, &binaries).unwrap();

    let path = dir.join("osf5_crc_variable.osf");
    b.with_integrity(IntegrityProfile::Crc32c)
        .write_to_file(&path)
        .unwrap();
    println!("wrote {}", path.display());
}

fn main() {
    let dir = generated_dir();
    std::fs::create_dir_all(&dir).unwrap();
    write_equidistant(&dir);
    write_variable(&dir);
}
