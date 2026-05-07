# OSF — Project Status

Snapshot for resuming work in a new chat session. Pair with `DECISIONS.md`,
`CHANGELOG.md`, and `docs/en/osf_general.md` (or `docs/de/osf_general.md`)
when deeper context is needed.

| Field | Value |
|---|---|
| Repo | https://github.com/optimeas/osf |
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
│   │   └── osf-core/                — read + write + transparent OSFZ complete
│   ├── python/                      — PyO3 bindings (PyPI: osfdata, import: osf); 7a landed
│   └── (c, cpp, csharp, …)/         — README placeholders only
├── integrations/(arrow, pytorch, tensorflow, mcp, langchain)/  — placeholders
├── examples/
│   ├── motorbike.osf                — real field sample
│   ├── steam_loco.osf + .csv        — real field sample
│   ├── weather_station.osfz         — real gzip-OSFZ field sample
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
| `error` | `OsfError` (thiserror): `Io`, `InvalidMagicHeader`, `UnsupportedVersion`, `MagicHeaderTooLong`, `InvalidMetablock`, `RemovedInSpec2026_05_04`, `Json`, `Xml` |
| `header` | `OsfVersion { Osf4, Osf5 }`, `MagicHeader { version, metablock_len }`, `parse_magic_header<R: Read>` — accepts `OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4` |
| `types` | `DataType` (spec rev 2026-05-04 set + `Unsupported(String)` for forward-compat), `ChannelType { Scalar, Equidistant, Timestamped, Unsupported }`, `BlockContent` |
| `meta` | `MetaBlock { file_info, channels, infos }`, `FileInfo`, `Channel`, `Info`, `SpectrumType`; validation helpers `parse_data_type` / `parse_channel_type` shared by both parsers |
| `meta_json` | `parse_metablock_json(&[u8])` — OSF5 JSON parser via `serde_json::Value` (manual field picking; no derive, for forward-compat) |
| `meta_xml` | `parse_metablock_xml(&[u8])` — OSF4 XML parser via `quick-xml` event reader; tolerates CP1252 bytes in real field files via `String::from_utf8_lossy` |
| `block` | `Block { channel_index, kind }`, `BlockKind` (StartData / ContinuedData / AbsTimestampData / ContinuedRelStampData / Skipped), `NumericPayload` / `TimestampedPayload` / `RelTimestampedPayload`, `GpsLocation`, `SkipReason`, control-byte decoder |
| `reader` | `BlockReader<R: Read>` — Iterator over `Result<Block, OsfError>`. Builder `with_capture_skipped_payload(bool)` (default off, no allocation) and `with_file_size(u64)`. Best-effort on truncation, hard error on unknown channel index, silent consume of optional `0xFFFF` info block plus 40-byte magic trailer |
| `stats` | `ReaderStats` with file/section sizes, elapsed, channels and per-reason block counters, plus `per_channel: HashMap<u16, ChannelStats>` (segments, samples_total, time_range_ns); `Display` impls for both |
| `data_channel` | `Channel` enum (`Equidistant` / `Timestamped` / `Variable`), per-variant typed structs, `Segment`, `ChannelMeta`, `NumericValues`; `samples_with_time()` iterators yielding `Sample<NumericValueRef<'_>>` / `Sample<VariableValueRef<'_>>`; `as_doubles_flat` etc. helpers |
| `manager` | `DataManager` — `load_from_file(path)` / `load_from_reader(R)` build the typed channel list, expose `channel(name)` (mandatory per DECISIONS §10) and `channel_by_index(u16)` (optional). Internal `build_channels` runs the per-channel builder state machine (Pending → Equidistant or Timestamped on first typed block; Variable upfront for string/binary). |
| `binary_write` | Crate-private little-endian write helpers (symmetric to `byteorder::ReadBytesExt`); `write_string_with_terminator` / `write_binary_with_terminator` append the spec-mandated `0x00` |
| `writer` | `WriterBuilder` — accumulator with `add_channel`, 2 equidistant + 12 timestamped + 2 variable `add_*` methods. `write_to_file(path)` / `write_to(W)` emit OSF5; `from_manager(&DataManager)` builds a builder from a loaded manager. Module-level `writer::write_to_file(&DataManager, path)` is the round-trip convenience |
| `compression` | `MaybeCompressed<R>` enum (Plain / Zlib / Gzip) + `detect_and_wrap<R>(reader)`; transparent OSFZ detection by leading two bytes (gzip `0x1F 0x8B` or zlib `0x78 0x01/5E/9C/DA`). Pure-Rust via `flate2 + miniz_oxide` |
| `lib` | top-level `parse_metablock(version, &[u8])` dispatcher and `read_file(path) -> (MetaBlock, Vec<Block>, ReaderStats)` convenience |

