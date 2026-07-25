# Changelog

All notable changes to the `osfdata` package will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-07-25

First release on production PyPI — `pip install osfdata`. The distribution
graduates from the TestPyPI pre-releases to a stable production line; the API
surface below is considered stable under semantic versioning.

### Added

- **OSF5 integrity profile, level `crc`.** `ReaderStats` exposes `integrity`
  (`"none"`/`"crc32c"`/`"ed25519"`), `blocks_crc_failed`,
  `blocks_signature_skipped`, and `verification_status` (`"none"`/`"crc_valid"`/
  `"invalid"`/`"signature_unverifiable"`). `WriterBuilder.with_integrity("crc32c")`
  and `osf.save(mgr, path, integrity="crc32c")` emit the profile (metablock
  `crc32c` header token + per-block frame CRC32C). Signed files stay readable and
  CRC-checked (signatures are not verified). Type stubs updated.

## [0.1.0] - 2026-05-07

Initial release on TestPyPI.

### Added

- Reading OSF4 and OSF5 files (`osf.load`).
- Writing OSF5 files (`osf.save`, `osf.WriterBuilder`).
- Transparent OSFZ decompression for both zlib (RFC 1950) and gzip (RFC 1952) formats.
- NumPy integration: numeric channels are returned as `numpy.ndarray` with matching
  dtypes, without intermediate copies.
- Channel access by name (mandatory) and by index (optional).
- Support for all current spec data types: `bool`, `int8`/`int16`/`int32`/`int64`,
  `uint8`/`uint16`/`uint32`/`uint64`, `float`, `double`, `string`, `binary`,
  `gpslocation`.
- Equidistant channels with multi-segment support (multiple `bcStartData` blocks
  per channel are explicitly supported).
- Timestamped channels for numeric, string, and binary data.
- `ReaderStats` exposing diagnostic information about the most recent load
  operation (file size, compression status, channel and block counts, elapsed time).
- Type stubs (`*.pyi`) for IDE support.
- abi3 wheels for Python 3.9 through 3.13 on Linux x86_64, Linux aarch64,
  macOS arm64, and Windows x86_64.

### Notes

- Intel-macOS wheels are not built; install from the source distribution if needed
  (requires a local Rust toolchain). See [DECISIONS.md §19](https://github.com/optimeas/osf/blob/main/DECISIONS.md).
- This is a pre-release on TestPyPI for stabilization. Production PyPI release
  follows after sufficient field testing.

[Unreleased]: https://github.com/optimeas/osf/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/optimeas/osf/releases/tag/v1.0.0
[0.1.0]: https://github.com/optimeas/osf/releases/tag/v0.1.0
