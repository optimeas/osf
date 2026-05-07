# OSF — Rust Implementation

![Status](https://img.shields.io/badge/status-in%20progress-orange.svg)

## Target Platform

Systems programming, high-performance server applications, and embedded
targets via `no_std` (later). Safe, zero-cost abstractions over OSF data.

This crate is also the foundation for the Python bindings published under
`implementations/python/` — see [DECISIONS.md §18](../../DECISIONS.md#18-rust-as-foundation-for-python).
One codebase, two audiences.

## Status

**In progress.** Read path is complete (raw `BlockReader` plus typed
`DataManager`); write path emits OSF5 in block mode. The crate now
round-trips every shipped reference file. OSFZ decompression and
PyO3 bindings remain.

| Capability                                                          | State   |
|---------------------------------------------------------------------|---------|
| Magic-header detection (OSF4 / OSF5)                                | ✅      |
| Legacy identifiers `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`| ✅      |
| OSF4 XML metablock parser                                           | ✅      |
| OSF5 JSON metablock parser                                          | ✅      |
| Removed-datatype detection (spec rev 2026-05-04)                    | ✅      |
| Deprecated-field tolerance (real field files parse)                 | ✅      |
| Block reader (all data types)                                       | ✅      |
| ReaderStats with per-channel detail                                 | ✅      |
| Best-effort truncation handling                                     | ✅      |
| Skipped-payload capture (opt-in)                                    | ✅      |
| In-memory data manager with typed channels                          | ✅      |
| Channel access by name and by index (DECISIONS §10)                 | ✅      |
| Equidistant segments (multi-`bcStartData`)                          | ✅      |
| Block writer (OSF5)                                                 | ✅      |
| Roundtrip validation                                                | ✅      |
| OSFZ transparent decompression (gzip + zlib)                        | ✅      |
| PyO3 bindings (`implementations/python/`)                           | Pending |

## Layout

```text
implementations/rust/
├── Cargo.toml          — workspace root
└── osf-core/
    ├── Cargo.toml
    ├── src/
    │   ├── lib.rs           — re-exports + read_file convenience
    │   ├── error.rs         — OsfError (thiserror)
    │   ├── header.rs        — parse_magic_header + unit tests
    │   ├── types.rs         — DataType, ChannelType, BlockContent
    │   ├── meta.rs          — MetaBlock model + datatype/channel-type validation
    │   ├── meta_json.rs     — OSF5 JSON metablock parser
    │   ├── meta_xml.rs      — OSF4 XML metablock parser
    │   ├── block.rs         — Block, BlockKind, payload enums, control-byte decoder
    │   ├── reader.rs        — BlockReader<R: Read> iterator + payload parsers
    │   ├── stats.rs         — ReaderStats, ChannelStats, Display impls
    │   ├── data_channel.rs  — typed Channel enum, Segment, samples_with_time
    │   ├── manager.rs       — DataManager + private build_channels logic
    │   ├── compression.rs   — transparent OSFZ detection (gzip + zlib)
    │   ├── binary_write.rs  — little-endian write helpers (private)
    │   └── writer.rs        — WriterBuilder + write_to_file convenience
    ├── examples/
    │   ├── inspect.rs       — header + metadata + channel list (no block reading)
    │   ├── stats.rs         — full read + ReaderStats + top-N raw channels
    │   ├── dump.rs          — manager-driven per-channel summary + first-channel detail
    │   └── copy.rs          — load + write_to_file + verify reload (writer demo)
    └── tests/
        ├── header_test.rs    — every shipped .osf parses its magic header
        ├── metablock_test.rs — every shipped .osf parses its metablock
        ├── block_test.rs     — every shipped .osf streams blocks cleanly
        ├── manager_test.rs   — every shipped .osf assembles into a DataManager
        ├── roundtrip_test.rs — every shipped .osf survives load + write + reload
        └── osfz_test.rs      — weather_station.osfz field sample + synthetic OSFZ
```

## Build

From `implementations/rust/`:

```bash
cargo build
cargo test
cargo clippy
```

Six integration suites walk `../../examples/` and
`../../examples/generated/`:

- `header_test.rs` — every shipped `.osf` parses its magic header.
- `metablock_test.rs` — every shipped `.osf` parses its metablock.
- `block_test.rs` — `BlockReader` streams every shipped `.osf`.
- `manager_test.rs` — `DataManager::load_from_file` succeeds on every
  shipped `.osf`.
- `roundtrip_test.rs` — load + write + reload, with bitwise sample
  comparison, on every shipped `.osf` (including OSF4-source →
  OSF5-target conversion).
- `osfz_test.rs` — gzip-OSFZ field sample (`weather_station.osfz`)
  loads cleanly with `stats.compressed = true`; synthetic gzip and
  zlib re-wraps of `steam_loco.osf` produce identical channel sets
  to the plain source.

`manager_test.rs` and `roundtrip_test.rs` each have a `#[ignore]`-gated
performance smoke. Run them manually:

```bash
cargo test --release -- --ignored
```

Local measurement: `steam_loco.osf` reads in ~3 ms in release; full
write of the same data also ~3 ms. Both well under the brief budgets
(100 ms read, 100 ms write).

## Inspect a file

```bash
cargo run --example inspect -- ../../examples/steam_loco.osf
cargo run --example inspect -- ../../examples/generated/osf5_mixed.osf
```

`inspect` is fast (header + metablock only). For a full read with
counters use `stats`; for a typed-channel summary use `dump`; to
copy a file via the writer use `copy`.

## Manager API

```rust
use osf_core::DataManager;

let mgr = DataManager::load_from_file("examples/steam_loco.osf")?;
println!("Channels: {}", mgr.channels().len());

// Channel access by name (mandatory per DECISIONS §10)
let temp = mgr.channel("Sensor.Temperature").expect("not found");

// Iterate over samples — segment timestamps are reconstructed lazily
for sample in temp.samples_with_time() {
    println!("{} ns: {:?}", sample.timestamp_ns, sample.value);
}

// Equidistant segments are first-class — every bcStartData opens one
if let Channel::Equidistant(eq) = temp {
    let values: Vec<f64> = eq.as_doubles_flat()?;
    for segment in eq.segments() {
        println!(
            "seg start={}, samples={}, rate={}",
            segment.start_timestamp_ns,
            segment.sample_count,
            segment.sample_rate_hz,
        );
    }
}
```

`DataManager::load_from_file` is the convenience entry. For streaming
sources, `load_from_reader(impl Read)` does the same starting from a
caller-supplied reader.

The lower-level `BlockReader` iterator is still available for callers
that want raw blocks; the manager sits on top of it.

## Writer API

Two tiers, symmetric to the read side:

### Convenience: round-trip a DataManager

```rust
use osf_core::{DataManager, writer};

let mgr = DataManager::load_from_file("input.osf")?;
writer::write_to_file(&mgr, "output.osf")?;
```

Always emits OSF5 (DECISIONS §6) — even when the source was OSF4.

### Builder: programmatic construction

```rust
use osf_core::writer::{WriterBuilder, ChannelDef};
use osf_core::types::{ChannelType, DataType};

let mut builder = WriterBuilder::new()
    .creator("my-app:1.0")
    .tag("preview")
    .reason("BOOT");

let temp_idx = builder.add_channel(ChannelDef {
    name: "Sensor.Temperature".into(),
    data_type: DataType::Double,
    channel_type: ChannelType::Scalar,
    physical_unit: Some("°C".into()),
    ..Default::default()
})?;

// Equidistant segment — multiple calls accumulate as separate segments.
builder.add_equidistant_segment_f64(
    temp_idx,
    1_574_200_200_000_000_000,
    1.0,
    &samples_f64,
)?;

// Or timestamped:
builder.add_timestamped_samples_f64(idx, &timestamps_ns, &values)?;

// Or strings (one block per sample):
builder.add_string_samples(idx, &timestamps_ns, &strings)?;

builder.write_to_file("output.osf")?;
```

### Constraints

- **OSF5 only** — DECISIONS §6.
- **Block mode only** — DECISIONS §7. Streaming write is reserved
  for embedded language targets.
- **No OSFZ** — writer never produces compressed output.
  DECISIONS §12.
- **No trailer / no magic trailer** — OSF5 dropped both.
- **`bcStartData` numeric only** — equidistant blocks support `float`
  and `double` only per spec rev 2026-05-04. Add equidistant data of
  other numeric types as `bcAbsTimeStampData` instead.
- **Block splitting** is automatic for numeric channels: a 100k-sample
  run with `sizeoflengthvalue=2` is silently split into multiple blocks
  the reader merges back into one segment.
- **Auto-bump** of `sizeoflengthvalue` for variable channels: a single
  string / binary sample > 65 521 bytes triggers a debug-logged bump
  from 2 → 4 so the file remains writable.

## Spec revision tracked

OSF specification revision **2026-05-04** ([English](../../docs/en/osf_general.md),
[Deutsch](../../docs/de/osf_general.md)).

The metablock parsers reject the four removed datatypes (`pair`,
`triple`, `candata`, `gpsdata`) explicitly with an error that names the
replacement, rather than silently mapping them to a current type. The
eight removed channel-level fields (`scale`, `offset`, `physicalunit1`
through `3`, `physicaldimension1` through `3`) are accepted on read with
a `log::warn!` because real field files (`examples/steam_loco.osf`,
`examples/motorbike.osf`) still carry them on every channel — failing
on them would make the parser unusable on existing data.

The writer never emits any removed datatype or deprecated field;
`add_channel` rejects `DataType::Unsupported` and `DataType::ByteArray`
(read-side alias only) up front, and the metablock JSON output uses
the canonical spec spellings.

The manager layer adds spec-level consistency checks per channel:
mixing `bcStartData` and `bcAbsTimeStampData` blocks on one channel
fails with `OsfError::ChannelMixedBlockTypes`; orphan
`bcContinuedData` (no preceding `bcStartData`) fails with
`OsfError::ContinuedDataWithoutStart`; `bcContinuedRelStampData`
without a prior absolute timestamp fails with
`OsfError::RelStampWithoutAnchor`. The writer applies the same rules
from the producer side.

Forward-compat `Unsupported` channels are silently dropped from the
manager's channel list so applications can iterate without filtering.

## Dependencies

| Crate                   | Purpose                                             |
|-------------------------|-----------------------------------------------------|
| `thiserror`             | Ergonomic error enum (`OsfError`)                   |
| `serde_json`            | OSF5 metablock parser + writer                      |
| `quick-xml`             | OSF4 metablock parser                               |
| `byteorder`             | Little-endian binary reader and writer              |
| `log`                   | Standard logging facade                             |
| `serde`                 | Derive support for upcoming structures              |
| `flate2` (rust_backend) | Transparent OSFZ decompression (gzip + zlib)        |
| `env_logger` (dev)      | Test-time + example-time logger backend             |

## OSFZ — compressed OSF files

The reader detects OSFZ (compressed OSF) transparently: if the first
two bytes match gzip (`0x1F 0x8B`) or zlib (`0x78 0x01 / 0x5E / 0x9C
/ 0xDA`), the stream is wrapped in the matching `flate2` decoder
before the magic-header parser sees it. Both
`DataManager::load_from_file` / `load_from_reader` and the lib-level
`read_file` convenience pick up the detection automatically. The
writer never produces OSFZ output (DECISIONS §12).

`ReaderStats` exposes `compressed: bool` and `compression_format:
CompressionFormat` (`None` / `Zlib` / `Gzip`) so callers can render
the compression status. The `inspect` and `stats` examples already
do.

```bash
cargo run --example inspect -- ../../examples/weather_station.osfz
# path:           ../../examples/weather_station.osfz
# compressed:     yes (gzip)
# version:        Osf4
# ...
```

The current Optimeas device output (`weather_station.osfz`) uses
gzip; older tooling may use zlib. Both are valid OSFZ — see
[DECISIONS §12](../../DECISIONS.md#12-osfz-compression) and the
specification documents in [docs/en](../../docs/en/osf_general.md) /
[docs/de](../../docs/de/osf_general.md) for details.

## Next steps

1. **Session 7** — PyO3 wrapper crate at `implementations/python/`,
   exposing the reader, manager, and writer to Python with NumPy
   interop on flat numeric channels.

## License

Apache 2.0. © 2026 Optimeas GmbH.
