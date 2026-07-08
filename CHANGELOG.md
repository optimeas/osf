# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Unreleased]

### Added

- **OSF5 integrity profile level `crc` — Rust (`osf-core`) + Python (`osfdata`).**
  Reader: a strict, must-understand magic-header tokenizer (`crc32c` /
  `ed25519`, `OsfError::UnknownHeaderToken` on unknown keys), metablock-CRC
  verification (`OsfError::MetablockCrcMismatch`), and fail-closed per-block
  frame-CRC32C framing (carved off before the typed parse; a mismatch skips the
  block and bumps `ReaderStats.blocks_crc_failed`). Signed files stay
  readable/CRC-checked: signature blocks on channel `0xFFFE` are skipped and
  counted, `verification_status()` reports `signature_unverifiable`. Writer:
  `WriterBuilder::with_integrity(IntegrityProfile::Crc32c)` emits the token and
  frame CRCs. Python mirrors this via `stats.integrity` / counters /
  `verification_status` and `WriterBuilder.with_integrity` /
  `save(..., integrity="crc32c")`. New Rust-generated reference files under
  `examples/generated/integrity/` (`osf5_crc_*.osf`). Depends on the `crc`
  crate. Signing (level `signed`) is not implemented. **No spec changes.**
- **OSF5 integrity profile — specification revision (2026-07-07).** New normative
  spec `docs/{de,en}/references/osf5_integrity.md` describing an **optional**
  three-level integrity profile for OSF5 (`none ⊂ crc ⊂ signed`): a "must
  understand" magic-header token declares the level; level *crc* adds a per-block
  frame **CRC32C** (fail-closed framing, effective payload = LEN − 4); level
  *signed* adds a SHA-256 hash chain with periodic **Ed25519** signature anchors
  (`bcIntegritySignature = 9` on the reserved file-wide channel `0xFFFE`) plus
  metablock-embedded **X.509** certificates for offline third-party verification.
  OSF4 is unaffected; profile-less OSF5 files stay valid. Supporting doc changes:
  `osf_general.md` (header-token grammar + must-understand rule), `osf5.md`
  (control byte 9, integrity overview, new `file_uuid` metablock parameter),
  `osf4.md` (not-affected note), `index.md` (feature mention). New decision record
  DECISIONS §24. Rationale: concept paper (Zenodo, DOI 10.5281/zenodo.21227942);
  current-parser starting point: `AUDIT_INTEGRITY_O1.md`. **Documentation only —
  no implementation changes** (those follow via separate task briefs).
- **German developer handbook for the C++ implementation** — new subtree `docs/de/implementations/cpp/` with eight in-depth pages ([Architektur](docs/de/implementations/cpp/architektur.md), [Lesen](docs/de/implementations/cpp/lesen.md), [Schreiben](docs/de/implementations/cpp/schreiben.md), [Fehlerbehandlung](docs/de/implementations/cpp/fehlerbehandlung.md) incl. the complete error-code catalogue, [C-ABI](docs/de/implementations/cpp/c-abi.md) incl. function catalogue + C/P-Invoke examples, [Bauen & Einbinden](docs/de/implementations/cpp/bauen.md) incl. `add_subdirectory`/FetchContent consumption snippets, [Kochbuch](docs/de/implementations/cpp/kochbuch.md) with copy-ready recipes, and [Interna](docs/de/implementations/cpp/interna.md) for contributors). `docs/de/implementations/cpp.md` reworked into the overview/entry page linking the handbook. The English mirror of the new subtree is an open follow-up.

### Fixed

- C++ writers now apply the **DECISIONS §13 file-metadata defaults** when assembling the metablock (both `StreamingWriter` and `BlockWriter` via the shared `build_metablock`): `created_utc` is always stamped automatically with the current UTC time (`YYYY-MM-DDTHH:MM:SSZ`, same format as the Rust writer), an unset `creator` falls back to `osf-cpp/<library-version>`, an unset `tag` falls back to `"default"`. Previously the C++ writers emitted files **without** `created_utc` and without the creator/tag fallbacks, violating §13 ("Set automatically by the writer") and diverging from the Rust/Delphi writers. Found during the C++ documentation pass; `reason` and the `created_at_*` triple stay omitted-when-unset as before. Two new unit tests pin the defaults and the explicit-value precedence; C++ ctest 319 → **321/321 green**.

### Changed

