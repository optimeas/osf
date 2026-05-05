# OSF — Project Status

Snapshot for resuming work in a new chat session. Pair with `DECISIONS.md`,
`CHANGELOG.md`, and `docs/en/osf_general.md` (or `docs/de/osf_general.md`)
when deeper context is needed.

| Field | Value |
|---|---|
| Repo | https://github.com/burkhard154/osf |
| Working dir | `V:\github\osf` (Windows) |
| Latest tag | **v0.2.0** (2026-05-05) |
| Branch | `main` |
| Spec revision in effect | **2026-05-04** |

---

## What OSF is

Open Streaming Format — a binary, block-oriented file format for time-series
measurement and process data. Designed for embedded streaming write and fast
block-wise read on servers/desktops/AI pipelines. Maintained by Optimeas GmbH;
implementations are Apache 2.0.

Two on-disk versions: **OSF4** (XML metablock, classic) and **OSF5** (JSON
metablock, simplified control byte, no trailer). Backward-compatible.

---

## Spec revision 2026-05-04 — what changed

**Removed datatypes and channel parameters** (readers reject them with a clear
"removed" error; do not silently fall back):
- `pair`, `triple`, `candata`
- `scale`, `offset`
- `physicalunit1..3`, `physicaldimension1..3`

**Renamed**:
- `gpsdata` → `gpslocation` (no backward-compat alias)
- `TOSFGpsLocation` field order: `latitude`, `longitude`, `altitude`

**Added**:
- Unsigned integers: `uint8`, `uint16`, `uint32`, `uint64`
- `bytearray` as read-side alias for `binary`; writer always emits `binary`
- `bcStartData` carries `double` SampleRate immediately after the `int64`
  start timestamp (OSF4 + OSF5)
- Multiple `bcStartData` blocks per channel are explicit. Each opens a new
  segment. Equidistant data types are limited to `float` and `double`.
- `string` and `binary` payloads in `bcAbsTimeStampData` end with a trailing
  `0x00` byte (uniform OSF4 + OSF5). Writer appends; reader strips. The byte
  is included in the per-value `uint32` length on multi-sample blocks.

Magic header legacy identifiers documented across all four spec docs:
`OSF4`, `OSF5`, **`OCEAN_STREAM_FORMAT4`** (still emitted by deployed
devices), `OCEAN_STREAMING_FORMAT4` (older).

---

## Repo layout

```
osf/
├── docs/
│   ├── README.md                    — language picker
│   ├── de/                          — German specification
│   │   ├── index.md
│   │   ├── osf_general.md
│   │   ├── examples/{index, osf_file_examples}.md
│   │   ├── references/{index, osf4, osf5, osf_vector_matrix}.md
│   │   └── media/                   — shared images
│   └── en/                          — English mirror, same structure
├── implementations/
│   ├── delphi/                      — reference implementation (full)
│   │   ├── src/                     — library units
│   │   ├── demos/osfviewer/         — viewer (uses TeeChart)
│   │   ├── demos/osfgenerator/      — writes the reference set
│   │   ├── demos/osfcsvexport/      — OSF → CSV export demo
│   │   └── OSFCompileCheck.dpr      — compile-only smoke test
│   ├── rust/                        — Cargo workspace; foundation for Python (DECISIONS §18)
│   │   └── osf-core/                — magic-header parser landed; metablock/blocks pending
│   └── (c, cpp, csharp, python, …)/ — README placeholders only
├── integrations/(arrow, pytorch, tensorflow, mcp, langchain)/  — placeholders
├── examples/
│   ├── motorbike.osf                — real field sample
│   ├── steam_loco.osf + .csv        — real field sample
│   └── generated/                   — 17 reference files (from OSFGenerator)
├── CHANGELOG.md, DECISIONS.md, CONTRIBUTING.md, README.md, LICENSE
└── STATUS.md                         — this file
```

---

## Delphi implementation — current API surface

**Library units in `implementations/delphi/src/`:**

| Unit | Public surface |
|---|---|
| `OSF.Types` | `TOSFDataType` (only current types — pair/triple/candata/gpsdata are gone), `TOSFVersion`, `TOSFGpsLocation`, `TBlockContent`, helpers (`OSFDataTypeFromString`, `OSFNowAsUnixNs`, …) |
| `OSF.Log` | `TOSFLogEvent`, `TOSFLogLevel`, `TOSFLoggable` |
| `OSF.Channel` | `TOSFChannelDef` — has `SampleRate: Double`; no longer has `Scale`, `Offset`, `PhysicalUnit1..3`, `PhysicalDimension1..3` |
| `OSF.Filer` | `TOSFFile` — streaming reader/writer for OSF4 and OSF5. `WriteEquidistantBlock(...)` requires `Channel.SampleRate > 0` and a non-zero `FirstTimestampNs` to start a new segment. `WriteTimestampedSample/Block/Doubles` for timestamped channels. Auto-appends/strips the `0x00` for `string`/`binary`. |
| `OSF.Data.Channels` | Typed in-memory channels. **`TOSFEquidistantDataChannel.Segments: TList<TOSFChannelSegment>`** maps the flat `Values` list onto absolute time — every `bcStartData` opens a new segment with `(StartTimestampNs, StartIndex, SampleCount)`. |
| `OSF.Data.Manager` | `TOSFDataManager.LoadFromFile/Stream` — high-level read; populates typed channels |
| `OSF.Export` | `TOSFExporter` abstract base (`ExcludeEmptyChannels`, `AbsoluteTimestamps`) |
| `OSF.Export.CSV` | `TOSFCSVExporter` — `(DecimalSeparator, ColumnSeparator, Encoding, TimestampFormat)` |

