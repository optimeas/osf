# OSF — Rust Implementation

![Status](https://img.shields.io/badge/status-in%20progress-orange.svg)

## Target Platform

Systems programming, high-performance server applications, and embedded
targets via `no_std` (later). Safe, zero-cost abstractions over OSF data.

This crate is also the foundation for the Python bindings published under
`implementations/python/` — see [DECISIONS.md §18](../../DECISIONS.md#18-rust-as-foundation-for-python).
One codebase, two audiences.

## Status

**In progress.** The crate skeleton and the magic-header parser are in
place; metablock parsing, block reading, and writing follow in subsequent
sessions.

| Capability                                                          | State   |
|---------------------------------------------------------------------|---------|
| Magic-header detection (OSF4 / OSF5)                                | ✅      |
| Legacy identifiers `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`| ✅      |
| OSF4 XML metablock parser                                           | Pending |
| OSF5 JSON metablock parser                                          | Pending |
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
    │   ├── lib.rs      — pub re-exports
    │   ├── error.rs    — OsfError (thiserror)
    │   ├── header.rs   — parse_magic_header + unit tests
    │   └── types.rs    — DataType, ChannelType, BlockContent
    ├── examples/
    │   └── inspect.rs  — CLI: print version + metablock length
    └── tests/
        └── header_test.rs  — runs against ../../../examples/
```

## Build

From `implementations/rust/`:

```bash
cargo build
cargo test
cargo clippy
```

The integration test in `osf-core/tests/header_test.rs` walks
`../../examples/` and `../../examples/generated/` and asserts that
`parse_magic_header` succeeds for every shipped `.osf` file. Updating the
reference set (e.g. by re-running `OSFGenerator`) is therefore exercised
automatically.

## Inspect a file

```bash
cargo run --example inspect -- ../../examples/steam_loco.osf
cargo run --example inspect -- ../../examples/generated/osf5_scalar_numeric.osf
```

Output for an OSF4 field file:

```text
path:           ../../examples/steam_loco.osf
version:        Osf4
metablock_len:  26279 bytes
```

## Dependencies

| Crate         | Purpose                                                       |
|---------------|---------------------------------------------------------------|
| `thiserror`   | Ergonomic error enum (`OsfError`)                             |
| `serde`       | Derive support for the upcoming OSF5 metablock structures     |
| `serde_json`  | OSF5 metablock parser (Session 2)                             |
| `quick-xml`   | OSF4 metablock parser (Session 2)                             |
| `byteorder`   | Little-endian binary block reader (Session 2)                 |

`serde_json` and `quick-xml` are wired in already so that the Session 2
work touches only library code, not `Cargo.toml`.

## Spec revision tracked

OSF specification revision **2026-05-04** ([English](../../docs/en/osf_general.md),
[Deutsch](../../docs/de/osf_general.md)). The `DataType` enum already
omits the removed types (`pair`, `triple`, `candata`, `gpsdata`); readers
will reject those legacy strings explicitly rather than silently mapping
them to a current type.

## Next steps

1. **Session 2** — OSF4 XML metablock parser (`quick-xml`) and OSF5 JSON
   metablock parser (`serde_json`); shared `MetaBlock` model.
2. **Session 3** — block stream reader: control byte, `bcStartData`,
   `bcAbsTimeStampData`, `bcEquidistantData`; little-endian primitive
   readers via `byteorder`.
3. **Session 4** — typed in-memory channels (mirror of
   `OSF.Data.Channels` from the Delphi reference).
4. **Session 5** — block writer for OSF5.
5. **Session 6** — PyO3 wrapper crate at `implementations/python/`,
   exposing the reader and writer to Python with NumPy interop.

## License

Apache 2.0. © 2026 Optimeas GmbH.