- **C++ API rename — camelCase convention (BREAKING, `osf-cpp` 0.0.x → 0.1.0):** the entire C++ public API now follows the smartCORE / Qt camelCase style sheet. Methods and free functions: `loadFromFile`, `writeToFile`, `addChannel`, `parseMagicHeader`, `asDoublesFlat` (and all 12 `as*Flat` helpers), `setCreator`, `withFileSize`, `fromManager`, `channelByIndex`, `errorCategoryName`, `formatBytes`, `decodeControlByte`, `detectCompression`, `parseMetablockJson/Xml`, `samplesVector`, `writeTimestampedSample`, etc. Public struct fields: `blocksTotal`, `sizeOfLengthValue`, `startTimestampNs`, `fileInfo`, `createdUtc`, `sampleRateHz`, `dataType`, `channelType`, `perChannel`, `timeRangeNs`, `trailerSeen`, `compressionFormat`, etc. Private members: trailing-underscore → `m_` prefix. Header files renamed to lowercase with no separator and `.h` extension (`blockwriter.h`, `streamingwriter.h`, `stalevalueguard.h`, `datachannel.h`, `binarysample.h`, `throwing.h`, `capi.h`, `osf.h`); internal src headers carry a `_p.h` suffix (`blockencode_p.h`, `writercommon_p.h`, `durablefile_p.h`, `binaryio_p.h`). The **C ABI** (`osf_*` symbols in `capi.h`), **wire-format JSON/XML keys** (e.g. `created_utc`, `sizeoflengthvalue`, `data_type` on disk), and **PascalCase types** are explicitly exempt. Library version bumped `0.0.1 → 0.1.0` (MINOR bump for the breaking change).

- **C++ standalone-distribution cleanup:** the consumer-facing surface of `implementations/cpp/` (public headers, `README.md`, `BUILD.md`, examples) and the German developer handbook (`docs/de/implementations/cpp*`) no longer reference repo-internal governance documents (`DECISIONS.md`, `BACKLOG`) or other OSF implementations (Rust/Delphi comparisons). Decision citations were replaced by the decision content itself; spec pointers reference the OSF format specification generically instead of repo-relative paths. The C++ tree is now self-contained and distributable including its documentation. Deliberate exceptions: the handbook keeps the source-code link plus the `git clone`/FetchContent URLs (distribution channel, not a content dependency). Comments/docs only — no behaviour change.
- C++ public headers received a **Doxygen documentation pass**: per-method docs for the complete `StreamingWriter` / `BlockWriter` write surface (chunking behaviour, `sizeoflengthvalue` guidance, error conditions), documented enumerators for all `osf::Error::Code` values, `Result<T>` usage idiom, and an explanatory umbrella-header note on the deliberately excluded `throwing.hpp` / `c_api.h`. A stale comment in `src/writer_common.hpp` (claiming `created_utc` was already stamped) was corrected, and `implementations/cpp/BUILD.md` gained the missing `OSF_WARNINGS_AS_ERRORS` row in its CMake-options table. Comments/docs only — no behaviour change beyond the §13 fix above.

- Rust `osf-core` writer: single-sample `string` / `binary` `bcAbsTimeStampData` blocks now emit the spec-canonical compact form (bit-7 = 0, no `[u32 N=1]` prefix, OSF5 no trailing `0x00`) — payload layout `[0x08][i64 ts][bytes]`. Replaces the expanded form (bit-7 = 1 + explicit `u32 N=1`) that the writer used previously; saves 4 bytes per block and matches the byte-exact output of the C++ Phase 7a encoder so the upcoming Phase 7b cross-implementation roundtrip tests can share fixtures. Both forms remain valid per spec and the readers in every implementation accept either; only the writer changed. Two new byte-exact assertions in `writer.rs` tests pin the canonical wire format. 124 unit + integration tests green; `cargo clippy --all-targets` clean.
- Reader-comment correction in `implementations/cpp/src/reader.cpp:230` and `implementations/rust/osf-core/src/reader.rs:708` — the in-source notes that said *"per spec bit 7 should be set; we tolerate clear bit as implicit N=1"* misread the spec. Both forms are valid; the new wording records that the C++ encoder and the (now-fixed) Rust writer both emit the canonical bit-7 = 0 form for single-sample blocks, and that the reader still accepts either. Documentation-only; behaviour unchanged.

### Removed

- MicroPython implementation placeholder (`implementations/micropython/`) — a status-only `README.md` with no code. Dropped during public-release preparation; the embedded writer-only niche it described is already covered by the planned C implementation. All references removed from `README.md`, `implementations/README.md`, `DECISIONS.md` (implementation table, streaming/block platform table, and priority order — items renumbered), `STATUS.md`, and `implementations/swift/README.md`.

### Added

