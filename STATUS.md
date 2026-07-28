# OSF — Project Status

Snapshot for resuming work in a new chat session. Pair with `DECISIONS.md`,
`CHANGELOG.md`, and `docs/en/osf_general.md` (or `docs/de/osf_general.md`)
when deeper context is needed.

| Field | Value |
|---|---|
| Repo | https://github.com/optimeas/osf |
| Working dir | `V:\github\osf` (Windows) |
| Latest tag | **v0.10.0** (2026-05-25) |
| Branch | `main` |
| Spec revision in effect | **2026-07-28** (`bcMessageEvent` read-mandatory, OSF-UP4; zero-length data blocks, OSF-UP3 — both dated 2026-07-28, so the value does not move; OSF5 integrity profile 2026-07-07; base format 2026-05-24) |

**Public-release prep — done (on `main`).** All four phases are complete:
Phase 1 (repo cleanup), Phase 2 (Docusaurus integration), Phase 3
(per-implementation + examples developer docs, **DE + EN**) and Phase 4
(history purge of unreleased field data + planning artefacts). Phase 2
shipped the reusable `docs/scripts/sync-to-docusaurus.py` tool; the OSF docs
were synced via PR branch `osf-docs-sync-phase3` and **merged + deployed to
docs.optimeas.com** (2026-06-07 — pipeline build + SFTP upload clean). The
`PUBLIC-PREP.md` tracker was closed/removed 2026-06-07; residual optional
polish (tag, GitHub topics, branch protection, issue templates) is parked in
`BACKLOG.md`.

---

## What OSF is

Open Streaming Format — a binary, block-oriented file format for time-series
measurement and process data. Designed for embedded streaming write and fast
block-wise read on servers/desktops/AI pipelines. Maintained by Optimeas GmbH;
implementations are MIT-licensed.

Two on-disk versions: **OSF4** (XML metablock, classic) and **OSF5** (JSON
metablock, simplified control byte, no trailer). Backward-compatible.

---

## Spec revisions 2026-05-04 + 2026-05-24 — what changed

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
- `string` and `binary` payloads in `bcAbsTimeStampData` are null-terminated
  in **OSF4** (writer MUST append the trailing `0x00`, reader MUST strip the
  last byte unconditionally) and **not** null-terminated in **OSF5** (writer
  MUST NOT append, reader MUST NOT strip). Version-deterministic — no
  strip-if-present heuristic. See `docs/{en,de}/osf_general.md` for the full
  rule. The rule was originally introduced as uniform-append in 2026-05-04
  and tightened to version-deterministic in 2026-05-24.

Magic header legacy identifiers documented across all four spec docs:
`OSF4`, `OSF5`, **`OCEAN_STREAM_FORMAT4`** (still emitted by deployed
devices), `OCEAN_STREAMING_FORMAT4` (older).

### Spec rev 2026-05-24 — clarifications and tightenings

Three follow-ons to the 2026-05-04 revision landed on 2026-05-24:

- **Bit 7 of the control byte is uniformly optional.** The earlier wording
  said Bit 7 "must be set" for `bcAbsTimeStampData` with string/binary; the
  new rule is type-agnostic — Bit 7 = 0 means implicit N=1 (no `uint32`
  N-prefix), Bit 7 = 1 means explicit N via `uint32` prefix. Saves four
  bytes per single-sample block.
- **Null-terminator on string/binary `bcAbsTimeStampData` is
  version-deterministic.** OSF4 keeps the trailing `0x00` (writer MUST
  append, reader MUST strip); OSF5 omits it entirely (writer MUST NOT
  append, reader MUST NOT strip). Replaces the strip-if-present
  heuristic — eliminates the ambiguity for OSF5 binary payloads that
  legitimately end in `0x00` (ASN.1 blobs, protobuf messages,
  null-terminated strings stored as binary).
