# Changelog

All notable changes to the C++ implementation of OSF will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