**`OSFCompileCheck.dpr`** at the implementation root is a no-form `uses`-only
program; running `dcc32 -B OSFCompileCheck.dpr` from `implementations/delphi/`
gives a clean compile signal after refactors.

---

## Demos

| Demo | Purpose |
|---|---|
| `demos/osfviewer/` | Loads an OSF file, lists channels, renders selected channel as a TeeChart. Uses TeeChart units — only builds inside the IDE (search path defined in the .dproj). |
| `demos/osfgenerator/` | Writes the 17-file reference set (8 OSF4 + 9 OSF5) into `examples/generated/`. Form has output-dir picker, OSF4/OSF5 toggles, samples-per-channel spinedit, log memo. |
| `demos/osfcsvexport/` | Pipeline demo: `TOSFFile` → `TOSFDataManager` → `TOSFCSVExporter`. Exposes the four exporter options + a debug toggle. |

All three projects compile clean with `dcc32` (Delphi 12 / RAD Studio 23.0;
`OSFViewer` only via IDE because of TeeChart).

---

## Rust implementation — current state

Started 2026-05-05 per [DECISIONS §18](DECISIONS.md#18-rust-as-foundation-for-python).
The `osf-core` crate is the foundation for both standalone Rust use and the
future Python bindings (PyO3 wrapper at `implementations/python/`).

**Crate at `implementations/rust/osf-core/`:**

| Module | Public surface |
|---|---|
| `error` | `OsfError` (thiserror): `Io`, `InvalidMagicHeader`, `UnsupportedVersion`, `MagicHeaderTooLong` |
| `header` | `OsfVersion { Osf4, Osf5 }`, `MagicHeader { version, metablock_len }`, `parse_magic_header<R: Read>` — accepts `OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4` |
| `types` | `DataType` (spec rev 2026-05-04 set; no `pair`/`triple`/`candata`/`gpsdata`), `ChannelType`, `BlockContent` — enum skeletons; methods land with the metablock parser |

Dependencies wired up but only `thiserror` is exercised so far: `serde`,
`serde_json`, `quick-xml`, `byteorder` are pre-staged for the next session.

**Tests:** 11 unit tests in `header.rs` plus 2 integration tests in
`tests/header_test.rs` that walk every `.osf` file under `examples/` and
`examples/generated/` and assert clean parsing. `cargo build`, `cargo test`,
and `cargo clippy --all-targets` all run clean.

**Inspect example:** `cargo run --example inspect -- <path>` prints the
detected version and metablock length.

**Next steps:** OSF5 JSON metablock parser (Session 2), OSF4 XML metablock
parser, block stream reader, typed channels, OSF5 writer, then PyO3 wrapper.

---

## Conventions in this repo

- **Push after every commit** — feedback memory; do not batch.
- **Commit messages**: imperative, lowercase scope where applicable
  (`feat(delphi): …`, `docs(en): …`, `style(delphi): …`, `fix(delphi): …`).
  Bodies wrap at ~72 chars and explain *why*.
- **Co-Authored-By trailer** on every commit:
  `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>`
- **No modification of OSF library units from a demo** unless explicitly
  requested. Demos depend on the units via `..\..\src\`.
- **Briefs and side-files**: external task briefs arrive as
  `~/Downloads/task-*.md`. Status / handoff files (this file included) live
  in the repo root.
- **dcc32 path** (Windows):
  `C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\dcc32.exe`

---

## Open / known follow-ups

- **DUnitX test suite** for the Delphi implementation — not started; only
  `OSFCompileCheck.dpr` exists today. Brief F3 from the spec-revision task
  was deferred; would be its own scaffolding effort.
- **Rust** — magic-header parser only; metablock parsers, block reader,
  typed channels, and writer are pending (see Rust section above).
- **Python bindings** — directory not yet started; will sit on `osf-core`
  via PyO3 once the Rust block reader/writer are in place.
- **Other language implementations** (C, C++, C#, …) — README
  placeholders only.
- **Integrations** (Arrow, PyTorch, TensorFlow, MCP, LangChain) — README
  placeholders only.
- **`docs/*/references/osf_vector_matrix.md`** — placeholder content
  (`weareworkingonit.png`); the full vector/matrix specification is pending.

---

## Resuming in a new Claude Code session

Drop this in as the first message:

> Lies `STATUS.md`, dann `DECISIONS.md`. Wir machen weiter.

Or, if you want to be absolutely minimal:

> `STATUS.md` lesen, dann weiter.

Claude Code finds the file in the working directory automatically. The
`auto-memory` system already holds the cross-session essentials (always-push
rule, dcc32 path, segments-model decision, Delphi-active workdir) — those
load on every session start; you don't need to repeat them.

For Claude.ai web chats (no filesystem access), paste the contents of this
file directly.