**Spec rev 2026-05-04 enforcement:**

- Removed datatypes (`pair`, `triple`, `candata`, `gpsdata`) → hard
  `OsfError::RemovedInSpec2026_05_04` with the spec replacement spelled
  out in the error.
- Removed channel-level fields (`scale`, `offset`, `physicalunit1..3`,
  `physicaldimension1..3`) → `log::warn!` and skipped. Keeping these
  tolerant is non-negotiable: every channel in `examples/steam_loco.osf`
  carries `scale="1"` and `offset="0"`.
- `bytearray` → normalised to `DataType::Binary` with a `log::debug!`.
- `sizeoflengthvalue` other than 2 or 4 → hard `OsfError::InvalidMetablock`
  (silent corruption otherwise).
- Unknown fields → `log::debug!` and ignored. Unknown datatypes /
  channel types → `Unsupported(String)` so the metablock as a whole still
  parses; block reads against an `Unsupported` variant will fail
  explicitly when implemented.
- OSF4 short GPS spelling (`latitude` / `longitude` / `altitude`) accepted
  on read with a `log::debug!`; writers emit only the spec form
  `created_at_*`.

**Block-reader behaviour (Session 3):**

- Iterator-only API: `for block in &mut reader { … }`.
- Best-effort on truncation: file ending mid-block bumps
  `stats.blocks_truncated` from 0 to 1 (capped) and yields `None`.
- Skip-on-unsupported: channels declared as `DataType::Unsupported` or
  `ChannelType::Unsupported` produce `BlockKind::Skipped` with the
  payload bytes drained from the stream so other channels stay aligned.
- Skipped-payload capture is opt-in via
  `with_capture_skipped_payload(true)` — default is `None` / no
  allocation; specialists can fish out raw bytes of deprecated event
  blocks (`bcMessageEvent` etc.) without the library having to expose
  deprecated enum variants.
- Hard error `OsfError::UnknownChannelIndex(u16)` for indices missing
  from the metablock — without the channel record we cannot guess the
  length-prefix width, so this is a corruption signal.
- Optional `0xFFFF` info-data block plus 40-byte
  `OSF_STREAM_END <pos>===` magic trailer are consumed silently;
  `stats.trailer_seen` flips to `true`.
- All four supported control bytes (5/6/7/8) parse to typed payloads
  via `byteorder` little-endian primitives. String / binary blocks
  strip the trailing `0x00` per spec rev 2026-05-04 and try
  equal-length splitting for `N>1`, falling back to single-sample on
  uneven body lengths with a `log::warn!`.

**Manager-layer behaviour (Session 4):**

- `Channel` is an enum over three storage layouts: `Equidistant`
  (flat samples plus `Vec<Segment>`), `Timestamped` (parallel
  timestamp and value vectors), `Variable` (string / binary).
- Multi-`bcStartData` is first-class: every start block opens a new
  segment with `(start_timestamp_ns, sample_rate_hz, start_index,
  sample_count)` indexing into the channel's flat sample vector.
- Builder state machine: numeric channels start `Pending`, lock to
  `Equidistant` on first `bcStartData` or `Timestamped` on first
  `bcAbsTimeStampData`; mismatched later blocks return
  `OsfError::ChannelMixedBlockTypes`. Orphan continuations return
  `ContinuedDataWithoutStart`. `bcContinuedRelStampData` deltas are
  converted to absolute timestamps using the channel's last absolute
  ts as anchor; missing anchor → `RelStampWithoutAnchor`.
- Forward-compat: channels declared with `DataType::Unsupported` or
  `ChannelType::Unsupported` are dropped from the manager's channel
  list (their blocks are already `Skipped` at the reader level).