- **Delphi multi-sample variable-length writer layout removed.** The
  historical Delphi-only multi-sample form for string/binary used a
  per-sample `uint32` length prefix between samples (never parsed by
  the Rust or C++ readers). The Delphi writer now auto-splits N>1
  variable-length calls into N single-sample blocks; the Delphi reader
  logs a warning and skips any non-spec multi-sample variable-length
  block it encounters.

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
│   └── (c, cpp, …)/                 — README placeholders only
├── integrations/(arrow, pytorch, tensorflow, mcp, langchain)/  — placeholders
├── examples/
│   ├── motorbike.osf                — real field sample
│   ├── steam_loco.osf + .csv        — real field sample
│   ├── weather_station.osfz         — real gzip-OSFZ field sample
│   ├── Testdata Motorbike/          — real OSFZ motorbike field recordings (daily dirs)
│   └── generated/                   — 19 reference files (17 from OSFGenerator, 2 from the Rust bcMessageEvent generator) + integrity/ and malformed/ subdirs
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
| `OSF.Log` | Central logging + progress dispatcher. `TOSFLogLevel = (llDebug, llInfo, llUser, llWarning, llError)` (verbosity-ascending; default listener filter is `llUser`). `TOSFLogEvent` carries `(Msg, Level, Sender)`. `TLoggerListener` class with four event slots (`OnAddLogMessage`, `OnStartProgress`, `OnDoProgress`, `OnEndProgress`) plus a per-listener `MinLevel`. `TOSFLog` class with `RegisterListener`/`UnregisterListener`, `Write` (two overloads), `ProgressStart`/`DoProgress`/`EndProgress`, and `IsLevelActive(Level)`; threadsafe via `TCriticalSection`, listener iteration over a snapshot so a listener can register / unregister another listener from inside its own callback. Process-wide singleton exposed as the global `Logger: TOSFLog` (init / final inside the unit). |
| `OSF.Channel` | `TOSFChannelDef` — has `SampleRate: Double`; no longer has `Scale`, `Offset`, `PhysicalUnit1..3`, `PhysicalDimension1..3` |
| `OSF.Filer` | `TOSFFile` — streaming reader/writer for OSF4 and OSF5. `WriteEquidistantBlock(...)` requires `Channel.SampleRate > 0` and a non-zero `FirstTimestampNs` to start a new segment. `WriteTimestampedSample/Block/Doubles` for timestamped channels. Version-deterministic `0x00` handling for `string`/`binary` in `bcAbsTimeStampData` per spec rev 2026-05-24: writer appends and reader strips for OSF4, both leave the payload verbatim for OSF5. Variable-length `WriteTimestampedBlock` calls with N>1 are auto-split into N single-sample blocks (the historical multi-sample per-sample-uint32-length-prefix layout was removed). Adds an optional read-side `ChannelFilter: TArray<string>` (skips blocks of channels not in the list — info blocks always pass through). A data block whose length field reads `0` is logged, counted (`BlocksZeroLengthSkipped`) and skipped — on the normal read path and on the channel-filter path alike — instead of raising and aborting the whole file (OSF-UP3, [DECISIONS §25](DECISIONS.md#25-zero-length-data-blocks)); an unrecognised length-field width is a separate guard that stops the scan, so a corrupt width is not misreported as a writer artefact. Transparent **OSFZ (gzip) decompression**: `OpenForRead` peeks the `1F 8B` magic and wraps the stream in `TZDecompressionStream`. OSF4 XML metablock is parsed via **OmniXML** (`GetDOMVendor(sOmniXmlVendor)`) so reads no longer need MSXML installed. |
| `OSF.Data.Channels` | Typed in-memory channels. **`TOSFEquidistantDataChannel.Segments: TList<TOSFChannelSegment>`** maps the flat `Values` list onto absolute time — every `bcStartData` opens a new segment with `(StartTimestampNs, StartIndex, SampleCount)`. |
| `OSF.Data.Manager` | `TOSFDataManager.LoadFromFile/Stream` — high-level read; populates typed channels. Pass-through `ChannelFilter: TArray<string>` forwarded to the internal `TOSFFile`; excluded channels get no `TOSFDataChannel` at all. |
| `OSF.Export` | `TOSFExporter` abstract base (`ExcludeEmptyChannels`, `AbsoluteTimestamps`) |
| `OSF.Export.CSV` | `TOSFCSVExporter` — per-channel XY CSV; `(DecimalSeparator, ColumnSeparator, Encoding, TimestampFormat)` |
| `OSF.Export.CSV.Unified` | `TOSFUnifiedCSVExporter` — single shared timeline: one timestamp column + one value column per channel, empty cell where a channel has no sample. `TUnifiedCSVTimestampFormat` (datetime / seconds / iso8601 / nanoseconds). O(N) cursor-walk over the merged, de-duplicated timeline. |
| `OSF.Meta.Cache` | `TOSFMetaCache` — sidecar `.json` per OSF/OSFZ file (source size + mtime validity stamp, global and per-channel first/last timestamps and sample counts; `CachePathFor` maps `foo.osf`/`foo.osfz` → `foo.json`). `TOSFMetaCacheBuilder` scans a file via `TOSFFile` discarding sample payloads. |
| `OSF.Merger` | `TOSFMerger` — scans a directory (or explicit `FileList`) for OSF/OSFZ files overlapping a UTC interval, merges selected channels into a single OSF4/OSF5 output. Cache-driven file selection, `osSkip`/`osOverwrite` overlap strategy, per-sample interval clipping. Output is emitted as `bcAbsTimeStampData` (equidistant inputs expanded to per-sample timestamps). |
| `OSF.Export.HDF5` | `TOSFHDF5Exporter` — exports a `TOSFDataManager` as an HDF5 file: one chunked / shuffled / deflated 1-D dataset of `{int64 timestamp_ns; value}` compound records per channel, the channel name split on the namespace separator into HDF5 groups, file and channel metadata as root/dataset attributes. Covers `bool`, every signed/unsigned integer width, `float`, `double`, `gpslocation` (a lat/lon/alt sub-compound) and `string` (variable-length UTF-8); `binary` is skipped. Configurable `ChunkSize`, `DeflateLevel`, `UseShuffle`, `NamespaceSep`, `LibraryDir`. Windows-only. |

| `OSF.Version` | osftool version single-source-of-truth: `OSFTOOL_VERSION` constant + `GetVersionString` / `GetFullVersionString` (build timestamp taken from the executable's own file date). |
| `Console.ProgressBar` (in `src/console/`) | OSF-agnostic CLI in-place progress bar. `TConsoleProgressBar` with `Start(MaxValue, Msg)` / `Update(Value, Msg)` / `Finish(Msg)`. Auto-picks between an in-place ANSI bar (interactive TTY, Windows VT processing enabled) and throttled plain "Progress: N% (i/m)" lines (redirected stdout). Plus `ShortenPath` helper for embedding long file paths in progress messages. Wired up by osftool through `Cmd.Base`'s default listener callbacks. |

**HDF5 DLL binding in `implementations/delphi/src/hdf5/`:** `Hdf5.Types`,
`Hdf5.Api` and `Hdf5.Wrapper` form a reusable, OSF-agnostic Delphi binding
to the HDF5 C library — `cdecl` function-pointer types, a six-stage
`hdf5.dll` resolver, the mandatory `H5open`-first initialisation with
`_g`-global readout, and RAII handle wrappers. `OSF.Export.HDF5` builds on
them. The runtime itself is never committed; `dataformats/hdf5/lib/install-hdf5.ps1`
fetches HDF5 1.14.4-3 from the HDF Group.

**`OSFCompileCheck.dpr`** at the implementation root is a no-form `uses`-only
program covering every `OSF.*` library unit in `src/` plus
`Console.ProgressBar` in `src/console/`, except the Windows-only
`OSF.Export.HDF5` and its `src/hdf5/` wrapper cluster (those are exercised
by the osftool project that links them explicitly). Running
`dcc32 -B -Q OSFCompileCheck.dpr` from `implementations/delphi/` gives a
clean compile signal after refactors.

---

## Demos

| Demo | Purpose |
|---|---|
| `demos/osfviewer/` | Loads an OSF file, lists channels, renders selected channel as a TeeChart. Uses TeeChart units — only builds inside the IDE (search path defined in the .dproj). |
| `demos/osfgenerator/` | Writes the 17-file reference set (8 OSF4 + 9 OSF5) into `examples/generated/`. Form has output-dir picker, OSF4/OSF5 toggles, samples-per-channel spinedit, log memo. Console companion `OSFGeneratorCLI.dpr` (`dcc32 -B -Q OSFGeneratorCLI.dpr`) generates the same set non-interactively for CI / regen sessions — `OSFGeneratorCLI [output-dir] [samples-per-channel]`, defaults match the GUI (samples = 100). |
| `demos/osfcsvexport/` | Pipeline demo: `TOSFFile` → `TOSFDataManager` → `TOSFCSVExporter`. Exposes the four exporter options + a debug toggle. |
| `demos/osfmerger/` | VCL front-end for `TOSFMerger` (`OsfMerger.exe`, Win64). PageControl with directory-scan / explicit-file-list tabs, channel-filter memo, overlap + output-format options, scan/merge/save actions, found-files and merge-result list views, debug toggle + log memo. |

`osfviewer` / `osfgenerator` / `osfcsvexport` compile with `dcc32`
(`OSFViewer` only via IDE because of TeeChart); `osfmerger` compiles
Win64 with `dcc64`. Delphi 12 / RAD Studio 23.0.

### Delphi integrity profile — level `crc` (2026-07-08)

Delphi implements the OSF5 integrity profile at level `crc` plus the two audit
fixes (`AUDIT_INTEGRITY_O1.md` §4.2). New unit **`OSF.CRC32C`** (pure-Pascal
table-based CRC-32/ISCSI; check value `0xE3069283`, byte-identical to Rust/C++;
`CRC32CSelfTest`). In `OSF.Filer`:

- **Fix A (tokenizer):** `ParseHeaderTokens` parses `crc32c` (8 upper hex,
  strict) and `ed25519` (16 lower hex, only after `crc32c`) magic-header tokens;
  an unknown key rejects the file (`unknown header token '<key>'`); tokens are
  rejected on OSF4 identifiers. Profile + counters exposed via `TOSFFile`
  properties (`IntegrityProfile`, `BlocksCRCFailed`, `BlocksSignatureSkipped`,
  `BlocksUnknownTypeSkipped`, `VerificationStatus`).
- **Metablock CRC** verified over the raw bytes before parse; **frame CRC**
  verified over the whole frame and stripped before the typed decode
  (fail-closed, effective len = LEN − 4), mismatch skips the block + counts +
  logs and reads on.
- **Fix C:** an unknown control byte is skipped via the length field and the
  scan continues (own counter/log); `FTruncationSeen` only on real truncation.
  Block reads return a tri-state (`boBlock`/`boSkip`/`boStop`); `ReadNextBlock`
  loops.
- **Signed coexistence:** signature blocks (channel `0xFFFE`, u32 length field,
  control 9) are skipped + counted; `VerificationStatus` reports
  `signature_unverifiable`.
- **Writer:** `TOSFFile.IntegrityProfile := ipCrc32c` emits the `crc32c` token
  (metablock CRC) and a per-block frame CRC (one block per call, no chunk
  reduction needed). Ed25519 / OSF4-integrity rejected.

**osftool:** `verify` reports the integrity status vocabulary + counters
(exit 4 on `invalid`); `info` shows `Integrity: none|crc32c|ed25519`.
**OSFCrcRefGen** (`demos/osfgenerator/`) writes Delphi CRC reference files into
`examples/generated/integrity/` (`*_crc_delphi.osf`). **Cross-validated
bidirectionally with Rust** (Rust reads Delphi files and vice versa, 0 CRC
failures, byte-identical CRCs).

**Tests — DUnitX suite** (`implementations/delphi/tests/OSFTests.dpr`, **37
tests**, measured 2026-07-29, green under **dcc32 and dcc64**):
`Test.OSF.CRC32C` (vectors incl.
RFC 3720), `Test.OSF.Filer.Header` (tokenizer matrix + strict-space cases),
`Test.OSF.Filer.Integrity` (metablock/frame CRC good+corrupt per block type,
Fix C control-byte-9 skip, `VerificationStatus`, write/read round-trip,
writer-overflow boundary, cross-validation reading the Rust + Delphi
`integrity/*.osf` reference files, and the manifest anomaly counts),
`Test.OSF.Filer.ZeroLengthBlock` (3 tests — OSF-UP3: skip + count on the normal
read path and on the channel-filter path), and
`Test.OSF.Filer.MessageEvent` (8 tests — OSF-UP4: manager decode of the legacy
channel, legacy-vs-equivalent equality, filer-level timestamp + payload,
`N = 0` → empty value, the two unspecified shapes plus `bcStatusEvent` skipped
and counted, meta cache agreeing with the manager, a `binary` payload ending in
`0x00` surviving intact, and malformed frames stopping the scan without
delivering a sample). Build with the DUnitX source **and
`..\src`** on the unit path, the DUnitX source on the include path, and `.dcu`
output routed to a writable dir that must exist:

```powershell
# from implementations\delphi\tests
$DX = "C:\Program Files (x86)\Embarcadero\Studio\23.0\source\DUnitX"
New-Item -ItemType Directory -Force dcu32 | Out-Null
dcc32 -B -Q -U"..\src;$DX" -I"$DX" -NU"dcu32" OSFTests.dpr   # (dcc64 / dcu64 likewise)
.\OSFTests.exe   # exit 0 = all pass
```

`-U` **replaces** the unit search path, so `..\src` is mandatory: the test units
use `OSF.Data.Channels` / `OSF.Data.Manager` without listing them in the `.dpr`,
and omitting it fails with `F2613 Unit 'OSF.Data.Channels' nicht gefunden`. The
`-NU` directory is not created by the compiler — create it first.

Two spec-conformance fixes landed with RED-first tests: **Fix 1** — strict
single-space magic-header grammar (trailing/double space now rejected, matching
Rust); **Fix 2** — writer guard against a u16 length-field overflow when the
frame CRC (+4) is counted (raises instead of silent wrap). Also verified via
osftool negative cases (metablock/numeric/string byte flips, unknown token,
control-byte-9 skip) with documented exit codes; `OSFCompileCheck` + osftool
compile clean.

---

### Java integrity profile — level `crc` (2026-07-09)

`osf-java` implements the OSF5 integrity profile at level `crc` (scope: crc
only; no signing). CRC32C is `java.util.zip.CRC32C` (JDK-native, no dependency;
check value `0xE3069283`, byte-identical to Rust/C++/Delphi).

- **Reader.** `MagicHeaderParser` whitespace-tokenizes the header: `crc32c`
  (8 upper hex, strict) + `ed25519` (16 lower hex, syntactic, only after
  `crc32c`); an unknown key throws the dedicated
  `OsfException.UnknownHeaderToken` (`unknown header token '<key>'`, no
  NumberFormat passthrough); tokens on an OSF4 identifier are rejected.
  `MagicHeader` carries `integrity` + `metablockCrc`. `DataManager` verifies the
  metablock CRC over the raw bytes before parse
  (`OsfException.MetablockCrcMismatch`). Under an active profile `BlockReader`
  verifies + strips the 4-byte frame CRC before the typed parse (fail-closed,
  effective payload = LEN − 4); a mismatch skips the block + bumps
  `ReaderStats.blocksCrcFailed`. Signature blocks (channel `0xFFFE`, control 9,
  u32 length) are skipped + counted (`blocksSignatureSkipped`); the permissive
  `ChannelAssembler` ignores the reserved index so signed files stay readable.
  `ReaderStats` exposes `integrity` + `verificationStatus()`
  (`none`/`crc_valid`/`invalid`/`signature_unverifiable`). Transparent OSFZ read
  is unchanged (a gzip-wrapped crc file decompresses then verifies).
- **Writer.** `setIntegrity(IntegrityProfile.CRC32C)` on **both** `BlockWriter`
  and `StreamingWriter` (default off) emits the `crc32c` token + metablock CRC
  (shared `internal.Integrity`) and a per-block frame CRC
  (`BlockEncoder.applyFrameCrc`); chunk budgets drop by 4. StreamingWriter
  applies the CRC in its single per-block `writeBlock` gate, so `force()`/fsync
  behaviour is unchanged. Ed25519 (signing) is rejected.
- **Numeric full-consume** leftover bytes are logged via `System.Logger`
  (softened to warning-only to match the Rust/C++ readers, which do not reject —
  the frame CRC is the integrity gate; noted as an interpretation decision).

**Tests** (JUnit 5 + AssertJ, **21 new**, module total **226** at the time of
this round, `mvn test` green): `MagicHeaderIntegrityTest` (tokenizer matrix),
`IntegrityReaderTest`
(4 reference files + metablock/numeric/string byte flips → `MetablockCrcMismatch`
/ `blocksCrcFailed`, signature-block skip, gzip), `WriterIntegrityTest`
(both-writer round-trip vs plain, byte-identical crc output), plus a
control-byte-9-profile-less skip case. Frame-CRC byte-identity to Rust is
transitive: the reader reads the two Rust reference files with 0 CRC failures,
and the writer's output reads back clean.

**Current module total: 265** (measured 2026-07-29, after the channeltype,
manifest, OSF-UP3 and OSF-UP4 rounds). The `226` above is the figure of its own
round and was never refreshed as the suite grew — it had drifted to 232 before
OSF-UP3 added 12, and OSF-UP4 added a further 21. Run it with

```powershell
mvn -f implementations/java/pom.xml -pl :osf-java test
```

The module selector must be **group-qualified** (`:osf-java`): a bare
`-pl osf-java` is taken as a relative path and fails with *Could not find the
selected project in the reactor*.

Reader statistics gained `ReaderStats.blocksSkippedZeroLength()` for OSF-UP3 —
see the *Zero-length data blocks* section below — and, for OSF-UP4,
`blocksSkippedStatusEvent()` **plus the reserved- and deprecated-skip counters
Java never had at all** (`blocksSkippedReservedType()` /
`blocksSkippedDeprecatedType()`): before that round `ReaderStats` carried a
decoded-block count and a truncation flag and no skip buckets, so a skipped
block left no trace. Java's stats surface is still thinner than Rust's and
C++'s — it has no `unsupported` bucket — see `BACKLOG.md`.

The four `integrity/` reference files are now part of the shared
`examples/reference_manifest.json` conformance contract (sub-path keys, optional
`integrity: "crc32c"`), and the manifest-driven conformance tests of all four
implementations — Java (`ConformanceManifestTest`), Rust
(`conformance_manifest_test.rs`), C++ (`test_conformance_manifest`), Delphi
(`ConformsToReferenceManifest`) — load them off that single file list and assert
the integrity profile plus zero frame-CRC failures. Per-language hard-coded
reference-file lists were removed. Open question surfaced by the retrofit: the
Rust-written `integrity/osf5_crc_equidistant.osf` omits the optional metablock
`timeincrement`, so the Delphi reader (which classifies equidistance from that
field rather than the `bcStartData` control byte, contrary to `osf_general.md`)
reads its channels as `timestamped` — a reader divergence, not fixed here.

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
| `merge` | Merge OSF files from a directory into one OSF — positionals `<rootdir> <outputfile> [channel ...]`; `--start`/`--end` ISO-8601 interval bounds (default `1970-01-01`..now), `--osf4`, `--overwrite`, `--no-cache`; output-mode flags `-q`/`--quiet`, `-v`/`--verbose`, `--json` (JSON-Lines event stream), `--log <path>`. Default run shows a live progress bar via the `TOSFLog` listener registered in `Cmd.Base` (`Console.ProgressBar` under the hood). Wraps `TOSFMerger` |
| `export` | Export channels — `--format csv` (per-channel XY), `unified-csv` (single shared timeline) or `hdf5` (Windows; one compound dataset per channel via `TOSFHDF5Exporter`); `--timestamp-format`, `--decimal-sep`, `--encoding`, `--start/--end`, plus the HDF5 options `--chunk-size`, `--deflate-level`, `--no-shuffle`, `--namespace-sep`, `--hdf5-lib-dir` |
| `info` | File metadata + global time range (cache-backed when a valid sidecar exists) |
| `channels` | List channels with optional `--filter` wildcard (`System.Masks`) |
| `stat` | Per-channel min/max/mean/stddev via single-pass Welford; `--start/--end` interval filter |
| `cache` | `build` / `rebuild` / `clean` / `status` of `.json` sidecars under a root dir |
| `config` | `show` / `set` / `reset` settings; `install-path` / `uninstall-path` add/remove the exe dir from the user PATH (HKCU registry on Windows, shell-snippet print on POSIX) |
| `convert` | OSF4 ↔ OSF5 round-trip via `TOSFMerger` |
| `verify` | Block-level integrity check (channel-index coverage, timestamp monotonicity, truncation) plus the read-side counters: CRC-failed, signature, unknown-type and **`Zero-length skips`** (`zero_length_skipped_count` under `--json`). A nonzero zero-length count raises a warning naming OSF-UP3; warnings keep exit 0, `--strict` escalates to 4 |

Shared infrastructure: `IOsfCommand` + `TBaseCommand` (argument parsing,
stdout/stderr split, global `--json` / `--quiet` / `--verbose`).
`TOsfToolConfig` persists settings at `%APPDATA%\osftool\config.json`
(Windows) or `~/.config/osftool/config.json` (POSIX). Uniform exit codes:
0 ok, 1 bad args, 2 not found, 3 io error, 4 format error. Compiles
clean with `dcc64`.

The top-level dispatcher also handles `--version` / `-V` (plus `--short`),
sourced from the `OSF.Version` unit (osftool 1.1.0). The `merge` verb
renders progress through the `TOSFLog` listener pattern that `Cmd.Base` sets up for every command — a
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
| `block` | `Block { channel_index, kind }`, `BlockKind` (StartData / ContinuedData / AbsTimestampData / ContinuedRelStampData / Skipped), `NumericPayload` / `TimestampedPayload` / `RelTimestampedPayload`, `GpsLocation`, `SkipReason` (now `#[non_exhaustive]`, incl. `ZeroLengthBlock`), control-byte decoder |
| `reader` | `BlockReader<R: Read>` — Iterator over `Result<Block, OsfError>`. Builder `with_capture_skipped_payload(bool)` (default off, no allocation) and `with_file_size(u64)`. Best-effort on truncation, hard error on unknown channel index, silent consume of optional `0xFFFF` info block plus 40-byte magic trailer |
| `stats` | `ReaderStats` with file/section sizes, elapsed, channels and per-reason block counters, plus `per_channel: HashMap<u16, ChannelStats>` (segments, samples_total, time_range_ns); `Display` impls for both |
| `data_channel` | `Channel` enum (`Equidistant` / `Timestamped` / `Variable`), per-variant typed structs, `Segment`, `ChannelMeta`, `NumericValues`; `samples_with_time()` iterators yielding `Sample<NumericValueRef<'_>>` / `Sample<VariableValueRef<'_>>`; `as_doubles_flat` etc. helpers |
| `manager` | `DataManager` — `load_from_file(path)` / `load_from_reader(R)` build the typed channel list, expose `channel(name)` (mandatory per DECISIONS §10) and `channel_by_index(u16)` (optional). Internal `build_channels` runs the per-channel builder state machine (Pending → Equidistant or Timestamped on first typed block; Variable upfront for string/binary). |
| `binary_write` | Crate-private little-endian write helpers (symmetric to `byteorder::ReadBytesExt`). Variable-length payloads (`string`, `binary`) are written via `Write::write_all` directly — no terminator appended (OSF5 writer per spec rev 2026-05-24). |
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
  handle the trailing `0x00` version-deterministically per spec rev
  2026-05-24 — OSF4 strips the last byte unconditionally, OSF5 leaves
  the payload verbatim. Multi-sample variable-length blocks try
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
- Variable (string / binary) blocks: one sample per block per spec; no
  trailing `0x00` byte (OSF5 writer per spec rev 2026-05-24).
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

**Tests:** 122 unit tests across `header.rs`, `meta.rs`, `meta_json.rs`,
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

**Integrity profile — level `crc` (2026-07-08).** `osf-core` implements the
OSF5 integrity profile at level `crc` (module `integrity`, `crc` crate for
CRC32C/Castagnoli). The magic-header parser is a strict must-understand
tokenizer (`crc32c:<8 upper hex>`, `ed25519:<16 lower hex>`, only after
`crc32c`); unknown keys → `OsfError::UnknownHeaderToken`. `MagicHeader` carries
`integrity` + `metablock_crc`. The metablock CRC is verified before parse on
both read paths (`OsfError::MetablockCrcMismatch`). `BlockReader::with_integrity`
verifies each block's frame CRC over the whole frame and strips it before the
typed parse (fail-closed); a mismatch skips the block and bumps
`ReaderStats.blocks_crc_failed`. Signature blocks (channel `0xFFFE`, control 9)
are skipped/counted so signed files stay readable; `ReaderStats.integrity` +
`verification_status()` (`none`/`crc_valid`/`invalid`/`signature_unverifiable`)
report the state. `WriterBuilder::with_integrity(IntegrityProfile::Crc32c)`
emits the token + frame CRCs (payload chunking reserves 4 bytes). Reference
files: `examples/generated/integrity/osf5_crc_{equidistant,variable}.osf` (Rust
`cargo run --example gen_crc_refs`). Signing (level `signed`) is not
implemented. **139 → 147 lib tests + `tests/integrity_test.rs` (6)**.

**Suite total (measured 2026-07-29): `cargo test` = 189 passed, 2 ignored** —
the two `#[ignore]`-gated performance smokes above. Added by OSF-UP4:
`bcMessageEvent` decoding into the existing timestamped representation
(`parse_message_event`), `ReaderStats.blocks_skipped_status_event`, and
`tests/writer_message_event_audit_test.rs` (4 tests). Added by OSF-UP3:
`ReaderStats.blocks_skipped_zero_length` + `SkipReason::ZeroLengthBlock` (the
`blocks_total` aggregation had to gain the new counter as a term; it omitted it
for three commits mid-branch — see `17771ee`), the
`examples/gen_malformed_refs.rs` corpus generator, and
`tests/writer_zero_length_audit_test.rs` (7 tests). See the *Zero-length data
blocks* section below.

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

**Integrity profile — level `crc` (2026-07-08).** The bindings surface the
integrity profile: `stats.integrity`, `stats.blocks_crc_failed`,
`stats.blocks_signature_skipped`, `stats.verification_status`, plus
`WriterBuilder.with_integrity("crc32c")` and `osf.save(mgr, path,
integrity="crc32c")`. Type stubs updated; `tests/test_integrity.py` (5 run +
1 skipped — no signed reference file yet). Verified locally via `maturin
develop` (18 passed, 1 skipped).

**OSF-UP3 (2026-07-28).** `ReaderStats` gained `blocks_skipped_zero_length`
(getter + type stub), inherited from the Rust core; `tests/test_basic.py` covers
the malformed corpus file.

**OSF-UP4 (2026-07-28).** `bcMessageEvent` decoding is inherited from the Rust
core — no binding change was needed for the read itself. `ReaderStats` gained
three more getters + type stubs (`blocks_skipped_status_event`,
`blocks_skipped_deprecated_type`, `blocks_skipped_reserved_type`), and
`tests/test_basic.py` plus `tests/test_writer_message_event_audit.py` cover the
corpus pair and the writer audit. **Suite total (measured 2026-07-29): 23
passed, 1 skipped.** The binding still surfaces only a subset of `osf-core`'s
counters — `blocks_skipped_unsupported` is the one that remains absent — see
BACKLOG, *Reader counters are not reachable from every high-level API*.

**Pending:** pandas `DataFrame` convenience (Session 7b).

---

## C++ implementation — current state

Phase 1 (skeleton) completed 2026-05-08; Phase 2 (magic-header parser) completed 2026-05-10; Phase 3 (OSF5 JSON metablock parser) completed 2026-05-19; Phase 4 (OSF4 XML metablock parser) completed 2026-05-23; Phase 5 (block-stream reader) completed 2026-05-23; Phase 6 (typed DataManager) completed 2026-05-23. Reader updated for the version-deterministic null-terminator rule on 2026-05-24. Phase 7a (private block-encoder library) completed 2026-05-26. Phase 7b (`StreamingWriter` — embedded streaming OSF5 writer) completed 2026-05-31. Phase 7c (`BlockWriter` — analyst-style OSF5 writer) completed 2026-06-02. Phase 7d (`StaleValueGuard` — optional freshness layer) completed 2026-06-03. Phase 8 (transparent OSFZ decompression on read) completed 2026-06-03. Phase 9 (throwing convenience layer) completed 2026-06-03. Phase 10 (CI integration) completed 2026-06-03. **Phase 11 (C ABI wrapper) completed 2026-06-04 — the §20 Implementation Order is now complete (phases 1–11).** Per [DECISIONS §20](DECISIONS.md#20-c-implementation-architecture) + [§23](DECISIONS.md#23-c-abi-osf-c).
Standalone C++17 implementation, parallel to the Rust core — not a port from C, not a wrapper around the Rust crate. Foundation API, magic-header surface, both OSF4 + OSF5 metablock parsers, the block-stream reader (with `ReaderStats`), the typed `DataManager`, the OSF5 block-encoder primitives, **both** user-facing writer classes (`StreamingWriter` + `BlockWriter`), the optional `StaleValueGuard` freshness layer, transparent OSFZ (gzip/zlib) decompression on read, the opt-in throwing convenience layer, and the `osf-c` C ABI shared library are all in place — and CI builds + tests them on Linux/macOS/Windows. **All eleven phases are done;** remaining C++ work is incremental (BACKLOG), not a numbered phase.

### C++ integrity profile — level `crc` (2026-07-09)

`osf-cpp` implements the OSF5 integrity profile at level `crc` (package
version **0.2.0**). Dependency-free vendored CRC32C (`src/crc32c.cpp`,
slicing-by-8 CRC-32/ISCSI; canonical check value `0xE3069283`,
byte-identical to Rust/Delphi). Signing (level `signed`) is out of scope.

- **Reader.** A strict must-understand magic-header tokenizer parses `crc32c`
  (8 upper hex) and `ed25519` (16 lower hex, only after `crc32c`); an unknown
  key is `Error::Code::UnknownHeaderToken` (distinct from a malformed-length
  parse error), and a token after an `OSF4` identifier is `InvalidMagicHeader`
  (grammar-level). The metablock CRC is verified over the raw bytes before the
  parse (`Error::Code::MetablockCrcMismatch`). Under an active profile each
  block's frame CRC32C is verified fail-closed (effective payload = `LEN − 4`,
  carved off before the typed parse); a mismatch skips the block and bumps
  `ReaderStats::blocksCrcFailed`. Signature blocks (reserved channel `0xFFFE`,
  control byte 9, u32 length) are skipped and counted
  (`blocksSignatureSkipped`) so a signed file stays readable — end-to-end
  through `DataManager`. `ReaderStats::integrity` and `verificationStatus()`
  (`none` / `crc_valid` / `invalid` / `signature_unverifiable`) report status.
  `DecompressingIStream` is unchanged: a gzip-wrapped crc file decompresses and
  then verifies transparently.
- **Writer.** `setIntegrity(IntegrityProfile::Crc32c)` on **both**
  `StreamingWriter` and `BlockWriter` (default off) emits the `crc32c` token,
  the metablock CRC, and a per-block frame CRC — implemented once in the shared
  `writercommon` path (frame CRC appended as the last 4 bytes; chunk budgets
  reduced by 4). `Ed25519` is rejected. `StreamingWriter` fsync/OSFZ behaviour
  is unchanged.
- **C ABI (`osf-c`).** Additive: `OSF_ERR_UNKNOWN_HEADER_TOKEN` /
  `OSF_ERR_METABLOCK_CRC_MISMATCH` status codes, an `osf_integrity_profile`
  enum, and `osf_manager_integrity` / `_blocks_crc_failed` /
  `_blocks_signature_skipped` / `_verification_status` accessors.
- **Tests.** `tests/unit/test_crc32c.cpp` (RFC 3720 vectors), header-tokenizer
  unit tests, and `tests/integration/test_integrity_examples.cpp` (10 cases:
  both-writer round-trips, the four cross-implementation reference files under
  `examples/generated/integrity/` read clean, the negative suite — metablock /
  numeric / string byte flips, unknown token, OSF4+token, signature block —
  and a gzip-wrapped crc file). Byte-level cross-validation with Rust is
  transitive: the CRC primitive matches and the C++ reader reads the Rust
  reference files with zero failures. **345/345 ctest green** at that round
  (**354/354** today — OSF-UP3 added one, OSF-UP4 eight).

**Library targets:**

- `osf::osf` — static library (default; shared if `BUILD_SHARED_LIBS=ON`). Internal CMake name is `osf_core`; `OUTPUT_NAME osf` keeps the produced file as `libosf.a` / `osf.lib`.
- `osf::headers` — INTERFACE target carrying the public include paths plus the vendored `tl::expected` and `nlohmann/json` directories (both attached SYSTEM so upstream warnings stay silent).

**Tree at `implementations/cpp/`:**

| File | Purpose |
|---|---|
| `CMakeLists.txt` | Top-level config: project, C++17 baseline, seven build options, both library targets, `add_subdirectory(tests)`/`examples` gated by `OSF_BUILD_TESTS`/`OSF_BUILD_EXAMPLES` (+ opt-in `osf-docs` Doxygen target under `OSF_BUILD_DOCS`); `osf_core` lists `error.cpp` + `header.cpp` + `metablock.cpp` + `types.cpp` |
| `cmake/CompilerWarnings.cmake` | `osf_set_warnings(target)` — MSVC `/W4 /permissive- /wd4100`; GCC/Clang `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion` |
| `cmake/version.h.in` | Template; `configure_file` emits `${BINARY_DIR}/generated/osf/version.h` with `OSF_VERSION_MAJOR/MINOR/PATCH` and `osf::version()` |
| `include/osf/osf.h` | Umbrella header (re-exports `error.h` + `header.h` + `metablock.h` + `types.h` + `version.h`) |
| `include/osf/error.h` | `osf::Error` (`Code` enum: Unknown / InvalidArgument / IoError / ParseError / NotFound / InvalidMagicHeader / UnsupportedVersion / MagicHeaderTooLong / InvalidMetablock / RemovedInSpec / JsonParseError; plus `std::string message`); `osf::Result<T>` as `tl::expected<T, Error>`; `errorCategoryName(Code)` declaration |
| `include/osf/header.h` | Magic-header API: `osf::OsfVersion` (Osf4/Osf5 enum), `osf::MagicHeader` struct (version + metablockLen, friend equality), three `parseMagicHeader` overloads (`std::istream&`, `std::uint8_t const*` + size, `std::filesystem::path`), `osf::MAX_MAGIC_HEADER_LEN = 128` |
| `include/osf/types.h` | Core OSF type enumerations (spec rev 2026-05-04): `DataType` (Bool / Int8..Int64 / UInt8..UInt64 / Float / Double / String / Binary / ByteArray / GpsLocation / Unsupported), `ChannelType` (Scalar / Equidistant / Timestamped / Unsupported), `SpectrumType` (Amplitude / RealImag / AmpPhaseRad / AmpPhaseDeg); plus `parseDataType` / `parseChannelType` (Result-returning) and `parseSpectrumType` (noexcept) |
| `include/osf/metablock.h` | OSF metablock data model: `FileInfo`, `Channel`, `Info`, `MetaBlock` structs; `parseMetablockJson` (OSF5) and `parseMetablockXml` (OSF4), each in two overloads (`std::uint8_t const*` + size; `std::string_view`) — both populate the same `MetaBlock` data model |
| `include/osf/block.h` | OSF block model: `Block` struct, `BlockKind` as `std::variant<StartData, ContinuedData, AbsTimestampData, ContinuedRelStampData, Skipped>`, payload sum types, `GpsLocation`, `SkipReason`, `ControlByte` / `ControlKind`, `decodeControlByte`. Constants `TRAILER_CHANNEL_INDEX = 0xFFFF` and `MAGIC_TRAILER_LEN = 40`. |
| `include/osf/stats.h` | Reader telemetry: `ReaderStats` (byte/block counters incl. `blocksSkippedZeroLength`, channel counters, `elapsed`, `trailerSeen`, `compressed`, `compressionFormat`, `perChannel`) and `ChannelStats` (name, blocks/skipped/samples/bytes/segments, `timeRangeNs`). `formatBytes`, `formatDuration`, `compressionFormatName`. `operator<<` overloads format the structs. |
| `include/osf/reader.h` | Block-stream reader: `BlockReader` class — fluent setters `withCaptureSkippedPayload(bool)` and `withFileSize(u64)`; primitive `next()`; range-based-for support; `stats()`, `blocksTruncated()`, `trailerSeen()`, `fileSizeBytes()`. Best-effort on truncation, hard error on unknown channel index, forward-compat `Skipped` records. |
| `include/osf/datachannel.h` | Typed in-memory channel model: `DataChannel` as `std::variant<EquidistantChannel, TimestampedChannel, VariableChannel>`. `EquidistantChannel` + `TimestampedChannel` + `VariableChannel`; `Segment` (fields: `startTimestampNs`, `sampleRateHz`, `startIndex`, `sampleCount`); `ChannelMeta`; `NumericValues` variant; flat-access helpers (`asDoublesFlat`, …, `asGpsFlat`); common free-function accessors (`channelIndex`, `channelName`, `channelSampleCount`, …). |
| `include/osf/manager.h` | High-level reader: `DataManager` class — static `loadFromFile(path)` and `loadFromStream(istream&)`; `channel(name)` and `channelByIndex(u16)` lookups; `channels()`; `meta` + `stats` fields. **Transparent OSFZ decompression** wraps the input before the magic-header parse. |
| `include/osf/compression.h` + `src/compression.cpp` | **Phase 8** transparent OSFZ decompression on read. `osf::DecompressingIStream` + `detectCompression(std::istream&)`. zlib is a PRIVATE `osf_core` dependency provisioned via `OSF_USE_SYSTEM_ZLIB` (default FetchContent zlib 1.3.2; `ON` → `find_package(ZLIB)`). |
| `include/osf/throwing.h` (header-only) | **Phase 9** opt-in throwing convenience layer. `osf::Exception : std::runtime_error`. `osf::throwing::unwrap(Result<T>)`. Free `osf::throwing::load(path)` / `load(istream&)` → `DataManager` and `writeToFile(mgr, path)` / `writeTo(mgr, ostream&)` → `void` (OSF5). **Not** in the `osf.h` umbrella — opt-in by design. |
| `include/osf/capi.h` + `src/capi.cpp` | **Phase 11** C ABI wrapper, built into the separate shared library `osf-c` only when `OSF_BUILD_C_API=ON` (DECISIONS §23). Pure-C99 `extern "C"` surface: opaque `osf_manager` + borrowed `osf_channel` handles; `osf_status` codes; thread-local `osf_last_error_message()`; read path + round-trip `osf_write_to_file`. Every entry point `try/catch`-wrapped; no C++ exception crosses the ABI. |
| `src/error.cpp` | `errorCategoryName` implementation; covers all eighteen `Error::Code` enumerators |
| `src/header.cpp` | Magic-header parser implementation. Byte-by-byte read via `istream::get()`, anonymous-namespace helpers for line read / identifier mapping / `from_chars`-based length parse, CRLF tolerance |
| `src/types.cpp` | `parseDataType` / `parseChannelType` / `parseSpectrumType` implementations. If-chain over wire spellings; `bytearray` normalises to `Binary`; removed datatypes (`gpsdata` / `pair` / `triple` / `candata`) reject with `Error::Code::RemovedInSpec` and a replacement-hint message |
| `src/metablock.cpp` | `parseMetablockJson` implementation over vendored `nlohmann::json`. `allow_exceptions=false` parse keeps the core API exception-free; anonymous-namespace helpers factor out per-field work; deprecated channel fields tolerated silently; `created_at_*` short-spelling fallback accepted on read |
| `src/metablock_xml.cpp` | `parseMetablockXml` implementation over vendored `pugi::xml_document`. DOM-walking parser, not event-driven. pugixml is configured with `parse_default` plus `encoding_utf8` hint. Anonymous-namespace helpers keep per-attribute work tight; deprecated channel fields tolerated silently; `created_at_*` short-spelling fallback accepted on read. The root element must be `<optimeas>`; unknown children and attributes are ignored (forward-compat). The `<channels count="N">` attribute is informational. |
| `src/block.cpp` | `numericPayloadLen` / `timestampedPayloadLen` / `relTimestampedPayloadLen` and their empty variants via `std::visit` over the variant; `decodeControlByte` switch over the 9 documented values plus the multi-sample-bit extraction |
| `src/stats.cpp` | `ChannelStats::observeTimestamp`, `formatBytes` (binary KB/MB/GB thresholds), `formatDuration` (sub-second → ms, otherwise s), `compressionFormatName`, `operator<<` for both stats structs |
| `src/reader.cpp` | `BlockReader` implementation. Byte-by-byte little-endian decoders; `PayloadCursor` walks an in-memory payload returning `std::optional<T>` on overflow; per-control-kind typed parsers; `recordSkip` / `skipBlock` for the forward-compat skip path with capture-or-drain selector; `consumeTrailer` for the 0xFFFF info block + 40-byte magic trailer. `BlockReader::Iterator` is a single-pass input iterator. |
| `src/datachannel.cpp` | `numericValuesLen` / empty / dataType / emptyFor via `std::visit`; `EquidistantChannel::samplesVector` walks the segments and computes per-sample timestamps; `TimestampedChannel::samplesVector` zips parallel vectors; `VariableChannel::samplesVector` plus `asStrings` / `asBinaries`. Twelve `OSF_DEFINE_FLAT_ACCESSORS` macro expansions produce `as*Flat` overloads with the `DataTypeMismatch` path. |
| `src/manager.cpp` | `DataManager::loadFromFile` / `loadFromStream` plus the internal builder state machine. Helpers derive the channel-side data type and append payload values. `ChannelBuilder` is a tagged struct. `applyBlockKind` dispatches by `BlockKind` variant; per-block transition helpers; `finalizeBuilder` returns the typed `DataChannel` or `std::nullopt` for Unsupported. `parseHeaderAndMetablock` does the OSFZ peek + magic-header + metablock parse. |
| `src/blockencode_p.{h,cpp}` | Phase 7a private encoder (`osf::detail::encode_*`): `encodeStartData<T>` / `encodeContinuedData<T>` (float/double), `encodeAbsTimestampData<T>` (11 numeric), `encodeAbsTimestampDataGps`, single-sample string/binary overloads. Emits the full `[u16 ci][len][payload]` frame, bit-7 = 0 for count==1, no trailing `0x00` (OSF5). Composed by both writers |
| `src/durablefile_p.{h,cpp}` | Phase 7b RAII file wrapper (`write` / `force` / `close`); `FlushFileBuffers` on Windows, `fsync` on POSIX. Used only by `StreamingWriter` |
| `include/osf/binarysample.h` | `osf::BinarySample` non-owning byte view (C++17 `std::span` substitute); the variable-length write APIs take it |
| `src/writercommon_p.{h,cpp}` | **Phase 7c** private writer infrastructure shared by both writers: chunking helpers (`maxPayloadForSov`, `maxSamplesPerStartBlock`, `maxSamplesPerContinuedBlock`, `maxSamplesPerTimestampedBlock`, `variableSampleCapacity`), sizing constants, `FileInfoDraft`, and `buildMetablock` (assembles the OSF5 metablock; normalises `channeltype` to `scalar` for non-equidistant channels) |
| `include/osf/streamingwriter.h` + `src/streamingwriter.cpp` | Phase 7b embedded streaming writer (encode → write → fsync per block; constant memory). `ChannelDef`, config setters, `addChannel`, `start()` / `close()`, the four write families. Cannot auto-bump `sizeOfLengthValue` (metablock already on disk) |
| `include/osf/blockwriter.h` + `src/blockwriter.cpp` | **Phase 7c** analyst-style writer: accumulates a per-channel `ChannelData` variant in memory, emits the whole file at `writeToFile(path)` / `writeTo(ostream&)` (const). Same template surface as `StreamingWriter`; **does** auto-bump variable `sizeOfLengthValue` 2 → 4. `fromManager(DataManager const&)` + free `osf::writeToFile` / `osf::writeTo(DataManager, …)` for round-trip / copy |
| `include/osf/stalevalueguard.h` + `src/stalevalueguard.cpp` | **Phase 7d** optional freshness layer over `StreamingWriter`. Write-through wrapper: forwards each timestamped write and caches the channel's last `(timestamp, value)`. `poll(now_ns)` re-emits the cached value of any channel idle `>= repeat_interval_ns` (default 100 s) stamped at `now_ns`, at most once per poll (no backfill, no internal clock/thread). Numeric (11 types) + `GpsLocation` only; string/binary excluded. Auto-tracks on first write-through; `isTracked` / `forget` / `clear`. |
| `tests/CMakeLists.txt` | GoogleTest via `FetchContent`; pinned to v1.15.2 by tarball URL + SHA256; `gtest_force_shared_crt=ON` for /MD parity; `DOWNLOAD_EXTRACT_TIMESTAMP=FALSE` for CMP0135 NEW behaviour; `OSF_EXAMPLES_DIR` define for integration tests |
| `tests/integration/test_header_examples.cpp` | Four integration tests against `examples/`: `motorbike.osf` and `steam_loco.osf` parse as Osf4; raw `weather_station.osfz` gzip bytes are not parseable as a plain magic header (the low-level parser deliberately does not decompress — OSFZ transparency lives in the DataManager layer); the generated files directly under `examples/generated/` (19 since OSF-UP4 — the test iterates the directory rather than hard-coding a count) all parse with version per filename prefix |
| `tests/integration/test_metablock_examples.cpp` | Three integration tests against the OSF5 reference files in `examples/generated/`: snapshot check on `osf5_equidistant.osf`; every `osf5_*.osf` parses with non-empty channels and valid `sizeOfLengthValue`; `osf5_gpslocation.osf` actually declares a `GpsLocation` channel |
| `tests/integration/test_metablock_xml_examples.cpp` | Six integration tests against `examples/generated/osf4_*.osf` plus the two field samples: snapshot check on `osf4_equidistant.osf`; every `osf4_*.osf` parses with valid `sizeOfLengthValue`; `osf4_gpslocation.osf` declares a `GpsLocation` channel; `motorbike.osf` and `steam_loco.osf` metablocks parse end-to-end (encoding-tolerance + deprecated-field-tolerance paths); cross-parser symmetry probe (`osf4_equidistant.osf` via XML parser matches `osf5_equidistant.osf` via JSON parser on every channel field) |
| `tests/integration/test_reader_examples.cpp` | Six BlockReader integration tests: every `.osf` under `examples/generated/` streams end-to-end producing at least one block; first-block snapshots on `osf5_scalar_int64.osf` (single-sample AbsTs Int64) and `osf4_equidistant.osf` (StartData with sample_rate > 0); `motorbike.osf` and `steam_loco.osf` field samples stream clean; reader-stats sanity (non-zero counters, at least one channel produces a time range) |
| `tests/integration/test_manager_examples.cpp` | Seven DataManager integration tests: every `.osf` under `examples/generated/` loads via `loadFromFile` and produces at least one non-empty channel with name-lookup verified; snapshot probes pin `osf4_equidistant.osf` (first channel Equidistant), `osf5_gpslocation.osf` (a GpsLocation channel exists), `osf4_timestamped_string.osf` (a String channel exists); `motorbike.osf` + `steam_loco.osf` field samples load clean; `weather_station.osfz` is rejected with the Phase-8 OSFZ error. |
| `tests/unit/test_error.cpp` | Five smoke tests covering Error, Result-with-value, Result-with-error, `osf::version()`, and `errorCategoryName` |
| `tests/unit/test_header.cpp` | 16 unit tests against synthetic byte sequences: identifier spellings, error codes, CRLF tolerance, lone-CR rejection, stream-position invariant, buffer↔istream equivalence, path overload (success + missing-file), `MagicHeader` equality |
| `tests/unit/test_types.cpp` | Nine unit tests for `parseDataType` / `parseChannelType` / `parseSpectrumType`: every current spelling, `bytearray` alias, removed-in-spec rejection, unknown-spelling fallback |
| `tests/unit/test_metablock.cpp` | 20 unit tests for `parseMetablockJson`: minimal + full channel + infos round-trip, forward-compat (unknown top-level + deprecated channel fields tolerated), negative cases (removed datatype, missing envelope, malformed JSON, non-object root, non-array channels/infos, every required channel field missing, index out-of-range, invalid `sizeOfLengthValue`), overload-agreement, null-pointer edge cases |
| `tests/unit/test_metablock_xml.cpp` | 20 unit tests for `parseMetablockXml`: minimal + full channel + infos round-trip, short-form / long-form geolocation, `bytearray` alias, `count` mismatch tolerance, deprecated `scale`/`offset` tolerated, unknown attribute ignored, negative cases (removed datatype, wrong root, malformed XML, every required-attribute-missing case, invalid `sizeOfLengthValue`, channel-index out-of-u16-range, non-numeric `timeincrement`), overload-agreement, null-pointer edge cases |
| `tests/unit/test_block.cpp` | 7 unit tests covering payload len helpers (numeric / timestamped / rel-timestamped, including string / binary / GpsLocation), `decodeControlByte` for every documented value plus the multi-sample bit and unknown-byte fallback, `GpsLocation` equality, default `Skipped::payload` is `nullopt` |
| `tests/unit/test_stats.cpp` | 6 unit tests for `ChannelStats::observeTimestamp` (two-sided growth), `formatBytes` (unit thresholds), `formatDuration` (ms / s split), `compressionFormatName`, and the two ostream overloads |
| `tests/unit/test_reader.cpp` | 24 BlockReader unit tests against synthetic byte sequences (port of the Rust reader suite): empty stream, three truncation paths (channel-index / length-field / mid-payload), unknown-channel-index hard error, `Unsupported`-channel skip with stream alignment, capture-skipped opt-in, deprecated control bytes 1/3/4, unknown high control byte 0x55, every typed parser (single + multi for AbsTs int64 / double, StartData double + float-N10, ContinuedData int16-N4, AbsTs string version-deterministic-strip in both OSF5 and OSF4, AbsTs binary version-deterministic-strip in both OSF5 and OSF4, AbsTs gpslocation, ContinuedRelStampData int16), `InvalidBlock` for equidistant-on-string, trailer consumption, range-based-for iteration |
| `tests/unit/test_data_channel.cpp` | 11 unit tests for the typed channel model: `NumericValues` data-type detection and `emptyFor` (returns `std::nullopt` for variable + Unsupported); equidistant `samplesVector` with single segment, three segments without interpolation between them, empty channel; flat-access mismatch returns `DataTypeMismatch`; timestamped `samplesVector` pairs correctly + flat-access works; variable string + binary channels collect values + `asStrings` / `asBinaries` mismatch handling; common `DataChannel` accessors per variant |
| `tests/unit/test_manager.cpp` | 13 DataManager unit tests driving the builder through synthetic in-memory OSF5 streams: one-start-plus-continued = one-segment, two-starts = two-segments, start-then-abs-ts = `ChannelMixedBlockTypes`, continued-without-start = `ContinuedDataWithoutStart`, abs-int32 builds Timestamped, rel-stamp-after-abs extends cumulatively, rel-stamp-without-anchor = `RelStampWithoutAnchor`, variable-string collects strings, Unsupported channel dropped from output, name + index lookups, and a truncated-gzip input fails gracefully (Phase-8 decompressor yields best-effort EOF, then the header parse fails — no crash, no leftover stub message) |
| `tests/unit/test_stale_value_guard.cpp` | 12 `StaleValueGuard` unit tests (Phase 7d): no-repeat-before-interval, single repeat @ now after interval, real-write resets staleness, at-most-one-repeat-per-poll (no backfill), repeated polls keep re-emitting, multiple channels mixed numeric types + GPS, batch-write caches the last sample, custom interval honoured, un-advanced `now` re-emits nothing, untracked channel ignored, `isTracked`/`forget`/`clear` control surface, writer-error propagates out of `poll`. Each writes through a real `StreamingWriter` to a temp file, then reloads via `DataManager` to assert repeated timestamps/values |
| `tests/unit/test_compression.cpp` | 10 unit tests (Phase 8): `detectCompression` classifies plain/zlib/gzip without consuming (position preserved); `DecompressingIStream` round-trips plain / zlib / gzip, treats `0x78 0xFF` (invalid zlib second byte) / single-byte `0x78` / empty / `OCEAN_STREAM_FORMAT4` as plain, and round-trips a 256 KiB payload that spans multiple inflate chunks. Links zlib directly to build compressed fixtures |
| `tests/integration/test_compression_examples.cpp` | 2 integration tests (Phase 8): a gzip and a zlib re-wrap of `steam_loco.osf` load via `loadFromStream` and match the plain load through `roundtripManagersEqual` (with `stats.compressed` / `compressionFormat` set); the real `weather_station.osfz` gzip field sample loads transparently with ≥1 non-empty channel |
| `tests/c_api/test_c_api.c` | Standalone **C99** smoke test (Phase 11), built + registered as ctest `c_api` only when `OSF_BUILD_C_API=ON`. Proves `osf/capi.h` is C-compatible and `osf-c` links + works: `osf_version`, load a generated `osf5_*.osf`, channel count + by-name lookup (same borrowed handle), data type + sample count, `read_timestamps` + `read_f64` copy-out counts, `osf_write_to_file` round-trip + reload, missing-file error path with non-empty `osf_last_error_message`. The osf-c lib dir is put on the runtime library path via the test's `ENVIRONMENT_MODIFICATION`. |
| `tests/unit/test_throwing.cpp` | 10 unit tests (Phase 9): `throwing::load` success / missing-file-throws / `load(istream)` success + garbage-throws; `writeToFile` + `writeTo(ostream)` round-trip via `roundtripManagersEqual`; `unwrap(Result<void>)` success (no throw) + failure (throws), `unwrap(Result<T>)` returns the value; `osf::Exception` carries `code()` + `error().message == what()`. Links `osf::osf` + the `integration/` include for `roundtriphelper.h` |
| `third_party/tl-expected/` | Vendored TartanLlama/expected v1.3.1 (`tl/expected.hpp` + `LICENSE`, CC0 1.0) |
| `third_party/nlohmann-json/` | Vendored nlohmann/json v3.12.0 (`nlohmann/json.hpp` + `LICENSE`, MIT). Single-header form; SHA-256 of `json.hpp` matches the upstream v3.12.0 release asset |
| `third_party/pugixml/` | Vendored pugixml v1.15 (`pugixml.hpp` + `pugixml.cpp` + `pugiconfig.hpp` + `LICENSE`, MIT). Unlike the other two vendored libraries pugixml is not header-only; the `.cpp` compiles into `osf_core` directly with warnings disabled (`/W0` on MSVC, `-w` on GCC/Clang). SHA-256: `pugixml.hpp = 2555F950…0043BE734`, `pugixml.cpp = 67C3892E…D09E0744`, `pugiconfig.hpp = 981CD9AD…FC98B92D` |
| `README.md`, `BUILD.md`, `CHANGELOG.md` | Per-package documentation |

**Build options (DECISIONS §20):**

- `BUILD_SHARED_LIBS` (default OFF), `OSF_BUILD_TESTS` (default ON) — effective in Phase 1.
- `OSF_BUILD_EXAMPLES` (default ON, no-op until Phase ≥3), `OSF_BUILD_C_API` (default OFF; **ON builds the `osf-c` C ABI shared library + the `c_api` test** — Phase 11), `OSF_USE_SYSTEM_ZLIB` (default OFF, Phase 8 zlib provider), `OSF_WARNINGS_AS_ERRORS` (default OFF; CI sets ON — Phase 10). When `OSF_BUILD_C_API=ON`, `CMAKE_POSITION_INDEPENDENT_CODE` is forced ON so the static core folds into the shared lib, and the C language is enabled for the C test.

**Build & test:**

```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

**Build verification (2026-06-12):**

- **CI (GitHub Actions)** builds + tests the C++ implementation on every
  change. The `test-cpp` job (ubuntu-latest / macos-14 / windows-latest)
  configures with `-D OSF_WARNINGS_AS_ERRORS=ON` (`/WX` / `-Werror`)
  **and `-D OSF_BUILD_C_API=ON`**, builds (incl. the shared `osf-c`), and
  runs ctest. FetchContent fetches googletest + zlib over HTTPS on
  the runners (no local-extract workaround needed there).
- Local (MSVC, Visual Studio 18, with `OSF_BUILD_C_API=ON`): `ctest`
  reports **354/354 passed** (measured 2026-07-29 — 346 before the OSF-UP4
  `bcMessageEvent` tests added on 2026-07-28; 345 before the OSF-UP3
  zero-length-block test added on 2026-07-28; 321 before the integrity-profile
  `crc` work added on 2026-07-09; 319 before the two DECISIONS-§13
  metadata-defaults tests added 2026-06-12) with 0 warnings under
  `/W4 /permissive-`. zlib 1.3.2 comes via FetchContent with the
  local-extract workaround (`FETCHCONTENT_SOURCE_DIR_ZLIB`) for the
  host's HTTPS-FetchContent failure (the CI's MSVC can differ from the
  local one, so a local `/WX` build is not a complete proxy for the
  Windows CI leg — verify Windows on CI).
- **Writer metadata (2026-06-12):** both writers now apply the
  DECISIONS §13 defaults at metablock assembly — `created_utc` is
  stamped automatically (`YYYY-MM-DDTHH:MM:SSZ`), unset `creator` →
  `osf-cpp/<version>`, unset `tag` → `default` (previously no
  `created_utc` was written at all; parity with the Rust writer).
- **Docs (2026-06-12):** German developer handbook at
  `docs/de/implementations/cpp/` (8 pages: Architektur, Lesen,
  Schreiben, Fehlerbehandlung, C-ABI, Bauen, Kochbuch, Interna) +
  reworked `docs/de/implementations/cpp.md` entry page; Doxygen
  comment pass over the public headers. EN mirror pending.
- **API rename (2026-06-12, BREAKING):** public C++ API migrated to
  camelCase — methods, free functions, struct fields, and header filenames
  all follow the smartCORE/Qt convention. Library version 0.0.1 → **0.1.0**.
  C ABI (`osf_*`), wire-format JSON/XML keys, and PascalCase types exempt.
  All 321 tests pass, documentation updated.

**Constraints:**

- C++17 is the firmly-defined language baseline in `CMakeLists.txt` (DECISIONS §20). No `OSF_CXX_STANDARD` switch — moving to C++20 or later is a deliberate library upgrade, not a build option.
- The library is Qt-neutral; a Qt-aware module may follow as a separate `integrations/` entry once the core is stable.

**Pending:**

**The §20 Implementation Order is complete — all eleven phases are done**
(both writers, the optional `StaleValueGuard`, transparent OSFZ read, the
opt-in throwing layer, CI on Linux/macOS/Windows, and the `osf-c` C ABI).
Remaining C++ work is incremental, not a numbered phase — see BACKLOG for
the deferred items (notably a full sample-by-sample C **builder** API,
per-exact-type numeric C getters, an `osf_load_buffer` memory/stream load
entry, and packaging/installing the DLL + import library).

The 18 polish nits from the Phase-7b code-quality reviews were all
folded in during Phase 7c (the four `### C++ StreamingWriter … polish
(post-Phase-7b)` BACKLOG entries are now closed). One Phase-7c review
also caught and fixed a strict-aliasing UB in the `BlockWriter`
`std::vector<bool>` emit path before merge. The §13 metadata defaults divergence (neither writer emitted
`created_utc`) was resolved on 2026-06-12 — both writers now stamp
`created_utc`, `creator`, and `tag` automatically.

The C ABI shared-library wrapper (Phase 11, DECISIONS §23) is done — Windows DLL / ActiveX/OCX and future language bindings can use `osf/capi.h` with `-D OSF_BUILD_C_API=ON`.

CI integration (Phase 10) is done — `ci.yml` covers `implementations/cpp/**` and runs a Linux/macOS/Windows job matrix with `-D OSF_WARNINGS_AS_ERRORS=ON`.

---

## Zero-length data blocks — OSF-UP3 (2026-07-28)

A data block whose per-channel length field reads `0` is a **non-conforming
writer artefact, not an error** — a conforming block always carries at least its
control byte. The rule is now normative in `docs/{en,de}/osf_general.md`
(*Zero-length data blocks*, DE + EN with identical anchors) and recorded as
[DECISIONS §25](DECISIONS.md#25-zero-length-data-blocks). It is a spec change in
its own right, which is why the header table's *spec revision in effect* moves to
**2026-07-28**. A pre-existing spec bug was fixed in the same pass: the "Basic
structure" list said the length field spans "the following data area", when it
actually spans control byte + payload (+ the frame CRC at integrity level `crc`).

**Readers.** All five implementations now classify the case under a dedicated
reason with its own counter, instead of misfiling it as a reserved-control-byte
skip — on a zero-length block no control byte is ever read, so the old
classification asserted something untrue and merged a writer bug with a
legitimate forward-compatibility skip:

| Implementation | Reason / counter | Note |
|---|---|---|
| Rust | `SkipReason::ZeroLengthBlock`, `ReaderStats.blocks_skipped_zero_length` | `SkipReason` is now `#[non_exhaustive]` (API-visible change); the `blocks_total` aggregation had to gain the new counter as a term — it briefly omitted it mid-branch (`0d8c48a`…`17771ee`), which is what `17771ee` calls a regression. `main` never undercounted: the frame was previously counted as `ReservedBlockType`, already a term of the sum |
| C++ | `SkipReason::ZeroLengthBlock`, `ReaderStats::blocksSkippedZeroLength` | `blocksTotal` gained the new counter as a term in the same commit, for the same reason — likewise never wrong on `main`. The `osf-c` C ABI has **no** zero-length getter yet (BACKLOG) |
| Java | `ZERO_LENGTH_BLOCK`, `ReaderStats.blocksSkippedZeroLength()` | |
| Python | `stats.blocks_skipped_zero_length` | inherited from the Rust core |
| **Delphi** | `TOSFFile.BlocksZeroLengthSkipped` | **behaviour fixed** — it used to `raise EOSFFormatError` and abort the whole file, making a recording unopenable in Delphi that read fine everywhere else |

Delphi's counter deliberately keeps the local `Blocks…Skipped` naming of its
siblings, so it reads `BlocksZeroLengthSkipped` where the other four use
`blocksSkippedZeroLength` / `blocks_skipped_zero_length` — a cross-language grep
for the shared name finds nothing in Delphi. The Delphi fix covers the normal
read path *and* the channel-filter path, and the unrecognised
length-field-width fall-through (which also produced `LenField = 0`) was split
out into its own guard, so a corrupt width is no longer misreported as a writer
artefact.

**Corpus + contract.** `examples/generated/malformed/osf5_zero_length_block.osf`
— hand-assembled from two writer outputs, since no writer in this repository can
emit the frame — is registered in `examples/reference_manifest.json` through a
new optional `"anomalies": { "zeroLengthBlocks": N }` field, and all four
manifest-driven conformance suites assert it. `osftool verify` reports the count
in both output modes (`Zero-length skips:` / `zero_length_skipped_count`) and
raises a warning naming OSF-UP3; plain `verify` keeps exit 0, `--strict`
escalates to 4.

**Writer audit.** All seven writer classes in this repository were audited and
cleared — none can emit a zero-length frame. The per-writer evidence table, the
two different mechanisms the guarantee rests on, the three risk shapes checked,
and the audit's own two coverage gaps live in
`examples/generated/malformed/README.md`. The remaining hunt is outside this
repository (om kernel, smartCORE `osfwriter`, device firmware) — see BACKLOG,
*Zero-length data blocks — find the producing writer (OSF-UP3)*.

**Suite totals after the round**, all measured on the work machine 2026-07-28:
Rust `cargo test` **178 passed / 2 ignored**; Java **244**; C++ ctest
**346/346**; Delphi DUnitX **29**; Python pytest **19 passed / 1 skipped**.

Follow-ups the round surfaced — manifest-contract strictness gaps, the empty
equidistant-segment writer divergence, the missing counter accessors (`osf-c`
has no zero-length getter, `TOSFDataManager` no counters at all, the Python
binding only a subset), and the N+1 warning mirroring in `osftool verify` — are
recorded in `BACKLOG.md`. The `osf-c` gap is the one to close first if the
writer hunt runs through a C/C++ integration.

---

## `bcMessageEvent` is read-mandatory — OSF-UP4 (2026-07-28)

Deployed device firmware writes OSF4 `string` channels as **`bcMessageEvent`
(control byte 4)**. Every reference reader skipped it as deprecated, so those
channels arrived **empty and silent** — no error, no warning, and in three of
the five implementations no statistic either. The spec only ever said the type
is no longer *produced* from OSF5 onwards, so emitting it in OSF4 is
**conforming**: this was a specification gap plus a reader gap, not a firmware
bug.

The rule is now normative in `docs/{en,de}/osf_general.md` (block-type table
rows 3 and 4, a dedicated `#### bcMessageEvent (deprecated, read-mandatory)`
subsection, a row in the block-type restriction table, and a corrected
`datatype` table) and recorded as
[DECISIONS §26](DECISIONS.md#26-bcmessageevent-is-read-mandatory). Both the
OSF-UP3 and the OSF-UP4 spec text carry `2026-07-28`, so the header table's
*spec revision in effect* stays at **2026-07-28** and simply gains a second
entry.

**The deeper cause was in the table, not the readers.** Row 4's payload column
omitted the `uint32` length prefix the bytes on disk actually carry — it
described the payload as a bare `string`. A reader built strictly from that row
decodes the wrong layout whether or not it also skips the block. Related trap,
also now stated: the OSF4 trailing-`0x00` rule belongs to `bcAbsTimeStampData`
only; `bcMessageEvent` is length-prefixed and never null-terminated, so reusing
`bcAbsTimeStampData`'s *framing* silently truncates every value by one byte.

**Readers.** All five decode control byte 4 as one time-stamped sample of the
channel's declared `datatype`, into their **existing** time-stamped
representation rather than a new block kind. `bcStatusEvent` (byte 3) keeps
being skipped — its payload is a fixed `uint32` status word regardless of
`datatype`, so attaching it as a sample would fabricate a value of the wrong
type — but everywhere gained a counter of its own:

| Implementation | Status-event reason / counter | Note |
|---|---|---|
| Rust | `SkipReason::StatusEventBlock`, `ReaderStats.blocks_skipped_status_event` | |
| C++ | `SkipReason::Kind::StatusEventBlock`, `ReaderStats::blocksSkippedStatusEvent` | |
| Java | `Block.SkipReason.STATUS_EVENT_BLOCK`, `ReaderStats.blocksSkippedStatusEvent()` | **also gained `blocksSkippedReservedType()` + `blocksSkippedDeprecatedType()`, which its `ReaderStats` never had at all** — before this round a skipped block left no trace in Java |
| Python | `stats.blocks_skipped_status_event` | inherited from the Rust core, together with the deprecated- and reserved-type getters |
| **Delphi** | `TOSFFile.BlocksStatusEventSkipped` | local `Blocks…Skipped` naming as with OSF-UP3 |

Two shapes are deliberately **not** guessed and are skipped-and-counted
instead: bit 7 (multi-value) set, whose layout is unspecified for this block
type, and any `datatype` other than `string` / `binary`. `N = 0` is legal and
decodes to an empty value — it is *not* the OSF-UP3 zero-length anomaly, where
the block's own length field is `0` and no control byte is ever read.

**Delphi additionally.** The codec unwraps the frame
(`TOSFFile.DecodeMessageEventPayload`) and hands the block on with
`BlockType = bcMessageEvent` and `RawPayload` holding the bare value bytes —
the type tag is load-bearing in two dispatches and must not be relabelled. The
manager dispatch feeds the sample to the channel, and the **meta cache was
fixed to count it too**: it previously disagreed with the manager, so
`osftool info` reported zero samples on a channel `osftool export` decoded five
from. `osftool verify` now surfaces the new counter (`Status-event skips:` /
`status_event_skipped_count`), deliberately as a reported line and not a
warning.

**Corpus + contract.** `examples/generated/osf4_message_event_string.osf` and
`…_equivalent.osf` hold the same `string` channel content twice — once as
`bcMessageEvent`, once as `bcAbsTimeStampData` — are reproducible via
`cargo run --example gen_message_event_refs`, and are registered in
`examples/reference_manifest.json`. All four manifest-driven conformance suites
assert them, and the assertion was proven non-vacuous by sabotage.

**Writer audit.** No writer in this repository can emit control byte 4, and the
round trip re-emits the decoded samples as `bcAbsTimeStampData` with the data
intact. The per-writer evidence table, what is cleared by test versus by
construction, and the audit's own four coverage gaps live in
[`examples/README.md`](examples/README.md#bcmessageevent-writer-audit--can-anything-here-emit-control-byte-4)
— not repeated here.

**Suite totals after the round**, each measured on the work machine 2026-07-29
by this bookkeeping pass (not copied from the implementation tasks): Rust
`cargo test` **189 passed / 2 ignored**; Java **265**; C++ ctest **354/354**;
Delphi DUnitX **37**, green under dcc32 *and* dcc64; Python pytest **23 passed
/ 1 skipped**.

Follow-ups the round surfaced — the three-way split on invalid UTF-8 in a
`string` payload (including a Delphi exception that escapes a best-effort API),
the shared string/binary builder that silently falls through to `Binary` in C++
and Java, Delphi's `BlocksUnknownTypeSkipped` naming plus its two uncounted
deprecated block types, and the remaining evidence gaps — are recorded in
`BACKLOG.md`. The UTF-8 one is the one to act on first: OSF-UP4 exists to
rescue legacy-firmware string channels, and firmware emitting CP1252/Latin-1 is
the plausible next field case.

---

## CI / release pipeline (Session 8)

GitHub Actions workflows live in `.github/workflows/`:

- `ci.yml` — runs on every push to `main`, every PR, and on
  `workflow_dispatch`. Job groups: `test-rust` (cargo test + clippy),
  **`test-cpp`** (Phase 10 — configure + build + ctest on
  ubuntu-latest / macos-14 / windows-latest with warnings-as-errors),
  `build-wheels` (4-platform matrix via maturin-action + per-wheel
  pytest run), `build-sdist`. A `summary` job aggregates results for
  branch-protection gating. The push + pull_request path filters cover
  `implementations/{python,rust,cpp}/**`, `examples/**`, and
  `.github/workflows/**` (the Delphi tree is still uncovered — no hosted
  Delphi toolchain).
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
- Other language implementations (C, C++, …) will
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

- **DUnitX test suite** for the Delphi implementation — **started
  (2026-07-08).** `implementations/delphi/tests/OSFTests.dpr` (DUnitX console
  runner, **37 tests**, dcc32 + dcc64 green) covers `OSF.CRC32C`, the header
  tokenizer, the integrity read/write path, the OSF-UP3 zero-length-block skip,
  and the OSF-UP4 `bcMessageEvent` decode. Structured for extension (one
  `Test.OSF.*` unit per area); the older OSF library units (merger, exporters,
  cache, …) are not yet covered — incremental follow-up. Note that the suite is
  still filer-centric: no Delphi test drives `osftool convert` / `TOSFMerger`,
  which is why the OSF-UP4 writer audit could clear Delphi only by reading.
- **OSF-UP3 writer origin** — the reader side is closed across all five
  implementations and held by the manifest contract, and the seven writers in
  this repository are audited and cleared, so the producer of the zero-length
  blocks seen in July 2026 field data is elsewhere (om kernel, smartCORE
  `osfwriter`, device firmware). `osftool verify` is the instrument. Details +
  the two `--json` gotchas: `BACKLOG.md`, *Zero-length data blocks — find the
  producing writer (OSF-UP3)*.
- **Follow-ups surfaced by OSF-UP3**, all in `BACKLOG.md`: manifest anomaly
  contract strictness gaps (Rust/C++ ignore unknown `anomalies` keys; Delphi
  does not assert `version`), the empty-equidistant-segment writer divergence
  (Rust/Python/Java emit a 21-byte zero-sample block, C++/Delphi refuse),
  reader counters missing from the surfaces callers use — **no zero-length
  getter in the `osf-c` C ABI** (the surface smartCORE and the om kernel
  integrate through, so the one that matters for the hunt), none at all on
  `TOSFDataManager`, and only a subset in the Python binding (OSF-UP4 closed
  most of that subset gap — only `blocks_skipped_unsupported` is still absent)
  — and the N+1 warning mirroring in `osftool verify`.
- **Follow-ups surfaced by OSF-UP4**, all in `BACKLOG.md`: **invalid UTF-8 in a
  `string` payload splits the implementations three ways** (Rust/Python/Delphi
  fail the whole load, C++ keeps raw bytes, Java substitutes `U+FFFD`) and
  Delphi's failure escapes as an exception through an API documented as
  best-effort, discarding every already-decoded channel — the highest-value
  entry, because OSF-UP4 exists to rescue legacy-firmware string channels and
  CP1252/Latin-1 firmware is the plausible next field case; the shared
  string/binary builder falling through to `Binary` on an unknown datatype in
  C++ and Java where Rust errors; Delphi's `BlocksUnknownTypeSkipped` naming
  plus its two still-uncounted deprecated block types (`bcTrustedTimestamp`,
  `bcTimebaseRealign`); Java's `ReaderStats` still having no `unsupported`
  bucket; and the round's remaining evidence gaps (no executable round-trip
  test for C++/Delphi, no `binary` corpus file for this block type, Rust
  missing a synthetic `N = 0` case).
- **Production PyPI release for `osfdata`** — the Python bindings are
  functional and CI already publishes to **TestPyPI** (`osfdata 0.1.0`); a
  production `pypi.org` release needs its own Trusted Publisher. Pandas
  convenience helpers remain a separate nice-to-have. See the *Python
  implementation* section above.
- **`optimeas/python-osf` deprecation header** — `osfdata` is on TestPyPI, so
  the trigger condition is met: add a "deprecated in favor of osfdata" notice to
  that repo's README. Mini follow-up.
- **Other language implementations** — only **C (native)** remains a README
  placeholder. Rust, Python, C++ (§20 complete), Java, and Delphi are all
  implemented — see their sections above.
- **Integrations** (Arrow, PyTorch, TensorFlow, MCP, LangChain) — README
  placeholders only.
- **`docs/*/references/osf_vector_matrix.md`** — placeholder content
  (`weareworkingonit.png`); the full vector/matrix specification is pending.

---

## Next session priorities (as of 2026-07-29)

**Integrity profile — stage `crc` (level b) is complete across all five
active implementations** and locked down by the shared conformance contract.
Rust, Python, C++ (`osf-cpp` + `osf-c`), Java, and Delphi all read + write OSF5
files with `crc32c` framing (metablock CRC + per-block frame CRC, signature-block
skip). The four integrity reference files under
`examples/generated/integrity/` are now listed in
`examples/reference_manifest.json` (sub-path keys, optional `integrity` field),
so every implementation's manifest-driven conformance test loads them and
additionally asserts the reported profile + zero frame-CRC failures — one shared
file list, no per-language duplication. Per-implementation detail lives in the
*integrity profile* subsections above; the `none ⊂ crc ⊂ signed` ladder and wire
format are in [DECISIONS](DECISIONS.md) (integrity section).

**OSF-UP3 — zero-length data blocks — is closed on the reader side
(2026-07-28).** The rule is normative in `docs/{en,de}/osf_general.md` and in
[DECISIONS §25](DECISIONS.md#25-zero-length-data-blocks); all five
implementations skip + count the frame under a dedicated reason, Delphi no
longer aborts the file, the malformed corpus file is a manifest key with an
`anomalies` count that all four conformance suites assert, and `osftool verify`
surfaces it. See *Zero-length data blocks — OSF-UP3* above. **The open remainder
is the writer origin:** the seven writers in this repository are audited and
cleared, so the producer is outside it (om kernel, smartCORE `osfwriter`, device
firmware). That is a corpus hunt with `osftool verify`, not a coding task, and
it waits on a field file plus its device and firmware version —
[BACKLOG.md](BACKLOG.md) → *Zero-length data blocks — find the producing writer
(OSF-UP3)* has the entry conditions and the two `--json` gotchas.

**OSF-UP4 — `bcMessageEvent` is read-mandatory — is closed (2026-07-28).**
Unlike OSF-UP3 there is no writer hunt behind it: the firmware that produces the
encoding is *conforming* in OSF4, so the fix was ours to make and it is made.
The rule is normative in `docs/{en,de}/osf_general.md` and in
[DECISIONS §26](DECISIONS.md#26-bcmessageevent-is-read-mandatory); all five
implementations decode control byte 4 into their existing time-stamped
representation and count `bcStatusEvent` separately; the corpus pair is a
manifest key all four conformance suites assert; and the writer audit
(`examples/README.md`) shows nothing here can emit the byte. See
*`bcMessageEvent` is read-mandatory — OSF-UP4* above. **The open remainder is
the encoding question the round deliberately did not touch:** invalid UTF-8 in
a `string` payload splits the implementations three ways, and legacy firmware
emitting CP1252/Latin-1 is the plausible next field case — `BACKLOG.md` →
*Invalid UTF-8 in a `string` payload splits the implementations three ways*.

**Next: stage `signed` (level c).** Level `signed` adds an Ed25519 signature
block (control byte 9) on the reserved `0xFFFE` channel over a hash chain of the
frame CRCs. A brief for it is on hand; the plan is to stand up a **test PKI** and
a **Rust reference implementation** first, then fan the same design out to the
other implementations the way stage `crc` was. No code exists for it yet.

Other continuations remain incremental / BACKLOG — notably the deferred `osf-c`
C builder surface + packaging (see `BACKLOG.md`); a native **C** implementation
is still README-only.

---

## Resuming in a new Claude Code session

Drop this in as the first message:

> Lies `STATUS.md`, dann `BACKLOG.md`, dann `DECISIONS.md`. Wir machen weiter.

(The `BACKLOG.md` step matters — incremental follow-ups and the
integrity roadmap beyond the current wave live there and in
DECISIONS §24, not in STATUS.md proper.)

Older form, still works for sessions where the BACKLOG is
already familiar:

> Lies `STATUS.md`, dann `DECISIONS.md`. Wir machen weiter.

Or, if you want to be absolutely minimal:

> `STATUS.md` lesen, dann weiter.

Claude Code finds the file in the working directory automatically. The
`auto-memory` system already holds the cross-session essentials (always-push
rule, dcc32 path, segments-model decision, Delphi-active workdir) — those
load on every session start; you don't need to repeat them.

For Claude.ai web chats (no filesystem access), paste the contents of this
file directly.
