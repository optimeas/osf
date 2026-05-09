# Changelog

All notable changes to the C++ implementation of OSF will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
