# OSF — Rust Implementation

![Status](https://img.shields.io/badge/status-in%20progress-orange.svg)

## Target Platform

Systems programming, high-performance server applications, and embedded
targets via `no_std` (later). Safe, zero-cost abstractions over OSF data.

This crate is also the foundation for the Python bindings published under
`implementations/python/` — see [DECISIONS.md §18](../../DECISIONS.md#18-rust-as-foundation-for-python).
One codebase, two audiences.

## Status

**In progress.** Magic header detection, metablock parsing for both OSF4
and OSF5, and the full block-stream reader are in place. Writing follows
in subsequent sessions.

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
| OSFZ (zlib) transparent decompression                               | Pending |
| Typed in-memory channels (segments + sample arrays)                 | Pending |
| Block writer (OSF5)                                                 | Pending |
| PyO3 bindings (`implementations/python/`)                           | Pending |

## Layout

```text
implementations/rust/
├── Cargo.toml          — workspace root
└── osf-core/
    ├── Cargo.toml
    ├── src/
    │   ├── lib.rs       — re-exports + read_file convenience
    │   ├── error.rs     — OsfError (thiserror)
    │   ├── header.rs    — parse_magic_header + unit tests
    │   ├── types.rs     — DataType, ChannelType, BlockContent
    │   ├── meta.rs      — MetaBlock model + datatype/channel-type validation
    │   ├── meta_json.rs — OSF5 JSON metablock parser
    │   ├── meta_xml.rs  — OSF4 XML metablock parser
    │   ├── block.rs     — Block, BlockKind, payload enums, control-byte decoder
    │   ├── reader.rs    — BlockReader<R: Read> iterator + payload parsers
    │   └── stats.rs     — ReaderStats, ChannelStats, Display impls
    ├── examples/
    │   ├── inspect.rs   — CLI: print header + metadata + channel list
    │   └── stats.rs     — CLI: full read + ReaderStats + top-N channels
    └── tests/
        ├── header_test.rs    — every shipped .osf parses its magic header
        ├── metablock_test.rs — every shipped .osf parses its metablock
        └── block_test.rs     — every shipped .osf streams blocks cleanly
```

## Build

From `implementations/rust/`:

```bash
cargo build
cargo test
cargo clippy
```

Three integration suites walk `../../examples/` and
`../../examples/generated/`:

- `header_test.rs` asserts that every shipped `.osf` parses its magic
  header.
- `metablock_test.rs` asserts that every shipped `.osf` parses its
  metablock and contains channels with consistent fields.
- `block_test.rs` drives the `BlockReader` over every file and checks
  that no file produces a reader error.

Updating the reference set (e.g. by re-running `OSFGenerator`) is
therefore exercised end-to-end.

## Inspect a file

```bash
cargo run --example inspect -- ../../examples/steam_loco.osf
cargo run --example inspect -- ../../examples/generated/osf5_mixed.osf
```

`inspect` is fast (header + metablock only). For a full read with
counters, use the `stats` example.

## Stats

```bash
cargo run --example stats -- ../../examples/steam_loco.osf
cargo run --example stats -- ../../examples/motorbike.osf 20
```

Pass an optional second argument to choose `top_n`. Output for an OSF4
field file with `RUST_LOG=error` to silence per-channel deprecation
warnings:

```text
File:            ../../examples/steam_loco.osf
File size:       2.53 MB
Header:          27 B
Metablock:       25.66 KB
Data section:    2.50 MB
Read in:         15 ms

Channels total:        123
With data:             123
Unsupported:           0

Blocks total:          123
Read:                  123
Skipped (unsupp.):     0
Skipped (deprec.):     0
Skipped (reserved):    0
Truncated:             0

Top 10 channels by sample count:
   index  name                                         samples      bytes  segments  time range (ns)
   -----  ----------------------------------------  ----------  ---------  --------  --------------------
      32  R_9                                            19507  304.80 KB         0  1692093763655739001..1692098578509456266
      ...
```

## Reader API

```rust
use osf_core::{read_file, BlockReader, parse_magic_header, parse_metablock};
use std::fs::File;
use std::io::{BufReader, Read};

// 1. Convenience: collect everything in memory and get the stats.
let (meta, blocks, stats) = read_file(path)?;
println!("{stats}");

// 2. Iterator-driven: stream blocks one at a time.
let mut reader = BufReader::new(File::open(path)?);
let header = parse_magic_header(&mut reader)?;
let mut body = vec![0u8; header.metablock_len as usize];
reader.read_exact(&mut body)?;
let meta = parse_metablock(header.version, &body)?;

let mut block_reader = BlockReader::new(reader, &meta)
    .with_capture_skipped_payload(false)  // default; flip to true to keep skip bytes
    .with_file_size(file_size);           // optional, threaded through to stats

for block in &mut block_reader {
    let block = block?;
    // …
}
let stats = block_reader.stats();
```

`BlockReader` is the iterator API; `read_file` is a convenience for
callers that want the whole file in memory plus stats.

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

The block reader skips deprecated control bytes (`bcTrustedTimestamp`,
`bcStatusEvent`, `bcMessageEvent`) and reserved values, surfacing them
through `BlockKind::Skipped` so applications still see the block. The
`motorbike.osf` field sample reaches the reader with 169 such skipped
blocks alongside 13 898 typed blocks; the reader processes the file end
to end in ~30 ms.

Forward-compatibility variants `DataType::Unsupported(String)` and
`ChannelType::Unsupported(String)` carry the on-disk spelling so a file
that uses a future-spec datatype still parses channel-by-channel; the
block reader emits `Skipped { reason: UnsupportedDataType }` for blocks
of such channels and re-aligns to the next one.

## Dependencies

| Crate                | Purpose                                                |
|----------------------|--------------------------------------------------------|
| `thiserror`          | Ergonomic error enum (`OsfError`)                      |
| `serde_json`         | OSF5 metablock parser                                  |
| `quick-xml`          | OSF4 metablock parser                                  |
| `byteorder`          | Little-endian binary block reader                      |
| `log`                | Standard logging facade (parser diagnostics)           |
| `serde`              | Derive support for upcoming structures                 |
| `env_logger` (dev)   | Test-time + example-time logger backend                |

## Next steps

1. **Session 4** — OSFZ transparent decompression (zlib wrapper around
   any `R: Read` that detects an OSFZ-compressed `.osf` and delegates
   to the existing reader). Small isolated change.
2. **Session 5** — typed in-memory channels (mirror of
   `OSF.Data.Channels` from the Delphi reference), including the
   per-channel `Segments` list for equidistant data and the aggregated
   sample arrays per channel.
3. **Session 6** — block writer for OSF5.
4. **Session 7** — PyO3 wrapper crate at `implementations/python/`,
   exposing the reader and writer to Python with NumPy interop.

## License

Apache 2.0. © 2026 Optimeas GmbH.