- Channel access mirrors DECISIONS §10: `channel(name)` is mandatory,
  `channel_by_index(u16)` is optional.
- Sample iteration: `samples_with_time()` yields
  `Sample<NumericValueRef<'_>>` for numeric / GPS channels and
  `Sample<VariableValueRef<'_>>` for string / binary. Eleven Copy
  scalars pass by value; `GpsLocation`, `&str`, `&[u8]` borrow from
  the channel.
- Flat-access helpers (`as_doubles_flat`, `as_int32_flat`, etc.)
  allocate fresh on every call and return
  `OsfError::DataTypeAccessMismatch` when the requested type does
  not match the stored datatype.

**Writer behaviour (Session 5):**

- OSF5 only (DECISIONS §6); always emits OSF5 even when the source
  manager came from an OSF4 file.
- Block mode only (DECISIONS §7); the builder accumulates samples in
  memory and emits at the end. Streaming write is reserved for
  embedded language targets.
- No OSFZ output (DECISIONS §12); compression is downstream concern.
- No trailer / no magic trailer — OSF5 dropped both.
- Equidistant blocks: numeric only (`f32` and `f64` per spec rev
  2026-05-04). The builder rejects other types up front. Each
  segment opens with `bcStartData`; long segments split into
  `bcContinuedData` blocks so payloads always fit the channel's
  `sizeoflengthvalue`. Reader merges them back transparently.
- Timestamped numeric blocks: `bcAbsTimeStampData` with the
  multi-sample bit set; chunked by `sizeoflengthvalue`.
- Variable (string / binary) blocks: one sample per block per spec.
  `sizeoflengthvalue` auto-bumps from 2 → 4 when a single sample
  would overflow the u16 length field; logged at debug level.
- File metadata defaults from DECISIONS §13: `created_utc` set to
  current UTC time at write (Howard Hinnant date math, no chrono
  dependency); `creator` defaults to `osf-core/<crate-version>`;
  `tag` defaults to `default`; `reason` and GPS fields omitted when
  unset (not written as null).
- Channel index is **not** preserved across round-trip — the writer
  assigns sequential 0..N indices. Names, datatypes, sample counts,
  segment boundaries, and bitwise sample values are preserved exactly.

**OSFZ behaviour (Session 6):**

- DECISIONS §12 was revised on 2026-05-06: deployed Optimeas devices
  emit gzip-wrapped OSF, not raw zlib as the original wording
  implied. Readers now detect both formats by leading magic bytes
  (gzip `0x1F 0x8B` or zlib `0x78 0x01/5E/9C/DA`) and wrap the
  stream in the matching `flate2` decoder transparently.
- `DataManager::load_from_file` / `load_from_reader` and the
  lib-level `read_file` convenience pick up the detection
  automatically. `BlockReader<R>` itself stays unchanged — the
  compression layer sits in front of it.
- `flate2 = { default-features = false, features = ["rust_backend"] }`
  pulls only pure-Rust crates (miniz_oxide, crc32fast, adler2).
  No system zlib, no MSVC linker complications.
- `ReaderStats` exposes `compressed: bool` and `compression_format:
  CompressionFormat` (None / Zlib / Gzip). `Display` adds a
  `Compressed: yes (gzip)` line when applicable.
- Writer side: never produces OSFZ output (DECISIONS §12 unchanged).

**Tests:** 123 unit tests across `header.rs`, `meta.rs`, `meta_json.rs`,
`meta_xml.rs`, `block.rs`, `reader.rs`, `stats.rs`, `data_channel.rs`,
`manager.rs`, `binary_write.rs`, `writer.rs`, `compression.rs`. Six
integration suites:

- `tests/header_test.rs` — every shipped `.osf` parses its magic header.
- `tests/metablock_test.rs` — every shipped `.osf` parses its metablock.
- `tests/block_test.rs` — every shipped `.osf` streams blocks cleanly
  via `BlockReader`.
- `tests/manager_test.rs` — every shipped `.osf` assembles into a
  `DataManager`; channel-by-name lookup verified on `steam_loco.osf`.
