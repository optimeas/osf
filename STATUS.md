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
implementations are MIT-licensed.

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
│   │   ├── integrations/{index, python}.md
│   │   ├── tools/{index, osftool}.md  — osftool CLI documentation
│   │   └── media/                   — shared images
│   ├── en/                          — English mirror, same structure
│   └── scripts/docs-to-pdf.py       — builds one combined PDF per language (docs/pdf-out/, gitignored)
├── implementations/
│   ├── delphi/                      — reference implementation (full)
│   │   ├── src/                     — library units
│   │   ├── src/hdf5/                — HDF5 DLL wrapper units (Windows)
│   │   ├── demos/osfviewer/         — viewer (uses TeeChart)
│   │   ├── demos/osfgenerator/      — writes the reference set
│   │   ├── demos/osfcsvexport/      — OSF → CSV export demo
│   │   ├── demos/osfmerger/         — VCL merger GUI (OsfMerger, Win64)
│   │   ├── tools/osftool/           — osftool CLI (9 verbs, Win64)
│   │   ├── setup/                   — Inno Setup installer for osftool
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
│   ├── Testdata Train OSFZ/         — one week of OSF4-OSFZ field recordings (346 files, daily dirs)
│   └── generated/                   — 17 reference files (from OSFGenerator)
├── dataformats/
│   └── hdf5/                        — language-agnostic HDF5 spec, knowledge base, DLL install scripts
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
| `OSF.Filer` | `TOSFFile` — streaming reader/writer for OSF4 and OSF5. `WriteEquidistantBlock(...)` requires `Channel.SampleRate > 0` and a non-zero `FirstTimestampNs` to start a new segment. `WriteTimestampedSample/Block/Doubles` for timestamped channels. Auto-appends/strips the `0x00` for `string`/`binary`. Adds an optional read-side `ChannelFilter: TArray<string>` (skips blocks of channels not in the list — info blocks always pass through). Transparent **OSFZ (gzip) decompression**: `OpenForRead` peeks the `1F 8B` magic and wraps the stream in `TZDecompressionStream`. OSF4 XML metablock is parsed via **OmniXML** (`GetDOMVendor(sOmniXmlVendor)`) so reads no longer need MSXML installed. |
| `OSF.Data.Channels` | Typed in-memory channels. **`TOSFEquidistantDataChannel.Segments: TList<TOSFChannelSegment>`** maps the flat `Values` list onto absolute time — every `bcStartData` opens a new segment with `(StartTimestampNs, StartIndex, SampleCount)`. |
| `OSF.Data.Manager` | `TOSFDataManager.LoadFromFile/Stream` — high-level read; populates typed channels. Pass-through `ChannelFilter: TArray<string>` forwarded to the internal `TOSFFile`; excluded channels get no `TOSFDataChannel` at all. |
| `OSF.Export` | `TOSFExporter` abstract base (`ExcludeEmptyChannels`, `AbsoluteTimestamps`) |
| `OSF.Export.CSV` | `TOSFCSVExporter` — per-channel XY CSV; `(DecimalSeparator, ColumnSeparator, Encoding, TimestampFormat)` |
| `OSF.Export.CSV.Unified` | `TOSFUnifiedCSVExporter` — single shared timeline: one timestamp column + one value column per channel, empty cell where a channel has no sample. `TUnifiedCSVTimestampFormat` (datetime / seconds / iso8601 / nanoseconds). O(N) cursor-walk over the merged, de-duplicated timeline. |
| `OSF.Meta.Cache` | `TOSFMetaCache` — sidecar `.json` per OSF/OSFZ file (source size + mtime validity stamp, global and per-channel first/last timestamps and sample counts; `CachePathFor` maps `foo.osf`/`foo.osfz` → `foo.json`). `TOSFMetaCacheBuilder` scans a file via `TOSFFile` discarding sample payloads. |
| `OSF.Merger` | `TOSFMerger` — scans a directory (or explicit `FileList`) for OSF/OSFZ files overlapping a UTC interval, merges selected channels into a single OSF4/OSF5 output. Cache-driven file selection, `osSkip`/`osOverwrite` overlap strategy, per-sample interval clipping. Output is emitted as `bcAbsTimeStampData` (equidistant inputs expanded to per-sample timestamps). |
| `OSF.Export.HDF5` | `TOSFHDF5Exporter` — exports a `TOSFDataManager` as an HDF5 file: one chunked / shuffled / deflated 1-D dataset of `{int64 timestamp_ns; value}` compound records per channel, the channel name split on the namespace separator into HDF5 groups, file and channel metadata as root/dataset attributes. Covers `bool`, every signed/unsigned integer width, `float`, `double`, `gpslocation` (a lat/lon/alt sub-compound) and `string` (variable-length UTF-8); `binary` is skipped. Configurable `ChunkSize`, `DeflateLevel`, `UseShuffle`, `NamespaceSep`, `LibraryDir`. Windows-only. |

