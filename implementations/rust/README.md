# OSF — Rust Implementation

![Status](https://img.shields.io/badge/status-in%20progress-orange.svg)

## Target Platform

Systems programming, high-performance server applications, and embedded
targets via `no_std` (later). Safe, zero-cost abstractions over OSF data.

This crate is also the foundation for the Python bindings published under
`implementations/python/` — see [DECISIONS.md §18](../../DECISIONS.md#18-rust-as-foundation-for-python).
One codebase, two audiences.

## Status

**In progress.** Magic header detection and metablock parsing for both
OSF4 and OSF5 are in place; block-stream reading and writing follow in
subsequent sessions.

| Capability                                                          | State   |
|---------------------------------------------------------------------|---------|
| Magic-header detection (OSF4 / OSF5)                                | ✅      |
| Legacy identifiers `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`| ✅      |
| OSF4 XML metablock parser                                           | ✅      |
| OSF5 JSON metablock parser                                          | ✅      |
| Removed-datatype detection (spec rev 2026-05-04)                    | ✅      |
| Deprecated-field tolerance (real field files parse)                 | ✅      |
| Block reader (all data types)                                       | Pending |
| Block writer (OSF5)                                                 | Pending |
| PyO3 bindings (`implementations/python/`)                           | Pending |

## Layout

```text
implementations/rust/
├── Cargo.toml          — workspace root
└── osf-core/
    ├── Cargo.toml
    ├── src/
    │   ├── lib.rs       — re-exports + parse_metablock dispatcher
    │   ├── error.rs     — OsfError (thiserror)
    │   ├── header.rs    — parse_magic_header + unit tests
    │   ├── types.rs     — DataType, ChannelType, BlockContent
    │   ├── meta.rs      — MetaBlock model + datatype/channel-type validation
    │   ├── meta_json.rs — OSF5 JSON metablock parser
    │   └── meta_xml.rs  — OSF4 XML metablock parser
    ├── examples/
    │   └── inspect.rs   — CLI: print header + metadata + channel list
    └── tests/
        ├── header_test.rs    — every shipped .osf parses its magic header
        └── metablock_test.rs — every shipped .osf parses its metablock
```

## Build

From `implementations/rust/`:

```bash
cargo build
cargo test
cargo clippy
```

The integration tests walk `../../examples/` and `../../examples/generated/`
and assert that every shipped `.osf` file parses both its magic header and
its metablock. Updating the reference set (e.g. by re-running
`OSFGenerator`) is therefore exercised automatically.

## Inspect a file

```bash
cargo run --example inspect -- ../../examples/steam_loco.osf
cargo run --example inspect -- ../../examples/generated/osf5_mixed.osf
```

Output for an OSF4 field file (with deprecated-field warnings silenced
via `RUST_LOG=error`):

```text
path:           ../../examples/steam_loco.osf
version:        Osf4
metablock_len:  26279 bytes
created_utc:    2023-08-15T21:14:40Z
creator:        optimeas
channels:       123
   [ 52] GPS.PosFixMode      scalar  double  unit=-    incr_ns=0
   [ 39] P_3_Schieberkasten  scalar  double  unit=bar  incr_ns=0
   [ 22] T_2_Heissdampf      scalar  double  unit=°C   incr_ns=0
   ...
infos:          0
```

Run with `RUST_LOG=debug` to see the full diagnostic output: read-side
alias usage (`bytearray` → `binary`), accepted alternative spellings
(short GPS attributes), unknown fields, and channel-by-channel
deprecated-attribute warnings.

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

Forward-compatibility variants `DataType::Unsupported(String)` and
`ChannelType::Unsupported(String)` carry the on-disk spelling so a file
that uses a future-spec datatype still parses channel-by-channel; the
upcoming block reader will reject the channel explicitly when an
`Unsupported` variant is accessed.

## Dependencies

| Crate                | Purpose                                                |
|----------------------|--------------------------------------------------------|
| `thiserror`          | Ergonomic error enum (`OsfError`)                      |
| `serde_json`         | OSF5 metablock parser                                  |
| `quick-xml`          | OSF4 metablock parser                                  |
| `log`                | Standard logging facade (parser diagnostics)           |
| `serde`              | Derive support for upcoming structures                 |
| `byteorder`          | Little-endian binary block reader (Session 3)          |
| `env_logger` (dev)   | Test-time logger backend                               |

## Next steps

1. **Session 3** — block stream reader: control byte, `bcStartData`,
   `bcAbsTimeStampData`, `bcEquidistantData`; little-endian primitive
   readers via `byteorder`. Will exercise the `Unsupported` channel
   types as hard errors at access time.
2. **Session 4** — typed in-memory channels (mirror of
   `OSF.Data.Channels` from the Delphi reference), including the
   per-channel `Segments` list for equidistant data.
3. **Session 5** — block writer for OSF5.
4. **Session 6** — PyO3 wrapper crate at `implementations/python/`,
   exposing the reader and writer to Python with NumPy interop.

## License

Apache 2.0. © 2026 Optimeas GmbH.
