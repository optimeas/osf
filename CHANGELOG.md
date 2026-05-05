# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

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