| `OSF.Version` | osftool version single-source-of-truth: `OSFTOOL_VERSION` constant + `GetVersionString` / `GetFullVersionString` (build timestamp taken from the executable's own file date). |
| `OSF.Progress` (+ `.Console` / `.Quiet` / `.Verbose` / `.Json` / `.Fallback` / `.Live` / `.LogFile`) | Reusable, OSF-agnostic `IProgressReporter` abstraction for long-running multi-file operations: structured phase events, a generic `StartProgress`/`DoProgress`/`EndProgress` progress-bar triplet, and a `Log` catch-all; six reporter implementations (live progress bar, plain redirect-fallback, quiet, verbose, JSON-Lines) and a log-file decorator. The live reporter draws a single in-place progress-bar line that carries the current file name. Consumed by `osftool merge`. |

**HDF5 DLL binding in `implementations/delphi/src/hdf5/`:** `Hdf5.Types`,
`Hdf5.Api` and `Hdf5.Wrapper` form a reusable, OSF-agnostic Delphi binding
to the HDF5 C library — `cdecl` function-pointer types, a six-stage
`hdf5.dll` resolver, the mandatory `H5open`-first initialisation with
`_g`-global readout, and RAII handle wrappers. `OSF.Export.HDF5` builds on
them. The runtime itself is never committed; `dataformats/hdf5/lib/install-hdf5.ps1`
fetches HDF5 1.14.4-3 from the HDF Group.

**`OSFCompileCheck.dpr`** at the implementation root is a no-form `uses`-only
program covering every `OSF.*` unit (including `OSF.Meta.Cache` and
`OSF.Merger`); running `dcc32 -B OSFCompileCheck.dpr` from
`implementations/delphi/` gives a clean compile signal after refactors.

---

## Demos

| Demo | Purpose |
|---|---|
| `demos/osfviewer/` | Loads an OSF file, lists channels, renders selected channel as a TeeChart. Uses TeeChart units — only builds inside the IDE (search path defined in the .dproj). |
| `demos/osfgenerator/` | Writes the 17-file reference set (8 OSF4 + 9 OSF5) into `examples/generated/`. Form has output-dir picker, OSF4/OSF5 toggles, samples-per-channel spinedit, log memo. |
| `demos/osfcsvexport/` | Pipeline demo: `TOSFFile` → `TOSFDataManager` → `TOSFCSVExporter`. Exposes the four exporter options + a debug toggle. |
| `demos/osfmerger/` | VCL front-end for `TOSFMerger` (`OsfMerger.exe`, Win64). PageControl with directory-scan / explicit-file-list tabs, channel-filter memo, overlap + output-format options, scan/merge/save actions, found-files and merge-result list views, debug toggle + log memo. |

`osfviewer` / `osfgenerator` / `osfcsvexport` compile with `dcc32`
(`OSFViewer` only via IDE because of TeeChart); `osfmerger` compiles
Win64 with `dcc64`. Delphi 12 / RAD Studio 23.0.

---

## Delphi CLI — osftool

`implementations/delphi/tools/osftool/` — a verb-based command-line tool,
Win64-primary. `OsfTool.dproj` also carries `OSX64` / `OSXARM64` /
`Linux64` build configurations; the source is conditional-compilation
clean for those targets (`{$IFDEF MSWINDOWS}` guards around the registry
and `Winapi` units), verified by inspection only — the build host has no
macOS/Linux toolchain.

Nine verbs, dispatched by `TOsfToolDispatcher`:

| Verb | Purpose |
|---|---|
| `merge` | Merge OSF files from a directory into one OSF — positionals `<rootdir> <outputfile> [channel ...]`; `--start`/`--end` ISO-8601 interval bounds (default `1970-01-01`..now), `--osf4`, `--overwrite`, `--no-cache`; output-mode flags `-q`/`--quiet`, `-v`/`--verbose`, `--json` (JSON-Lines event stream), `--log <path>`. Default run shows a live progress bar via the `OSF.Progress.*` reporter subsystem. Wraps `TOSFMerger` |
| `export` | Export channels — `--format csv` (per-channel XY), `unified-csv` (single shared timeline) or `hdf5` (Windows; one compound dataset per channel via `TOSFHDF5Exporter`); `--timestamp-format`, `--decimal-sep`, `--encoding`, `--start/--end`, plus the HDF5 options `--chunk-size`, `--deflate-level`, `--no-shuffle`, `--namespace-sep`, `--hdf5-lib-dir` |
| `info` | File metadata + global time range (cache-backed when a valid sidecar exists) |
| `channels` | List channels with optional `--filter` wildcard (`System.Masks`) |
| `stat` | Per-channel min/max/mean/stddev via single-pass Welford; `--start/--end` interval filter |
| `cache` | `build` / `rebuild` / `clean` / `status` of `.json` sidecars under a root dir |
| `config` | `show` / `set` / `reset` settings; `install-path` / `uninstall-path` add/remove the exe dir from the user PATH (HKCU registry on Windows, shell-snippet print on POSIX) |
| `convert` | OSF4 ↔ OSF5 round-trip via `TOSFMerger` |
| `verify` | Block-level integrity check (channel-index coverage, timestamp monotonicity, truncation) |

Shared infrastructure: `IOsfCommand` + `TBaseCommand` (argument parsing,
stdout/stderr split, global `--json` / `--quiet` / `--verbose`).
`TOsfToolConfig` persists settings at `%APPDATA%\osftool\config.json`
(Windows) or `~/.config/osftool/config.json` (POSIX). Uniform exit codes:
0 ok, 1 bad args, 2 not found, 3 io error, 4 format error. Compiles
clean with `dcc64`.

The top-level dispatcher also handles `--version` / `-V` (plus `--short`),
sourced from the `OSF.Version` unit (osftool 1.1.0). The `merge` verb
renders progress through the `OSF.Progress.*` reporter subsystem — a
single-line live progress bar by default, the `--verbose` / `--json` /
`--quiet` / `--log` alternatives, and an automatic plain-text fallback
when stdout is redirected. On Windows the console is switched to the
UTF-8 code page at startup so non-ASCII output renders correctly.

### osftool installer

`implementations/delphi/setup/osftool.iss` is an Inno Setup 6 script that
packages osftool as a Windows installer: it deploys `OsfTool.exe` plus the
HDF5 runtime (`hdf5.dll` and the bundled MSVC redistributable DLLs) into a
`lib\` subfolder, adds the install directory to PATH, and lets the user
choose an all-users (Program Files, system PATH) or per-user
(`%LocalAppData%`, user PATH) install at runtime. The compiled installer
binary is not committed; the build prerequisites are documented in the
script header.

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

## C++ implementation — current state

Phase 1 (skeleton) completed 2026-05-08; Phase 2 (magic-header parser) completed 2026-05-10; Phase 3 (OSF5 JSON metablock parser) completed 2026-05-19; Phase 4 (OSF4 XML metablock parser) completed 2026-05-23; Phase 5 (block-stream reader) completed 2026-05-23. Per [DECISIONS §20](DECISIONS.md#20-c-implementation-architecture).
Standalone C++17 implementation, parallel to the Rust core — not a port from C, not a wrapper around the Rust crate. Foundation API, magic-header surface, both OSF4 + OSF5 metablock parsers, and the block-stream reader (with `ReaderStats`) are in place; the typed `DataManager` arrives in Phase 6.

**Library targets:**

- `osf::osf` — static library (default; shared if `BUILD_SHARED_LIBS=ON`). Internal CMake name is `osf_core`; `OUTPUT_NAME osf` keeps the produced file as `libosf.a` / `osf.lib`.
- `osf::headers` — INTERFACE target carrying the public include paths plus the vendored `tl::expected` and `nlohmann/json` directories (both attached SYSTEM so upstream warnings stay silent).

**Tree at `implementations/cpp/`:**

| File | Purpose |
|---|---|
| `CMakeLists.txt` | Top-level config: project, C++17 hard-pin, five build options, both library targets, `add_subdirectory(tests)` gated by `OSF_BUILD_TESTS`; `osf_core` lists `error.cpp` + `header.cpp` + `metablock.cpp` + `types.cpp` |
| `cmake/CompilerWarnings.cmake` | `osf_set_warnings(target)` — MSVC `/W4 /permissive- /wd4100`; GCC/Clang `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion` |
| `cmake/version.hpp.in` | Template; `configure_file` emits `${BINARY_DIR}/generated/osf/version.hpp` with `OSF_VERSION_MAJOR/MINOR/PATCH` and `osf::version()` |
| `include/osf/osf.hpp` | Umbrella header (re-exports `error.hpp` + `header.hpp` + `metablock.hpp` + `types.hpp` + `version.hpp`) |
| `include/osf/error.hpp` | `osf::Error` (`Code` enum: Unknown / InvalidArgument / IoError / ParseError / NotFound / InvalidMagicHeader / UnsupportedVersion / MagicHeaderTooLong / InvalidMetablock / RemovedInSpec / JsonParseError; plus `std::string message`); `osf::Result<T>` as `tl::expected<T, Error>`; `error_category_name(Code)` declaration |
| `include/osf/header.hpp` | Magic-header API: `osf::OsfVersion` (Osf4/Osf5 enum), `osf::MagicHeader` struct (version + metablock_len, friend equality), three `parse_magic_header` overloads (`std::istream&`, `std::uint8_t const*` + size, `std::filesystem::path`), `osf::MAX_MAGIC_HEADER_LEN = 128` |
| `include/osf/types.hpp` | Core OSF type enumerations (spec rev 2026-05-04): `DataType` (Bool / Int8..Int64 / UInt8..UInt64 / Float / Double / String / Binary / ByteArray / GpsLocation / Unsupported), `ChannelType` (Scalar / Equidistant / Timestamped / Unsupported), `SpectrumType` (Amplitude / RealImag / AmpPhaseRad / AmpPhaseDeg); plus `parse_data_type` / `parse_channel_type` (Result-returning) and `parse_spectrum_type` (noexcept) |
| `include/osf/metablock.hpp` | OSF metablock data model: `FileInfo`, `Channel`, `Info`, `MetaBlock` structs (`std::optional<T>` everywhere the Rust reference has `Option<T>`); `parse_metablock_json` (OSF5) and `parse_metablock_xml` (OSF4), each in two overloads (`std::uint8_t const*` + size; `std::string_view`) — both populate the same `MetaBlock` data model |
| `include/osf/block.hpp` | OSF block model: `Block` struct, `BlockKind` as `std::variant<StartData, ContinuedData, AbsTimestampData, ContinuedRelStampData, Skipped>`, payload sum types (`NumericPayload` / `TimestampedPayload` / `RelTimestampedPayload` — each a `std::variant` of `std::vector<T>` per spec datatype), `GpsLocation`, `SkipReason`, `ControlByte` / `ControlKind`, `decode_control_byte`. Two helper free functions per payload type — `*_payload_len` (samples-held count) and `*_payload_empty`. Constants `TRAILER_CHANNEL_INDEX = 0xFFFF` and `MAGIC_TRAILER_LEN = 40`. |
| `include/osf/stats.hpp` | Reader telemetry: `ReaderStats` (byte/block counters, channel counters, `elapsed`, `trailer_seen`, `compressed`, per-channel `std::unordered_map<u16, ChannelStats>`) and `ChannelStats` (name, blocks/skipped/samples/bytes/segments, `time_range_ns`). `format_bytes`, `format_duration`, `compression_format_name`. `operator<<` overloads format the structs in the same shape as the Rust reference. |
| `include/osf/reader.hpp` | Block-stream reader: `BlockReader` class — constructor takes `std::istream&` (positioned after the metablock) + `MetaBlock const&`; fluent setters `with_capture_skipped_payload(bool)` and `with_file_size(u64)`; primitive `next() -> std::optional<Result<Block>>`; range-based-for support via `begin()` / `end()` (input iterator + `EndSentinel`); `stats()`, `blocks_truncated()`, `trailer_seen()`, `file_size_bytes()`. Best-effort on truncation (`stats().blocks_truncated` bumped, iteration ends cleanly), hard error on unknown channel index, forward-compat `Skipped` records for `Unsupported` channels and deprecated / reserved control bytes, silent consumption of the optional 0xFFFF info block + 40-byte magic trailer. |
| `src/error.cpp` | `error_category_name` implementation; covers all eighteen `Error::Code` enumerators |
| `src/header.cpp` | Magic-header parser implementation. Byte-by-byte read via `istream::get()`, anonymous-namespace helpers for line read / identifier mapping / `from_chars`-based length parse, CRLF tolerance |
| `src/types.cpp` | `parse_data_type` / `parse_channel_type` / `parse_spectrum_type` implementations. If-chain over wire spellings; `bytearray` normalises to `Binary`; removed datatypes (`gpsdata` / `pair` / `triple` / `candata`) reject with `Error::Code::RemovedInSpec` and a replacement-hint message |
| `src/metablock.cpp` | `parse_metablock_json` implementation over vendored `nlohmann::json`. `allow_exceptions=false` parse keeps the core API exception-free; anonymous-namespace helpers (`invalid_metablock`, `is_discarded`, `get_optional_string`, `get_optional_double`, `validate_size_of_length_value`, `parse_file_info`, `parse_channel`, `parse_channels`, `parse_infos`) factor out per-field work; deprecated channel fields tolerated silently; `created_at_*` short-spelling fallback (`latitude=` etc.) accepted on read |
| `src/metablock_xml.cpp` | `parse_metablock_xml` implementation over vendored `pugi::xml_document`. DOM-walking parser, not event-driven (XPath not needed; the metablock is small). pugixml is configured with `parse_default` plus the explicit `encoding_utf8` hint so CP1252-in-UTF-8 byte sequences in real field files (`°` in `°C` etc.) become Unicode replacement characters rather than parse errors. Anonymous-namespace helpers (`invalid_metablock`, `xml_parse_error`, `has_attr`, `get_optional_string`, `get_required_string`, `parse_optional_double`, `parse_optional_i64`, `validate_size_of_length_value`, `parse_optimeas_attrs`, `parse_channel`, `parse_channels`, `parse_info`, `parse_infos`) keep per-attribute work tight; deprecated channel fields tolerated silently; `created_at_*` short-spelling fallback (`latitude=` etc.) accepted on read. The root element must be `<optimeas>`; unknown children and unknown attributes are ignored (forward-compat). The `<channels count="N">` attribute is informational — actual channel count comes from element children, matching the Rust reference |
| `src/block.cpp` | `numeric_payload_len` / `timestamped_payload_len` / `rel_timestamped_payload_len` and their `_empty` variants via `std::visit` over the variant; `decode_control_byte` switch over the 9 documented values plus the multi-sample-bit extraction |
| `src/stats.cpp` | `ChannelStats::observe_timestamp`, `format_bytes` (binary KB/MB/GB thresholds), `format_duration` (sub-second → ms, otherwise s), `compression_format_name`, `operator<<` for both stats structs |
| `src/reader.cpp` | `BlockReader` implementation. Byte-by-byte little-endian decoders (`le_u16` / `le_u32` / `le_u64` plus signed/float overloads); `PayloadCursor` walks an in-memory payload returning `std::optional<T>` on overflow; per-control-kind typed parsers (`parse_start_data`, `parse_continued_data`, `parse_abs_timestamp_data` with the spec-mandated string/binary equal-length-segments path, `parse_continued_rel_stamp_data`); `record_skip` / `skip_block` for the forward-compat skip path with capture-or-drain selector; `consume_trailer` for the 0xFFFF info block + 40-byte magic trailer. `BlockReader::Iterator` is a single-pass input iterator that fetches the next block on increment and compares equal to `EndSentinel` once `next()` returned `std::nullopt`. |
| `tests/CMakeLists.txt` | GoogleTest via `FetchContent`; pinned to v1.15.2 by tarball URL + SHA256; `gtest_force_shared_crt=ON` for /MD parity; `DOWNLOAD_EXTRACT_TIMESTAMP=FALSE` for CMP0135 NEW behaviour; `OSF_EXAMPLES_DIR` define for integration tests |
| `tests/integration/test_header_examples.cpp` | Four integration tests against `examples/`: `motorbike.osf` and `steam_loco.osf` parse as Osf4; `weather_station.osfz` rejects until Phase 8; the 17 generated files in `examples/generated/` all parse with version per filename prefix |
| `tests/integration/test_metablock_examples.cpp` | Three integration tests against the OSF5 reference files in `examples/generated/`: snapshot check on `osf5_equidistant.osf`; every `osf5_*.osf` parses with non-empty channels and valid `sizeoflengthvalue`; `osf5_gpslocation.osf` actually declares a `GpsLocation` channel |
| `tests/integration/test_metablock_xml_examples.cpp` | Six integration tests against `examples/generated/osf4_*.osf` plus the two field samples: snapshot check on `osf4_equidistant.osf`; every `osf4_*.osf` parses with valid `sizeoflengthvalue`; `osf4_gpslocation.osf` declares a `GpsLocation` channel; `motorbike.osf` and `steam_loco.osf` metablocks parse end-to-end (encoding-tolerance + deprecated-field-tolerance paths); cross-parser symmetry probe (`osf4_equidistant.osf` via XML parser matches `osf5_equidistant.osf` via JSON parser on every channel field) |
| `tests/integration/test_reader_examples.cpp` | Six BlockReader integration tests: every `.osf` under `examples/generated/` streams end-to-end producing at least one block; first-block snapshots on `osf5_scalar_int64.osf` (single-sample AbsTs Int64) and `osf4_equidistant.osf` (StartData with sample_rate > 0); `motorbike.osf` and `steam_loco.osf` field samples stream clean; reader-stats sanity (non-zero counters, at least one channel produces a time range) |
| `tests/unit/test_error.cpp` | Five smoke tests covering Error, Result-with-value, Result-with-error, `osf::version()`, and `error_category_name` |
| `tests/unit/test_header.cpp` | 16 unit tests against synthetic byte sequences: identifier spellings, error codes, CRLF tolerance, lone-CR rejection, stream-position invariant, buffer↔istream equivalence, path overload (success + missing-file), `MagicHeader` equality |
| `tests/unit/test_types.cpp` | Nine unit tests for `parse_data_type` / `parse_channel_type` / `parse_spectrum_type`: every current spelling, `bytearray` alias, removed-in-spec rejection, unknown-spelling fallback |
| `tests/unit/test_metablock.cpp` | 20 unit tests for `parse_metablock_json`: minimal + full channel + infos round-trip, forward-compat (unknown top-level + deprecated channel fields tolerated), negative cases (removed datatype, missing envelope, malformed JSON, non-object root, non-array channels/infos, every required channel field missing, index out-of-range, invalid `sizeoflengthvalue`), overload-agreement, null-pointer edge cases |
| `tests/unit/test_metablock_xml.cpp` | 20 unit tests for `parse_metablock_xml`: minimal + full channel + infos round-trip, short-form / long-form geolocation, `bytearray` alias, `count` mismatch tolerance, deprecated `scale`/`offset` tolerated, unknown attribute ignored, negative cases (removed datatype, wrong root, malformed XML, every required-attribute-missing case, invalid `sizeoflengthvalue`, channel-index out-of-u16-range, non-numeric `timeincrement`), overload-agreement, null-pointer edge cases |
| `tests/unit/test_block.cpp` | 7 unit tests covering payload `len()` helpers (numeric / timestamped / rel-timestamped, including string / binary / GpsLocation), `decode_control_byte` for every documented value plus the multi-sample bit and unknown-byte fallback, `GpsLocation` equality, default `Skipped::payload` is `nullopt` |
| `tests/unit/test_stats.cpp` | 6 unit tests for `ChannelStats::observe_timestamp` (two-sided growth), `format_bytes` (unit thresholds), `format_duration` (ms / s split), `compression_format_name`, and the two ostream overloads |
| `tests/unit/test_reader.cpp` | 22 BlockReader unit tests against synthetic byte sequences (port of the Rust reader suite): empty stream, three truncation paths (channel-index / length-field / mid-payload), unknown-channel-index hard error, `Unsupported`-channel skip with stream alignment, capture-skipped opt-in, deprecated control bytes 1/3/4, unknown high control byte 0x55, every typed parser (single + multi for AbsTs int64 / double, StartData double + float-N10, ContinuedData int16-N4, AbsTs string with null-strip, AbsTs binary, AbsTs gpslocation, ContinuedRelStampData int16), `InvalidBlock` for equidistant-on-string, trailer consumption, range-based-for iteration |
| `third_party/tl-expected/` | Vendored TartanLlama/expected v1.3.1 (`tl/expected.hpp` + `LICENSE`, CC0 1.0) |
| `third_party/nlohmann-json/` | Vendored nlohmann/json v3.11.3 (`nlohmann/json.hpp` + `LICENSE`, MIT). Single-header form; SHA-256 of `json.hpp` matches the upstream v3.11.3 release asset |
| `third_party/pugixml/` | Vendored pugixml v1.15 (`pugixml.hpp` + `pugixml.cpp` + `pugiconfig.hpp` + `LICENSE`, MIT). Unlike the other two vendored libraries pugixml is not header-only; the `.cpp` compiles into `osf_core` directly with warnings disabled (`/W0` on MSVC, `-w` on GCC/Clang). SHA-256: `pugixml.hpp = 2555F950…0043BE734`, `pugixml.cpp = 67C3892E…D09E0744`, `pugiconfig.hpp = 981CD9AD…FC98B92D` |
| `README.md`, `BUILD.md`, `CHANGELOG.md` | Per-package documentation |

**Build options (DECISIONS §20):**

- `BUILD_SHARED_LIBS` (default OFF), `OSF_BUILD_TESTS` (default ON) — effective in Phase 1.
- `OSF_BUILD_EXAMPLES` (default ON, no-op until Phase ≥3), `OSF_BUILD_C_API` (default OFF, activates in Phase 11), `OSF_USE_SYSTEM_ZLIB` (default OFF, relevant once Phase 8 lands) — declared now, enabled in their respective phases.

**Build & test:**

```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

**Build verification (local, 2026-05-23):**

- Toolchain: MSVC 19.50.35730, Visual Studio 18 generator, CMake 4.2.3.
- `cmake -B build` configures with **0 CMake warnings** (CMP0135 set to NEW explicitly via `DOWNLOAD_EXTRACT_TIMESTAMP=FALSE`).
- `cmake --build` produces `osf.lib` plus twelve test executables with **0 compile warnings** under `/W4 /permissive-` (the vendored `pugixml.cpp` is built with `/W0` since it is treated as binary-identical to upstream).
- `ctest` reports **124/124 passed in 3.94 s** (5 + 16 + 4 + 9 + 20 + 3 + 20 + 6 + 7 + 6 + 22 + 6 = 124: error-smoke + header-unit + header-integration + types-unit + metablock-unit + metablock-integration + metablock-xml-unit + metablock-xml-integration + block-unit + stats-unit + reader-unit + reader-integration; the reader integration suite internally exercises every `.osf` under `examples/generated/` plus the `motorbike.osf` and `steam_loco.osf` field samples).

**Constraints:**

- C++17 is hard-pinned in `CMakeLists.txt` (DECISIONS §20). No `OSF_CXX_STANDARD` switch — moving to C++20 or later is a deliberate library upgrade, not a build option.
- The library is Qt-neutral; a Qt-aware module may follow as a separate `integrations/` entry once the core is stable.

**Pending:**

Phase 6: typed `DataManager` (per-channel sample assembly across the
`Block` stream, segment tracking, channel-by-name lookup, sample
iterators). Then sequentially per §20 Implementation Order: OSF5
writer, transparent OSFZ decompression, throwing convenience layer.

The C ABI shared-library wrapper (Phase 11) is the deferred deliverable for cross-language consumption — Windows DLL / ActiveX/OCX, future bindings. It will land after the core C++ library reaches roundtrip validation and gets its own DECISIONS entry covering handle patterns, error codes, string ownership, and ABI stability guarantees.

CI integration (Phase 10) will extend `ci.yml`'s path filter to `implementations/cpp/**` and add a Linux/macOS/Windows job matrix for the C++ build.

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
