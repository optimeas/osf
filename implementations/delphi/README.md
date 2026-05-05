# OSF — Delphi Implementation

![Status](https://img.shields.io/badge/status-in%20progress-orange.svg)

## Target Platform

Windows desktop and industrial measurement applications built with Delphi (RAD Studio). Suitable for data acquisition systems, HMI applications, and test equipment software.

## What This Implementation Provides

- **Writer**: stream OSF4 and OSF5 files with equidistant and time-stamped channels
- **Reader**: read and decode OSF4 and OSF5 files; random access to channel metadata
- Support for scalar values, vectors, matrices, and binary blobs

## OSF Version Support

Tracks OSF specification revision **2026-05-04** ([English](../../docs/en/osf_general.md), [Deutsch](../../docs/de/osf_general.md)).

Notable points:

- `bcStartData` carries the channel sample rate as a `double` directly after the start timestamp. This applies to both OSF4 and OSF5. Equidistant channels use only the `float` and `double` data types.
- A channel may produce **multiple `bcStartData` blocks** — each opens a new segment with its own absolute start time. The `Segments` list on `TOSFEquidistantDataChannel` exposes the boundaries.
- `string` and `binary` payloads in `bcAbsTimeStampData` are written and read with a trailing null byte (`0x00`), uniformly for OSF4 and OSF5. The reader strips the byte before delivering the value to the channel.
- `bytearray` is accepted as an alias for `binary` on read; the writer always emits `binary`.

### Removed in this revision

The following spec features have been removed and are no longer accepted by the writer or reader:

- Channel parameters: `scale`, `offset`, `physicalunit1` / `physicalunit2` / `physicalunit3`, `physicaldimension1` / `physicaldimension2` / `physicaldimension3`.
- Data types: `pair`, `triple`, `candata`.

The reader raises a clear "no longer supported" error if a file declares one of these data types.

### Renamed

- `gpsdata` → `gpslocation`. The struct field order is now `latitude`, `longitude`, `altitude`. There is no backward-compatibility alias for `gpsdata` (it was never used in production).

## Status

**In progress.** Core reader and writer classes are under active development.

## Dependencies

- Delphi (RAD Studio) — no third-party libraries required for the core implementation
- Standard Delphi RTL (classes, streams, XML parser for OSF4 metadata, JSON for OSF5)

## Structure

```
delphi/
  src/        — source units
  tests/      — DUnit/DUnitX test projects (planned)
  demos/      — sample applications demonstrating read and visualization
```