- `tests/roundtrip_test.rs` — every shipped `.osf` survives load +
  write + reload with bitwise sample comparison; OSF4-source files
  are confirmed to produce OSF5 output.
- `tests/osfz_test.rs` — `weather_station.osfz` field sample
  (gzip-OSFZ) plus synthetic gzip and zlib re-wraps of
  `steam_loco.osf` produce identical channel sets to the plain
  source.

`manager_test.rs` and `roundtrip_test.rs` each carry an `#[ignore]`-
gated performance smoke. Manual run via `cargo test --release --
--ignored` measures `steam_loco.osf` at ~3 ms read and ~3 ms write
locally — both well under the brief budgets (100 ms read, 100 ms
write).

`cargo build`, `cargo test`, and `cargo clippy --all-targets` all run
clean.

**Examples:**

- `cargo run --example inspect -- <path>` — header + metadata +
  per-channel summary (no block reading).
- `cargo run --example stats -- <path> [top_n]` — full read producing
  `ReaderStats` plus the top-N raw channels.
- `cargo run --example dump -- <path> [top_n]` — manager-driven typed
  channel summary plus first-channel detail with reconstructed
  per-sample timestamps.
- `cargo run --example copy -- <input> <output>` — load via
  `DataManager`, write OSF5 via `writer::write_to_file`, verify by
  reload (writer demo).

Diagnostics flow through `env_logger`; default `RUST_LOG=warn`, override
with `debug` for full alias / unknown-field tracing or `error` for
clean output on files that flood deprecated-field warnings.

**Next steps:** Python bindings via PyO3 + maturin (Session 7a
landed; pandas convenience in 7b; CI + wheel matrix in 8).

---

## Python implementation — current state

Session 7a (this session): the PyO3 binding crate at
`implementations/python/` is functional. Distribution name on PyPI
is `osfdata`; Python import name is `osf` (the established split
mirroring sklearn / yaml / bs4).

**Crate at `implementations/python/`:**

| Module | Public surface |
|---|---|
| `_osf` | `__version__`, `OsfError`; classes `DataManager`, `Channel`, `Segment`, `ReaderStats`, `WriterBuilder`; functions `load(path)`, `save(mgr, path)` |
| `osf` | Re-exports the above; ships `_osf.pyi` type stubs and `py.typed` marker |

**Build & install (Windows / `uv` venv shown; bash equivalent on macOS / Linux):**

```bash
cd implementations/python
uv venv
.venv/Scripts/Activate.ps1
uv pip install maturin pytest
maturin develop --release
pytest tests/
```

`maturin develop` produces an editable install — Python-side
changes are immediate, Rust-side changes need a rebuild.

**Key bindings:**

- `osf.load(path)` — opens an OSF or OSFZ file, drops the GIL
  during I/O, returns a `DataManager`.
- `osf.save(mgr, path)` — writes the manager back as OSF5
  (DECISIONS §6).
- `Channel.samples()` returns a NumPy array for numeric / GPS
  channels (`(N,)` for scalars, `(N, 3)` for `gpslocation`),
  `list[str]` / `list[bytes]` for variable channels.
- `Channel.timestamps_ns()` returns an int64 NumPy array;
  equidistant timestamps are reconstructed from segments on demand.
- `WriterBuilder` has chainable file-info setters and `add_*`
  methods that dispatch over the input NumPy array's dtype.
- Transparent OSFZ (gzip + zlib) is inherited automatically from
  `osf-core`; `mgr.stats.compressed` and `compression_format`
  surface the result.

**Tests:** 13 pytest cases under `implementations/python/tests/`
exercise the reader, writer, manager-by-name lookup, NumPy dtype
assertions, segment access, OSFZ detection, and a builder
roundtrip. Local run: 13/13 in 0.35 s.

**Performance:** `osf.load("examples/steam_loco.osf")` measures
~3 ms in release builds (matched pair of 5 runs locally), same
order as the underlying Rust read. Channel access plus NumPy
conversion adds ~0.3 ms per channel. The clone-pfad
(`mgr.channel(name).samples()` clones the `Vec<T>` once) is fast
enough that the Arc-Channel optimisation is not needed yet.

**Constraints:**

- abi3-py39 — one wheel per platform covers Python 3.9 through
  3.13.
