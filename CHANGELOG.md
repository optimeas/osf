# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [0.7.0] — 2026-05-20

### Changed

- Relicensed the entire project from the Apache License 2.0 to the MIT License. The `LICENSE` file, every source-file header (Delphi, Rust, C++), package metadata (`Cargo.toml`, `pyproject.toml`), and documentation were updated accordingly. Vendored third-party code under `implementations/cpp/third_party/` keeps its own upstream licenses (`tl::expected` CC0-1.0, `nlohmann/json` MIT). The `[0.1.0]` entry below is left intact as a historical record — that release did ship under Apache 2.0.

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