- C++ `osf-core` Phase 11 — C ABI wrapper (`osf-c`), the final §20 phase (DECISIONS §23). A separate **shared** library exposing the C++ core through a pure-C99 `extern "C"` header `implementations/cpp/include/osf/c_api.h`, built only when `OSF_BUILD_C_API=ON` (default OFF). Opaque `osf_manager` (owns a `DataManager`) + borrowed `osf_channel` handles; `osf_status` codes mirroring `osf::Error::Code` (`OSF_OK == 0`); thread-local `osf_last_error_message()`; full read path (load OSF/OSFZ, channel enumeration + metadata, **caller-buffer copy-out** readers for timestamps / f64 / i64 / GPS with reconstructed equidistant timestamps, borrowed string/binary accessors) plus a round-trip `osf_write_to_file` (always OSF5). Every entry point is `try/catch`-wrapped so no C++ exception crosses the ABI. A standalone **C99** test (`tests/c_api/test_c_api.c`) proves C-compatibility + DLL linkage; CI builds the shared lib and runs the C test on ubuntu/macos/windows (`-D OSF_BUILD_C_API=ON`). Two CMake fixes landed for the cross-compiler legs (`enable_language(C)`; `CMAKE_POSITION_INDEPENDENT_CODE` to fold the static core into the shared lib). ctest 304 → **305/305 green** with the C API on, 0 warnings. **This completes the §20 Implementation Order (phases 1–11);** remaining C++ work is incremental (BACKLOG), not a numbered phase.
- C++ `osf-core` Phase 10 — CI integration (DECISIONS §20). `.github/workflows/ci.yml` now covers the C++ implementation: `implementations/cpp/**` joins the push + pull_request path filters (C++ changes previously triggered no CI run), and a new `test-cpp` job configures + builds + runs ctest across a **ubuntu-latest / macos-14 / windows-latest** matrix with **warnings-as-errors**, gating the `summary` job. New opt-in CMake option `OSF_WARNINGS_AS_ERRORS` (default OFF; CI sets it ON) wires `/WX` (MSVC) / `-Werror` (GCC/Clang/AppleClang) into `osf_set_warnings` for OSF targets only. This is the first GCC/AppleClang build of the C++ code (previously MSVC-only); two warnings-as-errors hits were cleared — a dead Float/Double check in `block_writer.cpp` (MSVC C4127 → `static_assert`) and an unused test helper (`-Werror=unused-function`). All three OS legs green (304/304 ctest each); the full CI run (Rust + C++ + wheels + sdist + summary) is green.
- C++ `osf-core` Phase 9 — opt-in, header-only throwing convenience layer at `implementations/cpp/include/osf/throwing.hpp` (DECISIONS §20). Exposes the `Result`-based core API as exception-throwing functions for consumers who prefer RAII-style error propagation. `osf::Exception : std::runtime_error` carries the `osf::Error` (`what()` = message or category name; `code()` / `error()` for structured detail). `osf::throwing::unwrap(Result<T>)` returns the value or throws — works on any core `Result` including the writer methods (`unwrap(w.start())`), so the layer needs no per-method writer wrappers. Free `osf::throwing::load(path)` / `load(istream&)` → `DataManager` and `write_to_file(mgr, path)` / `write_to(mgr, ostream&)` → `void` (OSF5). Header-only, not part of the `osf/osf.hpp` umbrella and not compiled into the library, so consumers who never include it pull in no extra machinery. 10 new GoogleTest cases bring the C++ ctest count from 294 to **304/304 green**, 0 warnings under MSVC `/W4 /permissive-`. Next: Phase 10 (CI integration).
- C++ `osf-core` Phase 8 — transparent OSFZ (gzip / zlib) decompression on the read path (`implementations/cpp/include/osf/compression.hpp` / `src/compression.cpp`). Removes the `DataManager` OSFZ-rejection stub so gzip- and zlib-wrapped OSF files load transparently (deployed optiMEAS devices emit gzip-OSFZ — `weather_station.osfz`, the Train OSFZ field recordings; older tooling used raw zlib). `osf::DecompressingIStream` is a `std::istream` that classifies a stream by its leading two bytes and inflates on demand through a custom `std::streambuf` (constant-memory streaming, no whole-file buffering; auto gzip/zlib header detection via `inflateInit2(MAX_WBITS | 32)`; best-effort EOF on truncation; `z_stream` hidden behind a PIMPL so the public header stays zlib-free), plus a non-consuming `detect_compression(std::istream&)`. `DataManager` wraps its input before the magic-header parse and populates `ReaderStats::compressed` / `compression_format`; the low-level `parse_magic_header` stays non-decompressing by design. zlib provisioning honours the declared `OSF_USE_SYSTEM_ZLIB` option — default fetches zlib 1.3.2 via FetchContent (pinned tarball + SHA256), `ON` uses `find_package(ZLIB)`; zlib is a PRIVATE dependency of `osf_core`. New `test_compression.cpp` + `test_compression_examples.cpp` (gzip+zlib re-wrap of `steam_loco.osf` matches the plain load; `weather_station.osfz` loads). ctest 283 → **294/294 green**, 0 warnings under MSVC `/W4 /permissive-`. Next: Phase 9 (throwing convenience layer).
- C++ `osf-core` Phase 7d — `osf::StaleValueGuard`, the optional freshness layer over `StreamingWriter` for timestamped channels (`implementations/cpp/include/osf/stale_value_guard.hpp` / `src/stale_value_guard.cpp`). Re-emits the last value of idle channels so their on-disk trace stays fresh (the optiMEAS 100-second-repeat convention), disambiguating *channel still at this value* from *recording stopped*. No Rust/Delphi reference exists — from-scratch C++ design: a write-through wrapper around a caller-owned `StreamingWriter` that forwards each timestamped write and caches the channel's last `(timestamp, value)`; a pull-based `poll(now_ns)` re-emits the cached value of any channel idle `>= repeat_interval_ns` (default 100 s) stamped at `now_ns`, at most once per poll (no backfill, no internal clock, no background thread). Numeric (11 types) + `GpsLocation` only; string/binary excluded by design. Channels auto-track on first write-through; channel-type validation delegated to the writer. 12 new GoogleTest cases bring the C++ ctest count from 271 to **283/283 green**, 0 warnings under MSVC `/W4 /permissive-`. Completes Phase 7 (DECISIONS §20); Phase 8 (transparent OSFZ decompression on read) is next.
- C++ `osf-core` Phase 7b — `osf::StreamingWriter` (embedded streaming OSF5 writer). Public API at `implementations/cpp/include/osf/streaming_writer.hpp` ships all four write families: `start_equidistant_segment` + `append_equidistant_samples` (float + double per spec rev 2026-05-04), `write_timestamped_sample<T>` + `write_timestamped_samples<T>` (templates with `static_assert` over 11 numeric types — bool, int8/16/32/64, uint8/16/32/64, float, double), separate non-template `write_timestamped_gps_sample` / `write_timestamped_gps_samples` for GPS, and single-sample `write_timestamped_string` / `write_timestamped_binary` per spec rev 2026-05-24. Power-loss safety via per-block `DurableFile::force()` (`FlushFileBuffers` on Windows, `fsync` on POSIX); reader is best-effort at the truncation boundary so partial files remain valid up to the last fsync'd block. Transparent chunking for numeric write families at the channel's `sizeoflengthvalue` boundary; variable types are one-sample-per-block per spec with capacity-aware pre-check error messages (Spec §3.3). Six private implementation commits + four BACKLOG polish commits land via merge `0f8197d`; ctest moves from 192/192 to **245/245 green** in ~10 s, +0 warnings under MSVC `/W4 /permissive-`. Phase 7c (`BlockWriter`, analyst-style) and Phase 7d (`StaleValueGuard`) arrive next on the same encoder substrate. (Internal per-phase plan + spec artefacts are kept in git history, not in the tree.)
- C++ `osf-core` Phase 7a — private block-encoder layer at `implementations/cpp/src/block_encode.hpp` / `.cpp` in namespace `osf::detail`, plus the shared little-endian helper hub `src/binary_io.hpp` (9 read + 9 write helpers, file-private, mirroring Rust's `binary_write.rs`). Six encoder symbols: `encode_start_data<T>` and `encode_continued_data<T>` (templates, 2 instantiations each for `float` / `double`), `encode_abs_timestamp_data<T>` (template, 11 instantiations across `bool` / `int8`–`int64` / `uint8`–`uint64` / `float` / `double`), `encode_abs_timestamp_data_gps` (plain function), and two single-sample overloads of `encode_abs_timestamp_data` for `std::string_view` and `BinarySample`. All encoders return `Result<void>` with three documented error conditions (count==0 / bad `sizeoflengthvalue` / oversize payload). Bit-7 selection by `count` per spec rev 2026-05-24; string/binary blocks are single-sample and OSF5-conformant (no trailing `0x00`). Composed by `StreamingWriter` (Phase 7b) and the future `BlockWriter` (Phase 7c). 35 new GoogleTest cases bring the C++ ctest count to **192/192 green** under MSVC `/W4 /permissive-`.
- Repo-wide convention change: all C++ files under `implementations/cpp/` now carry the two-line `// SPDX-License-Identifier: MIT` + `// Copyright (c) 2026 Optimeas GmbH` header (was: SPDX-only). Matches the Delphi, Rust, and Python-PyO3 implementations; file-level attribution travels with detached source snippets. `CLAUDE.md` updated, 36 pre-existing C++ files retrofitted in the same pass.
- `BACKLOG.md` — two entries surfaced by the Phase 7a final code review: a low-priority documentation-correctness fix to misleading bit-7 comments in `implementations/cpp/src/reader.cpp` and `implementations/rust/osf-core/src/reader.rs`, plus a high-priority cross-implementation conformance item — the Rust writer emits bit-7=1 with explicit `uint32 N=1` for single-sample variable-length blocks where the spec-canonical and new-C++-encoder form is bit-7=0 with implicit N. Recommended action before Phase 7b begins.
- Internal design + execution artefacts for the Phase 7a work (per-phase plan + spec), retained in git history for future-session traceability.

### Notes

- No release tag yet; this section accumulates until a release decision. The Phase 7a deliverable is independently usable as the substrate for Phase 7b/7c.

---

## [0.10.0] — 2026-05-25

### Changed

- Specification (rev 2026-05-24): the null-terminator rule for `string` and `binary` payloads in `bcAbsTimeStampData` is now **version-deterministic**. OSF4 writers MUST append the trailing `0x00` and OSF4 readers MUST strip the last byte unconditionally; OSF5 writers MUST NOT append it and OSF5 readers MUST NOT strip any trailing byte. Replaces the soft "strip if present" heuristic — eliminates the ambiguity for OSF5 binary payloads that legitimately end in `0x00` (ASN.1 blobs, protobuf messages, null-terminated strings stored as binary). Affects six docs (EN + DE of `osf_general.md`, `osf4.md`, `osf5.md`) plus `DECISIONS.md` §16 + §21. Anchor IDs preserved.
- Specification (rev 2026-05-24): Bit 7 of the control byte is uniformly optional for all data types. Earlier wording said Bit 7 "must be set" for `bcAbsTimeStampData` with `string` / `binary`; the new rule is type-agnostic. Saves four bytes per single-sample block.
- Rust `osf-core`: the OSF5 writer no longer appends the trailing `0x00`; `binary_write`'s `write_string_with_terminator` / `write_binary_with_terminator` helpers are removed. `BlockReader` gains an `osf_version` field derived from the metablock and threads it through `parse_abs_timestamp_string_or_binary`; the new `strip_osf4_terminator` replaces the version-agnostic `strip_trailing_nul`. Reader-test surface grew from 22 to 24 unit tests via a 2 → 4 split into OSF5- and OSF4-pinned cases per string / binary path. 122 unit + 16 integration + 1 doc test pass; `cargo clippy --all-targets` clean.
- Delphi `OSF.Filer` + `OSF.Data.Manager`: same version-deterministic refactor. `TOSFFile` writer appends `0x00` only when `FVersion = osvOSF4`; `TOSFDataManager.DecodeAbsTimestampedBlock` strips the last byte only on OSF4 input. New `TOSFFile.TruncationSeen: Boolean` property replaces the string-sniffing hack that the manager and meta-cache builder used to detect truncation.
- C++ `osf::BlockReader`: same refactor on the reader side (writer arrives in Phase 7). `osf_version_` field derived from `meta.file_info.version`; `strip_osf4_terminator` replaces `strip_trailing_nul`; per-sample-size sanity check is now version-aware. Reader-test surface grew from 22 to 24 unit tests via the same 2 → 4 split; **157/157 ctest passes** in ~5.5 s.
- Reference files in `examples/generated/`: all 17 files regenerated against the new rule. OSF4 files are structurally identical (size unchanged; only metadata jitter); OSF5 files with `string` / `binary` channels shrink by exactly 100 bytes each (100 samples × 1 byte terminator removed): `osf5_timestamped_string.osf`, `osf5_timestamped_binary.osf`, `osf5_mixed_extended.osf`. The other six OSF5 files are size-unchanged.
- Delphi `TOSFMerger`: removed the per-class `WriteTimestampedBlock` path for multi-sample variable-length blocks. The historical Delphi-only layout (per-sample `uint32` length prefix) was non-spec and the Rust / C++ reference readers never parsed it. The writer now auto-splits N>1 string / binary calls into N single-sample blocks; the reader logs a warning and skips any such block it encounters. `BACKLOG.md` entry tracking the issue removed.
- Delphi logging + progress: replaced the per-class `OnLog` event chain and the standalone `OSF.Progress.IProgressReporter` subsystem (with its eight implementation units) with a single process-wide `TOSFLog` instance, exposed as the global variable `Logger`. Any number of caller-owned `TLoggerListener` instances can register; each filters by its own `MinLevel` and decides what to do with the events (console output, progress bar, JSON-Lines, file append, GUI memo, ...). `TOSFLogLevel` reordered to `(llDebug, llInfo, llUser, llWarning, llError)` — verbosity-ascending — with the new `llUser` level as the default listener filter for user-facing CLI / GUI output. Architecture documented in `DECISIONS.md` §22.
- osftool: `Cmd.Base` rewritten with listener setup / teardown. Each command registers a console listener (filter level driven by `--quiet` / `--verbose` / `--json`) and optionally a file listener (`--log <path>`, always at `llDebug`). The JSON event schema for `--json` is now generic (`{event: log/progress_start/progress/progress_end, level, msg, sender, value, max}`) — replaces the merge-specific schema from `OSF.Progress.Json`.
- `STATUS.md` and `BACKLOG.md` refreshed accordingly. `DECISIONS.md` §16 (specification revision) and §21 (Java implementation) align with the new version-deterministic null-terminator rule.

### Added

- `Console.ProgressBar` in `implementations/delphi/src/console/` — schlanke, OSF-agnostische CLI in-place progress bar (`TConsoleProgressBar` with `Start` / `Update` / `Finish`). Picks between an in-place ANSI bar on an interactive TTY and throttled plain `Progress: N% (i/m)` lines on redirected stdout. Used by osftool through `Cmd.Base`'s default listener callbacks.
- `OSFGeneratorCLI` in `implementations/delphi/demos/osfgenerator/` — console companion to the existing VCL `OSFGenerator` GUI. `OSFGeneratorCLI [output-dir] [samples-per-channel]`; defaults match the GUI (samples = 100). Allows CI-style regeneration of the 17-file reference set without GUI interaction.
- `DECISIONS.md` §22 — Delphi logging + progress architecture.

### Removed

- The `OSF.Progress.*` Delphi unit family superseded by the `TOSFLog` listener pattern: `OSF.Progress.pas`, `OSF.Progress.Console.pas`, `OSF.Progress.Fallback.pas`, `OSF.Progress.Json.pas`, `OSF.Progress.Live.pas`, `OSF.Progress.LogFile.pas`, `OSF.Progress.Quiet.pas`, `OSF.Progress.Verbose.pas`. Net loss after the consolidation: ~621 LOC across the Delphi tree.
- `BACKLOG.md` entries: "Delphi-Writer multi-sample string/binary uses non-spec layout" (resolved by the writer auto-split) and the entire "Spec Extensions / Future Format Revisions" section with the OSF6 null-terminator entry (obsoleted by the version-deterministic rule).

### Notes

- Pre-rev-2026-05-24 OSF5 files with a trailing `0x00` in `string` / `binary` payloads become technically non-conforming under the new rule: the byte will appear as a data byte on read. This is the intentional cost of removing the strip-if-present heuristic; the `examples/generated/` set has been regenerated to comply.

---

## [0.9.0] — 2026-05-20

### Added

- osftool `merge` — a live progress display. By default the verb now shows a per-phase header, the file currently being read and a redrawn progress bar instead of the per-channel log flood; error lines stay pinned permanently above the bar. New output flags select the presentation: `-q` / `--quiet` (errors only, on stderr; silent on success), `-v` / `--verbose` (the full classic log scroll, no live bar), `--json` (a machine-readable JSON-Lines event stream) and `--log <path>` (a complete diagnostic log of every level written to a file, orthogonal to the console mode). `--quiet`, `--verbose` and `--json` are mutually exclusive. When stdout is redirected to a pipe or file the live display falls back to periodic plain progress lines. The feature is built on a new reusable, OSF-agnostic `IProgressReporter` abstraction with six implementations (`OSF.Progress`, `OSF.Progress.{Console,Quiet,Verbose,Json,Fallback,Live,LogFile}`); `TOSFMerger` gained an optional `Reporter` hook that emits structured phase events while the merge algorithm itself is unchanged.
- osftool `--version` / `-V` — prints the tool version and build timestamp; `--version --short` prints just the version number. The version is now a single source of truth in the new `OSF.Version` unit (osftool 1.1.0).

### Changed

- osftool sets the Windows console and the RTL text files to the UTF-8 code page at startup, so non-ASCII output (channel units, the merge progress bar's block glyphs) renders correctly regardless of the machine's legacy code page.

### Fixed

- osftool — replaced em-dash (U+2014) literals in user-facing strings (the banner, the `info` / `channels` / `config` output and the OSF library log/error messages) with ASCII hyphens. Compiled from a source file saved without a UTF-8 BOM these produced mojibake such as `osftool â?"` in the banner.

---

## [0.8.0] — 2026-05-20

### Added

- HDF5 export for the Delphi implementation. `osftool export --format hdf5` renders a loaded OSF file as an HDF5 file: every channel becomes one chunked, shuffled and deflated 1-D dataset of compound records `{int64 timestamp_ns; value}`, the hierarchical channel name is split on the namespace separator into an HDF5 group path, file-level metadata lands in root attributes and per-channel metadata in dataset attributes. Datatype coverage: `bool`, `int8`/`16`/`32`/`64`, `uint8`/`16`/`32`/`64`, `float`, `double`, `gpslocation` (a `{latitude;longitude;altitude}` sub-compound) and `string` (an HDF5 variable-length UTF-8 string); `binary` channels are skipped for now. New `export` flags: `--chunk-size`, `--deflate-level`, `--no-shuffle`, `--namespace-sep`, `--hdf5-lib-dir`.
- Language-agnostic HDF5 format infrastructure under `dataformats/hdf5/` — the OSF→HDF5 mapping specification (`SPEC.md`), the HDF5 DLL-binding knowledge base (`WISSENSBASIS.md`), and `install-hdf5.ps1` / `install-hdf5.sh`, which fetch the official HDF Group runtime (HDF5 1.14.4-3) from GitHub. Binary DLLs are never committed.
- Delphi HDF5 binding units under `implementations/delphi/src/hdf5/` — `Hdf5.Types`, `Hdf5.Api` (dynamic `hdf5.dll` loading with a six-stage resolver and the mandatory `H5open`-first initialisation order with `_g`-global readout) and `Hdf5.Wrapper` (idiomatic RAII handle classes), a reusable OSF-agnostic binding to the HDF5 C library. The new `OSF.Export.HDF5` unit (`TOSFHDF5Exporter`, a `TOSFExporter` subclass) builds on them.
- Inno Setup installer for osftool at `implementations/delphi/setup/osftool.iss` — packages `OsfTool.exe` together with the HDF5 runtime (`hdf5.dll` plus the bundled MSVC redistributable DLLs), adds the install directory to PATH, and lets the user choose an all-users or per-user install.

### Notes

- The HDF5 export and its binding units are Windows-only; they compile to empty units on other platforms, so the cross-platform osftool build is unaffected. zlib is statically linked into the bundled `hdf5.dll`, so no separate `zlib.dll` is needed.

---

## [0.7.0] — 2026-05-20

### Added

- Documentation: new `tools/` chapter under `docs/en/` and `docs/de/` documenting the `osftool` command-line tool — installation and building from source, general usage, global options and exit codes, the sidecar cache, a full command reference for all nine verbs (`merge`, `export`, `info`, `channels`, `stat`, `cache`, `config`, `convert`, `verify`), and the configuration file. Available in English and German.

### Changed

- Relicensed the entire project from the Apache License 2.0 to the MIT License. The `LICENSE` file, every source-file header (Delphi, Rust, C++), package metadata (`Cargo.toml`, `pyproject.toml`), and documentation were updated accordingly. Vendored third-party code under `implementations/cpp/third_party/` keeps its own upstream licenses (`tl::expected` CC0-1.0, `nlohmann/json` MIT). The `[0.1.0]` entry below is left intact as a historical record — that release did ship under Apache 2.0.
- Delphi CLI: the `osftool merge` verb's interval bounds are now optional named flags `--start` / `--end` instead of the required third and fourth positionals. Positionals are reduced to `<rootdir> <outputfile> [channel ...]`. Each bound defaults independently — `--start` to `1970-01-01`, `--end` to the current time (UTC) — and a flag, when given, overrides only that one bound; `osftool merge <rootdir> <outputfile>` with no interval flags merges the full available range. An invalid flag value is now reported explicitly instead of being mistaken for a channel name.

---

## [0.6.0] — 2026-05-19

### Added

- C++ implementation: Phase 3 OSF5 JSON metablock parser landed. Public API: `osf::DataType` / `osf::ChannelType` / `osf::SpectrumType` enums (spec rev 2026-05-04 datatype set: `pair` / `triple` / `candata` removed, `gpsdata` → `gpslocation`, unsigned-int datatypes added), `osf::FileInfo` / `osf::Channel` / `osf::Info` / `osf::MetaBlock` structs, `osf::parse_metablock_json` in two overloads (`std::uint8_t const*` + size, `std::string_view`), plus `osf::parse_data_type` / `osf::parse_channel_type` / `osf::parse_spectrum_type` type-string helpers. Implementation in `src/metablock.cpp` translates the Rust reference (`implementations/rust/osf-core/src/meta_json.rs` + `meta.rs`) idiomatically: `nlohmann::json::parse(..., allow_exceptions=false)` keeps the core API exception-free, deprecated channel fields are tolerated silently, the OSFGenerator-style short geolocation spelling (`latitude=` without `created_at_`) is accepted on read. Three new `Error::Code` values: `InvalidMetablock`, `RemovedInSpec`, `JsonParseError`. Vendored `nlohmann/json` v3.11.3 (single-header form, MIT) under `third_party/nlohmann-json/`. Test suite extended from 25 to 57 cases: 9 type-parser unit tests, 20 metablock-parser unit tests, 3 integration tests against the OSF5 reference files in `examples/generated/`.

### Notes

- Per-package release notes for the C++ library are in [`implementations/cpp/CHANGELOG.md`](implementations/cpp/CHANGELOG.md). Repo and per-package version lines remain explicitly decoupled: repo at 0.6.0, cpp library at 0.0.3.

---

## [0.5.0] — 2026-05-10

### Added

- C++ implementation: Phase 2 magic-header parser landed. Public API: `osf::OsfVersion`, `osf::MagicHeader`, three `parse_magic_header` overloads (`std::istream&`, `std::uint8_t const*` + size, `std::filesystem::path`), `osf::MAX_MAGIC_HEADER_LEN` constant. Implementation in `src/header.cpp` follows the Rust reference (`implementations/rust/osf-core/src/header.rs`) idiomatically: byte-by-byte stream reading, `std::from_chars` for the length parse, CRLF tolerance, accepts the four identifier spellings (`OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`). Three new `Error::Code` values: `InvalidMagicHeader`, `UnsupportedVersion`, `MagicHeaderTooLong`. Test suite extended from 5 to 25 cases: 16 unit tests against synthetic byte sequences, 4 integration tests against the reference files in `examples/` (the last one internally iterates over 17 generated files in `examples/generated/`).

### Notes

- Per-package release notes for the C++ library are in [`implementations/cpp/CHANGELOG.md`](implementations/cpp/CHANGELOG.md). Repo and per-package version lines remain explicitly decoupled: repo at 0.5.0, cpp library at 0.0.2.

---

## [0.4.0] — 2026-05-08

### Added

- C++ implementation: Phase 1 skeleton landed. CMake build (C++17 hard-pinned, two targets `osf::osf` static and `osf::headers` interface), vendored `tl::expected` v1.3.1 for `Result<T>` foundation type, `osf::Error` and `osf::Result<T>` as the public error-handling API, GoogleTest integration via `FetchContent` (v1.15.2, SHA256-pinned), five-test smoke suite covering Error, Result, version, and `error_category_name`. Phase 1 documentation: `implementations/cpp/README.md`, `BUILD.md` (per-platform + FAQ), `CHANGELOG.md`. See [DECISIONS §20](DECISIONS.md#20-c-implementation-architecture).
- DECISIONS §20: C++ implementation architecture documented (standalone C++17, parallel to the Rust core; revises §15 priority order; eleven-phase implementation roadmap; C ABI deferred to its own future DECISIONS entry).

### Notes

- Per-package release notes for the C++ library are in [`implementations/cpp/CHANGELOG.md`](implementations/cpp/CHANGELOG.md). Repo and per-package version lines remain explicitly decoupled: repo at 0.4.0, cpp library at 0.0.1.

---

## [0.3.0] — 2026-05-07

### Added

- First Python implementation (`osfdata` package), pre-released on TestPyPI as v0.1.0.
- GitHub Actions CI pipeline building wheels for four platforms (Linux x86_64 and aarch64, macOS arm64, Windows x86_64).
- Trusted Publishing configured for TestPyPI uploads.
- BUILD.md documenting the toolchain and release process for the Python package.
- Python integration page in docs/de/integrations/ and docs/en/integrations/.
- Per-package changelog file at `implementations/python/CHANGELOG.md`.

### Changed

- Repository transferred from `burkhard154/osf` to `optimeas/osf`.
- OSFZ encoding clarified in DECISIONS.md §12: real-world devices write gzip (RFC 1952), not only zlib (RFC 1950); both formats now accepted on read.

### Notes

- Per-package release notes for `osfdata` are in [`implementations/python/CHANGELOG.md`](implementations/python/CHANGELOG.md). Future language implementations will follow the same pattern.

---

## [0.2.0] — 2026-05-05

### Changed

- Specification: removed `scale` and `offset` channel parameters.
- Specification: removed `physicalunit1`, `physicalunit2`, `physicalunit3` and `physicaldimension1`, `physicaldimension2`, `physicaldimension3`.
- Specification: removed the data types `pair`, `triple`, and `candata`.
- Specification: renamed `gpsdata` to `gpslocation`. Field order corrected to `latitude`, `longitude`, `altitude`.
- Specification: `bcStartData` now carries the sample rate as `double` (applies to OSF4 and OSF5). Multiple `bcStartData` blocks per channel are explicitly supported.
- Specification: clarified that `string` and `binary` payloads in `bcAbsTimeStampData` are null-terminated for both OSF4 and OSF5 (existing behavior, now formalized). Readers must strip the trailing null byte before further processing.
- Documentation: split into `docs/de/` (German) and `docs/en/` (English, default), expanded with `index`, `examples/`, and `references/` subsections.
- Documentation: list both `OCEAN_STREAM_FORMAT4` and `OCEAN_STREAMING_FORMAT4` as legacy OSF4 magic-header identifiers across all spec documents.

### Added

- Specification: unsigned integer datatypes `uint8`, `uint16`, `uint32`, `uint64`.
- Specification: `bytearray` documented as alias for `binary` on the read side.
- Delphi implementation: support for the spec revision above, including null-terminated strings/binary, sample-rate field on `bcStartData`, and multi-segment equidistant channels exposed via `TOSFEquidistantDataChannel.Segments`.
- Delphi demo: `OSFGenerator` — VCL application that writes a suite of sample `.osf` files (one per data-type group) for both OSF4 and OSF5.
- Delphi demo: `OSFCSVExport` — VCL application that loads any OSF file via `TOSFDataManager` and exports all channels through `TOSFCSVExporter`.
- Examples: real field-data samples `examples/motorbike.osf` and `examples/steam_loco.osf` (with a `.csv` reference of the latter).

---

## [0.1.0] — 2026

### Added

- Initial repository structure with `docs/`, `implementations/`, `integrations/`, and `examples/` directories.
- Placeholder specification documents for OSF general concepts, OSF4, and OSF5.
- `README.md` files for all planned language implementations and ecosystem integrations.
- Delphi implementation — in progress (reader and writer for OSF4 and OSF5).
- Apache 2.0 license, `CONTRIBUTING.md`, and `CHANGELOG.md`.