- PyO3 0.22 + numpy 0.22 (matched pair per the rust-numpy README;
  bumping one requires bumping the other).
- Pure-Rust dependency graph (no system zlib, no MSVC linker
  surprises).

**Pending:** pandas `DataFrame` convenience (Session 7b).

---

## CI / release pipeline (Session 8)

GitHub Actions workflows live in `.github/workflows/`:

- `ci.yml` — runs on every push to `main`, every PR, and on
  `workflow_dispatch`. Three job groups: `test-rust` (cargo test +
  clippy), `build-wheels` (4-platform matrix via maturin-action +
  per-wheel pytest run), `build-sdist`. A `summary` job aggregates
  results for branch-protection gating.
- `release.yml` — triggered by `v*`-tag pushes. Same wheel + sdist
  matrix; the `publish-testpypi` job uploads to TestPyPI via
  Trusted Publishing (OIDC, no API tokens).

**Wheel matrix (4 (os, target) pairs, abi3-py39 → one wheel per
platform covers Python 3.9–3.13):**

| OS              | Target  | Notes                                       |
|-----------------|---------|---------------------------------------------|
| ubuntu-latest   | x86_64  | native                                      |
| ubuntu-latest   | aarch64 | QEMU emulation via maturin-action           |
| macos-14        | aarch64 | Apple Silicon (arm64-only — DECISIONS §19)  |
| windows-latest  | x64     | native                                      |

Intel-macOS is intentionally not in the matrix; users install from
the sdist if needed. See DECISIONS.md §19 for the reasoning.

**Action versions (current major tags as of Session 8):**

`actions/checkout@v6`, `actions/setup-python@v6`,
`actions/upload-artifact@v7`, `actions/download-artifact@v7`,
`PyO3/maturin-action@v1`, `pypa/gh-action-pypi-publish@release/v1`,
`dtolnay/rust-toolchain@stable`, `Swatinem/rust-cache@v2`.

### Session 8 — Phase A (CI stabilization)

- 2026-05-06/07: GitHub Actions wheel-build pipeline established.
  After several Concurrency-Group cancellations and a macos-13
  runner-availability dead-end, the matrix was tightened to four
  arm64+x86_64 platforms and the workflow proven stable across
  multiple consecutive `main` pushes (~3:30 per run).

### Session 8 — Phase B (TestPyPI release)

- 2026-05-07: `osfdata 0.1.0` released on TestPyPI via Trusted
  Publishing — first successful end-to-end release pipeline run.
  Tag `v0.1.0` triggered `release.yml`, which built four wheels
  plus the sdist and published them via OIDC.
- Live: <https://test.pypi.org/project/osfdata/>
- Trusted Publisher on TestPyPI active (account: optiMEAS,
  project: `osfdata`, owner: `optimeas`, repo: `osf`, workflow:
  `release.yml`).
- Verification install in fresh venv passed:
  `cp39-abi3-win_amd64` wheel installed, `osf.__version__ == "0.1.0"`,
  numpy 2.4.4 pulled in as dependency.

### Open / known follow-ups (Session 8)

- Production PyPI release: requires a separate Trusted Publisher
  configured on `pypi.org` (TestPyPI and production are independent
  accounts).
- Pandas convenience layer (Session 7b): build a DataFrame from a
  `DataManager`, one column per channel, optional time alignment.
- Other language implementations (C, C++, C#, MicroPython, …) will
  reuse the same per-package CHANGELOG + Trusted Publishing pattern.

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
- **Rust** — read path complete (header + metablock + block reader +
  DataManager + transparent OSFZ); OSF5 writer landed with full
  round-trip validation.
- **Python** — PyO3 bindings live for read + write + OSFZ via the
  `osfdata` distribution (import as `osf`); 13 pytest cases pass
  locally; CI builds wheels for 5 platforms on every push (Session
  8 Phase A). Trusted Publishing to TestPyPI configured but gated
  off (Session 8 Phase B); pandas convenience pending (Session 7b).
- **`optimeas/python-osf` deprecation header** — once `osfdata`
  appears on TestPyPI, add a "deprecated in favor of osfdata"
  notice to that repo's README. Mini follow-up session.
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
