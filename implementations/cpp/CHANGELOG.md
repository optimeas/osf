# Changelog

All notable changes to the C++ implementation of OSF will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
