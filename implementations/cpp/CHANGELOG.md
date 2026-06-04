# Changelog

All notable changes to the C++ implementation of OSF will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Phase 11 — C ABI wrapper (`osf-c`).** The final §20 phase: a separate
  **shared** library exposing the C++ core through a pure-C99 `extern "C"`
  surface for cross-language consumption (Windows DLL / ActiveX-OCX,
  future bindings). Built only when `OSF_BUILD_C_API=ON` (default OFF);
  see DECISIONS §23 for the full contract.
  - `include/osf/c_api.h` — opaque `osf_manager` (owns a `DataManager`) +
    borrowed `osf_channel` handles; `osf_status` codes mirroring
    `osf::Error::Code` (`OSF_OK == 0`); thread-local
    `osf_last_error_message()`. Read path: `osf_load_file`, channel
    enumeration + metadata, **caller-buffer copy-out** readers
    (`osf_channel_read_timestamps` / `read_f64` / `read_i64` / `read_gps`,
    reconstructing equidistant timestamps), borrowed `string_at` /
    `binary_at`. Round-trip `osf_write_to_file` (always OSF5).
    `OSF_C_API` export macro (`__declspec(dllexport/import)` on Windows;
    `visibility("default")` elsewhere).
  - `src/c_api.cpp` — thin adapter over `DataManager` + the `BlockWriter`
    `write_to_file` convenience; every entry point `try/catch`-wrapped so
    no C++ exception crosses the ABI.
  - `tests/c_api/test_c_api.c` — a standalone **C99** program (proves
    C-compatibility + DLL linkage): load, enumerate, read, round-trip
    write + reload, error path. Registered as ctest `c_api`.
  - CI builds the shared `osf-c` and runs the C test on
    ubuntu/macos/windows (`-D OSF_BUILD_C_API=ON`). Two CMake fixes were
    needed for the cross-compiler legs: `enable_language(C)` (single-config
    generators don't auto-enable C) and `CMAKE_POSITION_INDEPENDENT_CODE`
    (fold the static core into the shared lib on ELF/Mach-O).
  ctest 304 → **305/305 green** with `OSF_BUILD_C_API=ON`, 0 warnings
  under `/W4 /permissive-` (`/WX`). **This completes the §20
  Implementation Order (phases 1–11).**
- **Phase 10 — CI integration.** The C++ implementation now builds and
  tests on every change via GitHub Actions (DECISIONS §20). `.github/workflows/ci.yml`
  gains `implementations/cpp/**` in its push + pull_request path filters
  (C++ changes previously triggered no CI run) and a `test-cpp` job that
  configures, builds, and runs ctest across a **ubuntu-latest / macos-14
  / windows-latest** matrix, gating the `summary` job. The build runs
  with **warnings-as-errors** via the new opt-in CMake option
  `OSF_WARNINGS_AS_ERRORS` (default OFF; CI sets it ON), wired into
  `osf_set_warnings` as `/WX` (MSVC) / `-Werror` (GCC/Clang/AppleClang).
  This is the first time the code is compiled under GCC and AppleClang
  (it had only ever been MSVC-built); two warnings-as-errors hits were
  cleared in the process — a dead Float/Double runtime check in
  `block_writer.cpp` (MSVC C4127, replaced with a `static_assert`) and an
  unused test helper in `test_manager.cpp` (`-Werror=unused-function`).
  All three OS legs green (304/304 ctest each).

### Changed

- New CMake option `OSF_WARNINGS_AS_ERRORS` (default OFF) — promotes
  compiler warnings to errors for OSF targets only (vendored `pugixml.cpp`
  and the FetchContent `zlib` / `googletest` targets are unaffected).
  Local dev builds stay lenient; CI enables it.

- **Phase 9 — throwing convenience layer** at
  `include/osf/throwing.hpp` (header-only, opt-in). Exposes the
  `Result`-based core API as exception-throwing functions for consumers
  who prefer RAII-style error propagation, per DECISIONS §20:
  - `osf::Exception : std::runtime_error` — carries the `osf::Error`;
    `what()` is the error message (or the stable category name when
    empty); `code()` / `error()` expose the structured detail. In
    namespace `osf`.
  - `osf::throwing::unwrap(Result<T>)` — returns the value or throws
    `osf::Exception`. Works on **any** core `Result`, including the writer
    methods (`unwrap(w.start())`, `unwrap(w.add_channel(def))`), which
    keeps the layer thin — no per-method writer wrappers.
  - `osf::throwing::load(path)` / `load(std::istream&)` → `DataManager`;
    `osf::throwing::write_to_file(mgr, path)` / `write_to(mgr, ostream&)`
    → `void` (OSF5, DECISIONS §6).
  Header-only and **not** part of the `osf/osf.hpp` umbrella and **not**
  compiled into the `osf` library — consumers who never include it pull
  in no extra machinery. 10 new GoogleTest cases bring the C++ ctest
  count from 294 to **304/304 green**, 0 warnings under MSVC
  `/W4 /permissive-`. Phase 10 (CI integration) is next.
- **Phase 8 — transparent OSFZ decompression on read** at
  `include/osf/compression.hpp` / `src/compression.cpp`. Removes the
  `DataManager` OSFZ-rejection stub: gzip- and zlib-wrapped OSF files now
  load transparently (deployed optiMEAS devices emit gzip-OSFZ —
  `weather_station.osfz`, the Train OSFZ field recordings; older tooling
  used raw zlib). Mirrors the Rust `compression` module
  (`detect_and_wrap` / `MaybeCompressed<R>`):
  - `osf::DecompressingIStream` — a `std::istream` over a source stream
    that classifies by the leading two bytes (gzip `0x1F 0x8B`, zlib
    `0x78 {01,5E,9C,DA}`, else plain) and **inflates on demand** via a
    custom `std::streambuf` (constant-memory streaming, no whole-file
    buffering). Auto gzip/zlib header detection via
    `inflateInit2(MAX_WBITS | 32)`; best-effort EOF on truncation. The
    `z_stream` is hidden behind a PIMPL so the public header stays
    zlib-free. Plus the non-consuming `detect_compression(std::istream&)`.
  - `DataManager::load_from_file` / `load_from_stream` wrap their input in
    a `DecompressingIStream` before the magic-header parse and populate
    `ReaderStats::compressed` + `compression_format`. The low-level
    `parse_magic_header` deliberately stays non-decompressing.
  - zlib provisioning honours the declared `OSF_USE_SYSTEM_ZLIB` option:
    default fetches zlib **1.3.1** via FetchContent (pinned tarball +
    SHA256); `ON` uses `find_package(ZLIB)`. zlib is a PRIVATE dependency
    of `osf_core`.
  `tests/unit/test_compression.cpp` (detection + round-trips incl. a
  256 KiB multi-chunk case) and `tests/integration/test_compression_examples.cpp`
  (gzip+zlib re-wrap of `steam_loco.osf` matches plain via
  `roundtrip_managers_equal`; `weather_station.osfz` loads). ctest 283 →
  **294/294 green**, 0 warnings under MSVC `/W4 /permissive-`. Phase 9
  (throwing convenience layer) is next.
- **Phase 7d — `StaleValueGuard`** (optional freshness layer) at
  `include/osf/stale_value_guard.hpp` / `src/stale_value_guard.cpp`.
  Re-emits the last value of idle timestamped channels so their on-disk
  trace stays "fresh" (the optiMEAS 100-second-repeat convention), which
  disambiguates *channel still at this value* from *recording stopped*.
  No Rust/Delphi reference — from-scratch C++ design:
  - Write-through wrapper around a caller-owned `StreamingWriter`:
    forwards each timestamped write and, on success, caches the channel's
    last `(timestamp, value)`. Decoupled — owns no file handle, never
    touches writer internals.
  - Pull-based `poll(now_ns)` re-emits the cached value of any channel
    idle `>= repeat_interval_ns` (default `100'000'000'000` = 100 s),
    stamped at `now_ns`, **at most once per poll** (keeps the trace fresh,
    no backfill). No internal clock, no background thread — deterministic
    and embedded-friendly.
  - Numeric (11 types) + `GpsLocation` only; `string` / `binary` excluded
    by design. Channels auto-track on first successful write-through;
    channel-type validation is delegated to the writer. Control surface:
    `is_tracked` / `forget` / `clear` / `repeat_interval_ns`.
  Header-defined class (in-header numeric template bodies; GPS writes,
  `poll`, `reemit` via `std::visit`, and control methods in the `.cpp`).
  12 new GoogleTest cases bring the C++ ctest count from 271 to
  **283/283 green**, 0 warnings under MSVC `/W4 /permissive-`. The
  two-writer OSF5 write surface of DECISIONS §7 plus this guard complete
  Phase 7; Phase 8 (transparent OSFZ decompression on read) is next.
- **Phase 7c — `BlockWriter`** (analyst-style OSF5 writer) at
  `include/osf/block_writer.hpp` / `src/block_writer.cpp`. Accumulates
  every channel kind in memory and emits the complete file at
  `write_to_file(path)` / `write_to(std::ostream&)` (const /
  non-consuming — a builder can be emitted to several sinks). Mirrors
  the `StreamingWriter` template surface:
  - `add_equidistant_segment` (`float` / `double`), the
    `add_timestamped_sample<T>` / `add_timestamped_samples<T>`
    template over 11 numeric types, non-template GPS
    (`add_timestamped_gps_sample/samples`), and single-sample
    `add_string_*` / `add_binary_*`.
  - **Variable `sizeoflengthvalue` auto-bump 2 → 4** before the
    metablock is written (the structural asymmetry to `StreamingWriter`,
    which cannot bump — its metablock is already on disk). Mirrors the
    Rust `writer.rs` autobump; strict `>` boundary (a 65526-byte sample
    stays sov=2, 65527 bumps).
  - `BlockWriter::from_manager(DataManager const&)` plus free
    `osf::write_to_file` / `osf::write_to(DataManager const&, …)` for the
    round-trip / copy workflow — always OSF5 even from an OSF4 source
    (DECISIONS §6). Mirrors the Rust `from_manager` path.
  Per-channel storage is a `ChannelData` variant
  (`Empty / Equidistant / Timestamped / Variable`), the C++ mirror of
  Rust's `WriterBuilder::ChannelData`.
- **`src/writer_common.{hpp,cpp}`** — private writer infrastructure
  shared by both `StreamingWriter` and `BlockWriter`: the block-payload
  chunking helpers (`max_payload_for_sov`, `max_samples_per_*_block`,
  `variable_sample_capacity`), the sizing constants `GPS_WIRE_SIZE = 24`
  / `VARIABLE_BLOCK_OVERHEAD_BYTES = 9`, and `build_metablock`
  (extracted from `StreamingWriter::start()`'s inline assembly).
- **`tests/integration/roundtrip_helper.hpp`** — a shared
  `roundtrip_managers_equal` comparing two loaded `DataManager`s by
  channel count + per-channel name / datatype / sample-count **and
  first/last materialised sample value**. Used by both the new
  `test_block_writer_examples.cpp` (round-trips every `osf5_*.osf`
  through `from_manager`) and the refactored
  `test_streaming_writer_examples.cpp` (closing the prior
  count+datatype-only coverage gap).
- **New unit tests** `tests/unit/test_block_writer.cpp` (builder
  mechanics, all write families, auto-bump, from_manager) and
  `tests/unit/test_writer_common.cpp` (metablock channeltype
  normalisation), plus three `StreamingWriter` lifecycle tests
  (double-close, move-construct, self-move). Total ctest **245 → 271
  green**, 0 warnings under `/W4 /permissive-`.

### Changed

- **`channeltype` normalisation in `build_metablock`.** Both writers
  now emit `channeltype: scalar` for every non-equidistant channel
  (timestamped numeric, GPS, string, binary) and `equidistant` only for
  equidistant channels, matching the Delphi reference generator. Folds
  the parked BACKLOG Task-7 #2 nit; `StreamingWriter` output converges
  to the reference on this field (round-trip behaviour unchanged — the
  reader keys non-equidistant channels off `datatype`).
- **18 parked Phase-7b polish nits folded in** alongside Phase 7c
  (the four `### C++ StreamingWriter … polish (post-Phase-7b)` BACKLOG
  entries): `MAX_PAYLOAD_FOR_SOV` → `max_payload_for_sov`, the two
  sizing constants promoted, `make_double_channel` split into
  scalar/equidistant test helpers, `sov_for` assert, `start()`
  Broken-state error surfacing, tightened long-run chunking assertions,
  and stale-comment / truncation-math cleanups. A strict-aliasing UB in
  the `BlockWriter` `std::vector<bool>` emit path (a `reinterpret_cast`
  to `bool const*`) was caught in review and fixed with a genuine
  `bool[]` materialisation.
- **Reader-comment correction in `src/reader.cpp`.** The note in
  `parse_abs_ts_string_or_binary` that said *"per spec bit 7 should
  be set; we tolerate clear bit as implicit N=1"* misread the spec.
  Both bit-7 forms are valid; the canonical compact form for a
  single sample is bit-7 = 0 with no `[u32 N]` prefix (saves four
  bytes vs. the bit-7 = 1 + `u32 N=1` variant). The Phase 7a encoder
  and the (now-fixed) Rust writer both emit the canonical form; the
  reader still accepts either. Documentation-only — wire-format
  behaviour unchanged; 192/192 ctest still green.

### Added

- **Phase 7b — `StreamingWriter`** (embedded streaming OSF5
  writer) at `include/osf/streaming_writer.hpp` /
  `src/streaming_writer.cpp` (764 lines). Public API ships
  all four write families per Spec §2 Q4/Q5/Q6 + §3.3:
  - **Equidistant** (Task 4): `start_equidistant_segment` +
    `append_equidistant_samples` for `float` + `double` per
    spec rev 2026-05-04. Two private template `_impl<T>`
    bodies + 4 explicit instantiations. Three constexpr
    chunking helpers in the anonymous namespace
    (`MAX_PAYLOAD_FOR_SOV`, `max_samples_per_start_block` with
    OVERHEAD=21, `max_samples_per_continued_block` with
    OVERHEAD=5).
  - **Timestamped numeric** (Task 5): `write_timestamped_sample<T>`
    and `write_timestamped_samples<T>` templates with
    `static_assert(IsTimestampedNumeric<T>::value, ...)` over
    11 types (bool, int8/16/32/64, uint8/16/32/64, float,
    double). Reuses `max_samples_per_timestamped_block` from
    Task 4.
  - **GPS** (Task 6): separate non-template
    `write_timestamped_gps_sample` (scalar forwards to array)
    and `write_timestamped_gps_samples` (array with chunking
    via `max_samples_per_timestamped_block(/*value_size=*/24u,
    sov)`).
  - **Variable** (Task 6): `write_timestamped_string` and
    `write_timestamped_binary` — single-sample only per OSF
    spec rev 2026-05-24. New anonymous-namespace helper
    `variable_sample_capacity(sov)` for the pre-encoder
    capacity check; error message quotes exact effective
    capacity per Spec §3.3 (`65526 bytes` for sov=2).
  Power-loss safety via per-block `DurableFile::force()`
  (Task 1, `FlushFileBuffers` on Windows, `fsync` on POSIX);
  reader's existing best-effort-on-truncation behaviour means
  the file remains readable up to the last successfully
  fsync'd block.
- **`DurableFile`** at `src/durable_file.{hpp,cpp}` (Task 1) —
  RAII wrapper around the OS file handle with `write` /
  `force` / `close`; cross-platform via `_Windows` /
  `_Posix` builders.
- **`serialize_metablock_json`** at `src/metablock.cpp`
  (Task 2) — the inverse of `parse_metablock_json`; emits
  the OSF5 metablock JSON the `StreamingWriter` writes in
  `start()`. Round-trip pinned via 14 new tests.
- **`BinarySample`** promoted to public header
  `include/osf/binary_sample.hpp` (Task 0) — was private to
  the Phase-7a encoder; the new `StreamingWriter` API needs
  it on the public surface for the
  `write_timestamped_binary(channel, ts, BinarySample)`
  signature.
- **Cross-implementation roundtrip integration test**
  (Task 7) at
  `tests/integration/test_streaming_writer_examples.cpp` (347
  lines). Three Cat-F roundtrips on
  `examples/generated/osf5_{equidistant,scalar_numeric,
  mixed_extended}.osf` — load via `DataManager`, re-write
  through `StreamingWriter`, reload, assert per-channel
  count + datatype equality. One Cat-G truncation regression
  via `std::filesystem::resize_file` cutting the last block
  mid-frame; reader recovers 9 of 10 blocks and bumps
  `stats.blocks_truncated` to 1.
- **53 new GoogleTest cases** across Tasks 1-7 (DurableFile,
  serialize_metablock_json, StreamingWriter skeleton +
  equidistant + timestamped numeric + GPS + variable +
  integration). Total ctest count moves from 192/192 to
  **245/245 green** in ~10 s under MSVC `/W4 /permissive-`,
  0 build warnings.
- **Phase 7c (`BlockWriter`) and Phase 7d (`StaleValueGuard`)**
  arrive next on the same encoder substrate. 18 minor polish
  items parked across four BACKLOG entries
  (`### C++ StreamingWriter ... polish (post-Phase-7b)`),
  scheduled to land as a single tightening pass on top of
  Phase 7c since the chunking helpers + encoder symbols
  + roundtrip helper shape are all reused there.

- **Phase 7a — Private block-encoder layer.** Six encoder symbols
  in `src/block_encode.hpp` / `.cpp`, namespace `osf::detail`,
  composed by the future `StreamingWriter` (Phase 7b) and
  `BlockWriter` (Phase 7c). All return `Result<void>` with three
  documented error conditions (count==0, `sizeoflengthvalue` ∉ {2,4},
  oversize payload):
  - `encode_start_data<T>` (template, instantiated for `float` /
    `double` per spec rev 2026-05-04 equidistant restriction)
  - `encode_continued_data<T>` (template, same instantiations)
  - `encode_abs_timestamp_data<T>` (template, 11 numeric
    instantiations: `bool`, `int8` / `int16` / `int32` / `int64`,
    `uint8` / `uint16` / `uint32` / `uint64`, `float`, `double`)
  - `encode_abs_timestamp_data_gps` (plain function, separate
    non-template symbol per the Q3.5 architecture decision —
    keeps the GPS 32-byte-per-sample layout greppable)
  - `encode_abs_timestamp_data(... std::string_view)` (single-sample
    overload; bit-7=0, no `uint32 N`-prefix, **no trailing `0x00`**
    per spec rev 2026-05-24 OSF5 rule)
  - `encode_abs_timestamp_data(... BinarySample)` (single-sample
    overload; same wire-format rules as the string overload).
    `BinarySample` is a non-owning view struct with explicit
    constructor + `from_vector` factory — the C++17 substitute
    for `std::span` with the lifetime trap of implicit-from-vector
    blocked.
  Bit-7 selection automatic by `count` (=0 for `count==1`, saves
  4 bytes; =1 for `count>1` with `uint32 N`-prefix). Control
  bytes match `block.hpp` enum: 5 = `bcContinuedData`, 6 =
  `bcStartData`, 8 = `bcAbsTimeStampData`. The encoder never
  emits 0x07 (`bcContinuedRelStampData` is read-only deprecated).
- **`src/binary_io.hpp` — shared LE byte-helper hub** in
  `osf::detail`. 9 read helpers (`read_le_u16` / `_u32` / `_u64`
  / `_i8` / `_i16` / `_i32` / `_i64` / `_f32` / `_f64`) plus the 9
  matching `write_le_*` counterparts. The 8 pre-existing reader
  helpers moved out of `src/reader.cpp`'s anonymous namespace and
  were renamed for write-side symmetry; `read_le_i8` is new (no
  current reader call site but added so the read/write pair is
  complete). Mirror of Rust's `binary_write.rs`. Integer helpers
  use manual byte shifts; float helpers use `std::memcpy` to
  avoid strict-aliasing UB.
- **`tests/unit/test_block_encode.cpp`** — 35 new GoogleTest
  cases over the encoder symbols: 2 `BinarySample` smoke tests +
  10 equidistant tests (byte-exact frame layout, bit-7 toggle,
  error paths including the exact-boundary oversize trip,
  roundtrip via `BlockReader`) + 14 timestamped numeric tests
  (single-sample byte-exact, multi-sample byte-exact, roundtrip
  for all 11 numeric types in single + multi configurations) +
  4 GPS tests + 9 variable-length tests including a
  payload-with-embedded-`0x00` preservation check. Roundtrip
  tests for `float` and `double` use `EXPECT_FLOAT_EQ` /
  `EXPECT_DOUBLE_EQ` matchers (4-ULP tolerance) instead of bare
  `EXPECT_EQ`. Three coverage-extension tests added after the
  final review (encode_continued_data `count==0`,
  `sizeoflengthvalue=4` byte-exact, `channel_index > 255`
  high-byte verify).
- **Two-line SPDX+Copyright header** is now uniform across all
  C++ source / header / test files in `implementations/cpp/`,
  matching the rest of the implementations. 36 pre-existing
  files retrofitted in the same pass; new files inherit the
  convention by default.

### Changed

- `src/CMakeLists.txt`: `src/block_encode.cpp` added to the
  `osf_core` source list (alphabetical between `block.cpp` and
  `data_channel.cpp`).
- `tests/CMakeLists.txt`: new `test_block_encode` executable
  with `target_include_directories(... PRIVATE ../src)` to allow
  the non-public `block_encode.hpp` to be included from the test
  TU.
- `src/reader.cpp`: now `#include "binary_io.hpp"` and calls the
  renamed `osf::detail::read_le_*` helpers. Mechanical refactor;
  all 157 pre-existing tests stay green.

### Notes

- Total ctest count is now **192/192 green** locally (157
  pre-Phase-7a baseline + 35 new). No release tag yet.

## [0.0.6] - 2026-05-23

### Added

- Typed in-memory channel model in `include/osf/data_channel.hpp`:
  `DataChannel` as `std::variant<EquidistantChannel,
  TimestampedChannel, VariableChannel>` (distinct from the
  metablock-level `osf::Channel`, which is the channel *definition*;
  `DataChannel` represents the assembled *samples*).
  `EquidistantChannel` holds flat samples + `std::vector<Segment>`;
  `TimestampedChannel` holds parallel `std::vector<int64>` +
  `NumericValues`; `VariableChannel` holds string XOR binary samples.
  `Segment`, `ChannelMeta`, `NumericValues` (variant per data type +
  `GpsLocation`), `Sample<T>` template, `NumericValueRef` (GPS by
  value, 24 B), `VariableValueRef` (string XOR binary).
- Materializing `samples_vector()` per channel kind that reconstructs
  per-sample timestamps (using the segment math from spec rev
  2026-05-04) and returns a `std::vector<Sample<...>>`.
- Flat-access helpers: `as_doubles_flat` / `as_int32_flat` / ... in
  twelve overloads each for `EquidistantChannel` (returning
  `std::vector<T>`) and `TimestampedChannel` (returning
  `std::vector<std::pair<i64, T>>`). Type mismatch produces
  `Error::Code::DataTypeMismatch`.
- Common-accessor free functions on `DataChannel`: `channel_index`,
  `channel_name`, `channel_data_type`, `channel_physical_unit`,
  `channel_display_name`, `channel_sample_count`, `channel_is_empty`,
  `channel_meta`.
- High-level reader `osf::DataManager` in
  `include/osf/manager.hpp`: `load_from_file(path)` and
  `load_from_stream(istream&)` drive a `BlockReader` to completion
  and assemble the typed channel list. `channel(name)` (mandatory
  per DECISIONS §10) and `channel_by_index(u16)` (optional)
  lookups; `meta` and `stats` are public fields carrying the parsed
  metablock and reader telemetry.
- Internal builder state machine in `src/manager.cpp` mirroring the
  Rust reference: per-channel `ChannelBuilder` with states
  `Pending` / `Equidistant` / `Timestamped` / `Variable` /
  `Unsupported`; numeric channels start `Pending` and lock to
  `Equidistant` on first `bcStartData` or to `Timestamped` on first
  `bcAbsTimeStampData`; mismatched later blocks produce
  `ChannelMixedBlockTypes`. Orphan continuations produce
  `ContinuedDataWithoutStart` / `RelStampWithoutAnchor`.
  `bcContinuedRelStampData` deltas are converted to absolute
  timestamps using the channel's last absolute ts as anchor.
- OSFZ-stub detection: `DataManager::load_from_file` /
  `load_from_stream` peek at the first two bytes and reject
  gzip / zlib magic with a clear `IoError` message pointing to
  Phase 8 — keeps callers from getting a confusing magic-header
  parse failure on a compressed file.
- `tests/unit/test_data_channel.cpp` — 11 tests covering
  `NumericValues` helpers (`data_type`, `empty_for`),
  `samples_vector` for equidistant (single + multi-segment, no
  interpolation between segments), `samples_vector` for
  timestamped, variable string/binary collection, flat-access
  mismatch, common Channel accessors.
- `tests/unit/test_manager.cpp` — 13 tests driving the builder
  through synthetic in-memory OSF5 streams:
  one start + continued = one segment, two starts = two segments,
  start + abs-ts = `ChannelMixedBlockTypes`, continued without
  start = `ContinuedDataWithoutStart`, abs-int32 builds
  Timestamped, rel-stamp extends cumulatively, rel-stamp without
  anchor = `RelStampWithoutAnchor`, variable strings collected,
  Unsupported channel dropped from output, name + index lookups,
  gzip / zlib magic produce the Phase-8 error.
- `tests/integration/test_manager_examples.cpp` — 7 tests against
  the generated reference files plus the field samples: every
  `.osf` under `examples/generated/` loads through
  `load_from_file` and produces at least one channel with
  samples; snapshot probes pin equidistant / GPS / string
  channels on specific files; `motorbike.osf` + `steam_loco.osf`
  load clean; `weather_station.osfz` produces the Phase-8 OSFZ
  error.

### Changed

- `osf_core` library target gains two translation units
  (`src/data_channel.cpp`, `src/manager.cpp`).
- `include/osf/osf.hpp` umbrella re-exports the two new headers.
- `ctest` count: 124 → 153 (existing 124 unchanged; 11 new
  data-channel-unit + 13 new manager-unit + 7 new
  manager-integration; one test from the manager suite is
  intentionally omitted — the `data_type_mismatch` check in the
  builder is defensive against custom block sources and is
  unreachable through `load_from_stream` because the reader
  already typed-decodes payloads).

## [0.0.5] - 2026-05-23

### Added

- Block-stream reader (`osf::BlockReader`). Borrows an `std::istream`
  positioned at the end of the metablock plus the parsed `MetaBlock`,
  iterates the block stream producing typed `osf::Block` values.
  Provides both a primitive `next() -> std::optional<Result<Block>>`
  API and a range-based-for compatible iterator
  (`begin()` / `end()` with an `EndSentinel`).
- Block-model primitives in `include/osf/block.hpp`: `Block`,
  `BlockKind` as `std::variant<StartData, ContinuedData,
  AbsTimestampData, ContinuedRelStampData, Skipped>`, payload variants
  `NumericPayload` / `TimestampedPayload` / `RelTimestampedPayload`
  (one `std::vector<T>` alternative per spec datatype),
  `GpsLocation`, `SkipReason`, `decode_control_byte`,
  `TRAILER_CHANNEL_INDEX`, `MAGIC_TRAILER_LEN`.
- Reader telemetry in `include/osf/stats.hpp`: `ReaderStats` and
  `ChannelStats` with per-channel detail, byte/block counters,
  `time_range_ns`, plus a `CompressionFormat` enum reserved for
  Phase 8. `operator<<` overloads format both structs in the same
  shape as the Rust reference (`File size: …`, `Channels total: …`,
  `blocks=X+Yskipped samples=…`).
- Six new `osf::Error::Code` values: `UnknownChannelIndex`,
  `InvalidBlock`, `ChannelMixedBlockTypes`,
  `ContinuedDataWithoutStart`, `RelStampWithoutAnchor`,
  `DataTypeMismatch`. The last four are reserved for the future
  `DataManager`; the first two surface from the block reader
  itself.
- Best-effort truncation handling: a file that ends mid-block
  bumps `stats().blocks_truncated` from 0 to 1 (capped) and
  iteration ends cleanly. Hard error on unknown channel index
  (the reader can't know the length-prefix width without the
  channel record).
- Forward-compat skipping: channels declared with
  `DataType::Unsupported` or `ChannelType::Unsupported` produce
  `BlockKind::Skipped` records and consume their payload from
  the stream so other channels stay aligned. The optional
  `0xFFFF` info-data block plus 40-byte `OSF_STREAM_END` magic
  trailer are consumed silently.
- Skipped-payload capture is opt-in via
  `with_capture_skipped_payload(true)` — default behaviour drops
  the bytes without allocation.
- `tests/unit/test_block.cpp` — payload `len()` helpers,
  control-byte decoder (every documented value plus multi-sample
  bit + unknown-byte fallback), `GpsLocation` equality, Skipped
  default payload.
- `tests/unit/test_stats.cpp` — `observe_timestamp` two-sided
  growth, `format_bytes` unit thresholds, `format_duration`
  ms/s split, `compression_format_name` mapping, ostream output
  for both structs.
- `tests/unit/test_reader.cpp` — 21 BlockReader tests against
  synthetic byte sequences, direct port of the Rust reader
  suite: empty stream, truncation paths, unknown channel
  (hard error), Unsupported-channel skip with stream alignment,
  capture-skipped opt-in, deprecated control bytes, unknown
  control bytes, every typed parser
  (`bcAbsTimeStampData` for int64/double/string/binary/gps,
  `bcStartData` single + multi for double/float,
  `bcContinuedData` int16,
  `bcContinuedRelStampData` int16),
  `InvalidBlock` for equidistant-on-string, trailer consumption,
  range-based-for iteration.
- `tests/integration/test_reader_examples.cpp` — every
  uncompressed `.osf` under `examples/generated/` streams clean
  end-to-end producing at least one block; snapshot probes on
  `osf5_scalar_int64.osf` (first block is single-sample AbsTs
  Int64) and `osf4_equidistant.osf` (first block is StartData);
  `motorbike.osf` and `steam_loco.osf` field samples read
  through with no hard errors; stats sanity check.

### Changed

- `osf_core` library target gains three translation units
  (`src/block.cpp`, `src/reader.cpp`, `src/stats.cpp`).
- `include/osf/osf.hpp` umbrella re-exports the three new
  headers.
- `error_category_name` extended to cover the six new
  `Error::Code` values.
- `ctest` count: 83 → 124 (5 + 16 + 4 + 9 + 20 + 3 + 20 + 6
  unchanged; 7 new block-unit + 6 new stats-unit + 22 new
  reader-unit + 6 new reader-integration).

## [0.0.4] - 2026-05-23

### Added

- OSF4 XML metablock parser (`osf::parse_metablock_xml`) in two
  overloads: `std::uint8_t const*` + size and `std::string_view`.
  Populates the same `osf::MetaBlock` data model as the OSF5 JSON
  parser (Phase 3); Phase 4's success criterion is symmetric
  population, pinned by an `equidistant_osf4_and_osf5_have_matching_channels`
  integration test.
- New `osf::Error::Code::XmlParseError` enumerator, paralleling the
  existing `JsonParseError`. `error_category_name` extended.
- Vendored `pugixml` v1.15 (MIT) under `third_party/pugixml/`.
  Unlike the previous two vendored libraries pugixml is not
  header-only; its `pugixml.cpp` compiles into `osf_core` directly.
  The translation unit is built with warnings disabled
  (`/W0` on MSVC, `-w` on GCC/Clang) since it is treated as
  binary-identical to upstream. Include path is attached to
  `osf::headers` SYSTEM so consumers can `#include <pugixml.hpp>`
  via the interface target if needed.
- `tests/unit/test_metablock_xml.cpp` — 20 unit tests covering
  happy-path field round-trip (minimal + full channel + infos),
  short-form / long-form geolocation, `bytearray` alias,
  `count` mismatch tolerance, deprecated `scale`/`offset` tolerated,
  unknown attribute ignored, plus negative cases (removed datatype,
  wrong root, malformed XML, every required-attribute-missing case,
  invalid `sizeoflengthvalue`, channel-index out-of-u16-range,
  non-numeric `timeincrement`, overload agreement, null-pointer
  edge cases).
- `tests/integration/test_metablock_xml_examples.cpp` — 6
  integration tests against `examples/generated/osf4_*.osf` plus
  the field samples `examples/motorbike.osf` and
  `examples/steam_loco.osf`. Includes the cross-parser symmetry
  probe (OSF4 file via XML parser vs. OSF5 file via JSON parser
  must have matching channel lists).

### Changed

- `osf_core` library target gains a third translation unit
  (`src/metablock_xml.cpp`) and the vendored
  `third_party/pugixml/pugixml.cpp`.
- `osf::headers` interface target gains a third SYSTEM include
  path (`third_party/pugixml/`).
- `ctest` count: 57 → 83 (5 + 16 + 4 + 9 + 20 + 3 unchanged; 20 new
  XML unit tests in `test_metablock_xml`; 6 new XML integration
  tests in `test_metablock_xml_examples`).

## [0.0.3] - 2026-05-19

### Added

- OSF5 JSON metablock parser (`osf::parse_metablock_json`) in two
  overloads: `std::uint8_t const*` + size and `std::string_view`.
- `osf::DataType`, `osf::ChannelType`, `osf::SpectrumType` enums
  mirroring the spec rev 2026-05-04 datatype set (`pair`, `triple`,
  `candata` removed; `gpsdata` renamed to `gpslocation`; unsigned-int
  datatypes `uint8`..`uint64` added).
- `osf::FileInfo`, `osf::Channel`, `osf::Info`, `osf::MetaBlock`
  structs as the shared metablock data model (used by both the OSF5
  parser landing in this release and the OSF4 parser arriving in
  Phase 4). `std::optional<T>` everywhere the Rust reference has
  `Option<T>`; default member initialisers throughout.
- `osf::parse_data_type`, `osf::parse_channel_type`,
  `osf::parse_spectrum_type` wire-string-to-enum helpers.
  `parse_data_type` rejects datatypes removed in spec rev
  2026-05-04 with `Error::Code::RemovedInSpec` and a replacement-hint
  message; unknown spellings fall through to `Unsupported`.
- Three new `osf::Error::Code` values: `InvalidMetablock`,
  `RemovedInSpec`, `JsonParseError`.
- Vendored `nlohmann/json` v3.11.3 (single-header, MIT) under
  `third_party/nlohmann-json/`; followed the same pattern as
  `tl-expected`: byte-identical drop, SHA-256 of `json.hpp` matches
  the upstream release asset, LICENSE prefixed with two provenance
  lines.
- `tests/unit/test_types.cpp` — 9 unit tests for the type-string
  parsers (all current datatype spellings, `bytearray` alias,
  removed-in-spec rejection, unknown-spelling fallback, channel-type
  and spectrum-type spellings).
- `tests/unit/test_metablock.cpp` — 20 unit tests for
  `parse_metablock_json` covering happy-path field round-trip,
  forward-compatibility (unknown top-level + deprecated channel
  fields tolerated), every required-field-missing case, invalid
  `sizeoflengthvalue`, malformed JSON, non-object root, non-array
  channels/infos, channel-index out-of-u16-range, overload agreement,
  null-pointer edge cases.
- `tests/integration/test_metablock_examples.cpp` — 3 integration
  tests against the OSF5 reference files in `examples/generated/`:
  snapshot check on `osf5_equidistant.osf`; every `osf5_*.osf`
  parses with non-empty channels and valid `sizeoflengthvalue`;
  `osf5_gpslocation.osf` declares a `GpsLocation` channel.

### Changed

- `include/osf/osf.hpp` umbrella now also re-exports `metablock.hpp`
  and `types.hpp`.
- `osf_core` library target gains two translation units
  (`src/metablock.cpp`, `src/types.cpp`).
- `osf::headers` interface target gains the second SYSTEM include
  path (`third_party/nlohmann-json/`).

## [0.0.2] - 2026-05-10

### Added

- Magic-header parser (`osf::parse_magic_header`) in three overloads:
  `std::istream&`, `std::uint8_t const*` + size, `std::filesystem::path`.
- `osf::OsfVersion` enum (`Osf4` / `Osf5`).
- `osf::MagicHeader` struct (`version`, `metablock_len`) with friend
  `operator==` / `operator!=`.
- `osf::MAX_MAGIC_HEADER_LEN` public constant (128 bytes).
- Three new `osf::Error::Code` values: `InvalidMagicHeader`,
  `UnsupportedVersion`, `MagicHeaderTooLong`.
- `tests/unit/test_header.cpp` — 16 unit tests against synthetic byte
  sequences (identifier spellings, error codes, CRLF tolerance,
  lone-CR rejection, stream-position invariant, buffer↔istream
  equivalence, path overload, equality).
- `tests/integration/test_header_examples.cpp` — 4 integration tests
  against `examples/` (one iterates over the 17 generated reference
  files in `examples/generated/`).
- `OSF_EXAMPLES_DIR` CMake define for the integration test target,
  resolved via `file(TO_CMAKE_PATH)` for forward-slash literal safety
  on Windows.

### Changed

- `include/osf/osf.hpp` umbrella now also re-exports `header.hpp`.
- `osf_core` library target gains a second translation unit
  (`src/header.cpp`).

## [0.0.1] - 2026-05-08

### Added

- Initial CMake skeleton with `osf::osf` and `osf::headers` targets.
- Vendored `tl::expected` for `Result<T>` support.
- Error and Result types as foundation API.
- GoogleTest integration via `FetchContent`.
