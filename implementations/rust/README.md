# OSF — Rust Implementation

![Status](https://img.shields.io/badge/status-in%20progress-orange.svg)

## Target Platform

Systems programming, high-performance server applications, and embedded
targets via `no_std` (later). Safe, zero-cost abstractions over OSF data.

This crate is also the foundation for the Python bindings published under
`implementations/python/` — see [DECISIONS.md §18](../../DECISIONS.md#18-rust-as-foundation-for-python).
One codebase, two audiences.

## Status

**In progress.** The two reading layers are complete: the streaming
`BlockReader` for raw block-by-block access, and the higher-level
`DataManager` that aggregates blocks into typed channels with segment
metadata. Writing follows in subsequent sessions.

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
| OSFZ (zlib) transparent decompression                               | Pending |
| Block writer (OSF5)                                                 | Pending |
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
    │   └── manager.rs       — DataManager + private build_channels logic
    ├── examples/
    │   ├── inspect.rs       — header + metadata + channel list (no block reading)
    │   ├── stats.rs         — full read + ReaderStats + top-N raw channels
    │   └── dump.rs          — manager-driven per-channel summary + first-channel detail
    └── tests/
        ├── header_test.rs    — every shipped .osf parses its magic header
        ├── metablock_test.rs — every shipped .osf parses its metablock
        ├── block_test.rs     — every shipped .osf streams blocks cleanly
        └── manager_test.rs   — every shipped .osf assembles into a DataManager
```

## Build

From `implementations/rust/`:

```bash
cargo build
cargo test
cargo clippy
```

Four integration suites walk `../../examples/` and
`../../examples/generated/`:

- `header_test.rs` — every shipped `.osf` parses its magic header.
- `metablock_test.rs` — every shipped `.osf` parses its metablock.
- `block_test.rs` — `BlockReader` streams every shipped `.osf`.
- `manager_test.rs` — `DataManager::load_from_file` succeeds on every
  shipped `.osf` and channels are reachable both by name and by index.

`manager_test.rs` also has a `#[ignore]`-gated performance smoke
(`steam_loco_load_time_within_budget`) that asserts the brief's
budget (≤ 100 ms in release, ≤ 200 ms in debug). Run it manually:

```bash
cargo test --release -- --ignored
```

Local measurement: ~3 ms in release, well under budget.

## Inspect a file

```bash
cargo run --example inspect -- ../../examples/steam_loco.osf
cargo run --example inspect -- ../../examples/generated/osf5_mixed.osf
```

`inspect` is fast (header + metablock only). For a full read with
counters use `stats`; for a typed-channel summary use `dump`.

## Manager API

```rust
use osf_core::DataManager;

let mgr = DataManager::load_from_file("examples/steam_loco.osf")?;
println!("Channels: {}", mgr.channels().len());

// Channel access by name (mandatory per DECISIONS §10)
let temp = mgr.channel("Sensor/Temperature").expect("not found");

// Iterate over samples — segment timestamps are reconstructed lazily
for sample in temp.samples_with_time() {
    println!("{} ns: {:?}", sample.timestamp_ns, sample.value);
}

// Or dump everything as f64s in stream order (segments transparently
// joined, equidistant channels stitched)
if let Channel::Equidistant(eq) = temp {
    let values: Vec<f64> = eq.as_doubles_flat()?;
}

// Equidistant segments are first-class — every bcStartData opens one
if let Channel::Equidistant(eq) = temp {
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

### Sample iteration: `NumericValueRef` and `VariableValueRef`

`samples_with_time()` returns `Sample<NumericValueRef<'_>>` for
numeric / GPS channels and `Sample<VariableValueRef<'_>>` for
string / binary channels. `NumericValueRef` passes the eleven Copy
scalars by value and borrows only `GpsLocation` (24 bytes); the
lifetime parameter exists for that single variant. `VariableValueRef`
borrows in both variants because the values are heap-allocated.

```rust
match sample.value {
    NumericValueRef::Double(v)    => println!("{v}"),
    NumericValueRef::GpsLocation(g) => println!("{}, {}", g.latitude, g.longitude),
    other => println!("{other:?}"),
}
```

## Stats and dump

```bash
# Raw-block telemetry; faster, no channel aggregation.
cargo run --example stats -- ../../examples/steam_loco.osf

# Manager-driven summary; slower but with typed channels.
cargo run --example dump -- ../../examples/motorbike.osf
```

`dump` output for an OSF4 field file with `RUST_LOG=error`:

```text
File:            ../../examples/steam_loco.osf
Channels:        123 (123 with data, 0 unsupported)
Load time:       21 ms

Top 10 channels by sample count:
   index  name                                      type            samples  segments  unit
   -----  ----------------------------------------  -----------  ----------  --------  ----
      32  R_9                                       timestamped       19507         0  Ohm
      ...

First channel detail:
   name:           GPS.PosFixMode
   data type:      Double
   sample count:   109
   first 5 samples:
     0:  ts=1692093763318471742  value=3
     1:  ts=1692093779317336374  value=3
     ...
```

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

The manager layer adds spec-level consistency checks per channel:
mixing `bcStartData` and `bcAbsTimeStampData` blocks on one channel
fails with `OsfError::ChannelMixedBlockTypes`; orphan
`bcContinuedData` (no preceding `bcStartData`) fails with
`OsfError::ContinuedDataWithoutStart`; `bcContinuedRelStampData`
without a prior absolute timestamp fails with
`OsfError::RelStampWithoutAnchor`. Forward-compat `Unsupported`
channels are silently dropped from the manager's channel list so
applications can iterate without filtering.

## Dependencies

| Crate                | Purpose                                                |
|----------------------|--------------------------------------------------------|
| `thiserror`          | Ergonomic error enum (`OsfError`)                      |
| `serde_json`         | OSF5 metablock parser                                  |
| `quick-xml`          | OSF4 metablock parser                                  |
| `byteorder`          | Little-endian binary block reader                      |
| `log`                | Standard logging facade (parser + manager diagnostics) |
| `serde`              | Derive support for upcoming structures                 |
| `env_logger` (dev)   | Test-time + example-time logger backend                |

## Next steps

1. **Session 5** — block writer for OSF5 (block-mode only per
   DECISIONS §7). Will mirror the typed-channel structures from this
   session into a writer that produces `bcStartData` and
   `bcAbsTimeStampData` blocks. Embedded streaming-write is a separate,
   later language target.
2. **Session 6** — OSFZ transparent decompression (zlib wrapper). Small
   isolated change.
3. **Session 7** — PyO3 wrapper crate at `implementations/python/`,
   exposing the reader, manager, and writer to Python with NumPy
   interop on flat numeric channels.

## License

Apache 2.0. © 2026 Optimeas GmbH.
