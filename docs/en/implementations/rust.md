---
title: Rust implementation
description: The osf-core library in Rust — read, write and transparent OSFZ; also the foundation of the Python binding
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Rust
  - osf-core
  - Cargo
  - no_std
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

🇩🇪 [German version](../../de/implementations/rust.md)

# Rust implementation

The **`osf-core`** crate is a safe, performant Rust implementation of the
Open Streaming Format. It targets systems programming, high-performance
server applications and — later, via `no_std` — embedded targets.

`osf-core` is also the **foundation of the Python binding** `osfdata`: Rust
does the actual work, Python is only a thin PyO3 wrapper on top (see
[DECISIONS §18](https://github.com/optimeas/osf/blob/main/DECISIONS.md)).
One codebase, two audiences.

## Capabilities

The read path is complete (raw `BlockReader` plus typed `DataManager`), the
write path produces OSF5 in block mode, and the crate runs a full round-trip
for every bundled reference file (load → write → reload, compared
bit-for-bit).

| Capability | Status |
|---|---|
| Magic header (OSF4/OSF5) incl. legacy identifiers | ✅ |
| OSF4 XML and OSF5 JSON metablock parsers | ✅ |
| Block reader (all data types) + `ReaderStats` | ✅ |
| Best-effort on truncated files | ✅ |
| Typed `DataManager`, access by name and index | ✅ |
| Equidistant segments (multiple `bcStartData`) | ✅ |
| Block writer (OSF5) + round-trip validation | ✅ |
| Transparent OSFZ decompression (gzip + zlib) | ✅ |
| PyO3 bindings (`osfdata`) | ✅ |

## Building

From `implementations/rust/`:

```bash
cargo build
cargo test
cargo clippy
```

Six integration suites run over `examples/` and `examples/generated/` and
check every bundled `.osf` file from the magic header to the round-trip. An
`#[ignore]`-gated performance probe can be run with
`cargo test --release -- --ignored`; `steam_loco.osf` reads and writes
locally in ~3 ms each.

`osf-core` is part of the repository workspace; it has not been published to
crates.io yet. Until then the crate is consumed via a path or git
dependency.

## Inspecting a file

```bash
cargo run --example inspect -- ../../examples/steam_loco.osf
cargo run --example inspect -- ../../examples/weather_station.osfz
```

`inspect` is fast (header + metablock only). For a full read with counters
there is `stats`, for a typed channel summary `dump`, and `copy`
demonstrates the writer (load → write → verify).

## Manager API

```rust
use osf_core::DataManager;

let mgr = DataManager::load_from_file("examples/steam_loco.osf")?;
println!("Channels: {}", mgr.channels().len());

// Access by name (mandatory, DECISIONS §10)
let temp = mgr.channel("Sensor.Temperature").expect("not found");

// Iterate over the samples — segment timestamps are reconstructed lazily
for sample in temp.samples_with_time() {
    println!("{} ns: {:?}", sample.timestamp_ns, sample.value);
}
```

`load_from_file` is the convenient entry point; `load_from_reader(impl Read)`
does the same starting from any reader. The lower-level `BlockReader`
iterator stays available to callers that want to process raw blocks.

## Writer API

Two tiers, symmetric to the read side:

```rust
use osf_core::{DataManager, writer};

// Convenience: write a DataManager back as OSF5 (round-trip)
let mgr = DataManager::load_from_file("input.osf")?;
writer::write_to_file(&mgr, "output.osf")?;
```

```rust
use osf_core::writer::{WriterBuilder, ChannelDef};
use osf_core::types::{ChannelType, DataType};

// Builder: programmatic construction
let mut builder = WriterBuilder::new().creator("my-app:1.0").tag("preview");
let idx = builder.add_channel(ChannelDef {
    name: "Sensor.Temperature".into(),
    data_type: DataType::Double,
    channel_type: ChannelType::Scalar,
    ..Default::default()
})?;
builder.add_timestamped_samples_f64(idx, &timestamps_ns, &values)?;
builder.write_to_file("output.osf")?;
```

### Constraints

- **OSF5 only** (DECISIONS §6) and **block mode only** (DECISIONS §7);
  streaming writes are reserved for embedded language targets.
- **No OSFZ output** (DECISIONS §12) — OSFZ is read transparently.
- Equidistant blocks `float`/`double` only (spec rev 2026-05-04); other
  numeric types as `bcAbsTimeStampData`.
- Block splitting and bumping `sizeoflengthvalue` (2 → 4 for large variable
  samples) happen automatically.

## Transparent OSFZ

The reader detects compressed OSF files by their first two bytes
(gzip `0x1F 0x8B`, zlib `0x78 …`) and inflates them via `flate2`
(pure Rust, no system zlib) before the magic-header parser sees them.
`ReaderStats` exposes `compressed` and `compression_format`
(`None`/`Zlib`/`Gzip`).

## Source code and further reading

- Source code on GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  directory `implementations/rust/osf-core/`
- Python binding on top: [Python integration](../integrations/python.md)
- Format specification: chapter [OSF format](../osf_general.md)

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
