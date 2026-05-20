# OSF — Delphi Implementation

![Status](https://img.shields.io/badge/status-in%20progress-orange.svg)

## Target Platform

Windows desktop and industrial measurement applications built with Delphi (RAD Studio). Suitable for data acquisition systems, HMI applications, and test equipment software.

## What This Implementation Provides

- **Writer**: stream OSF4 and OSF5 files with equidistant and time-stamped channels
- **Reader**: read and decode OSF4 and OSF5 files; random access to channel metadata
- Support for scalar values, vectors, matrices, and binary blobs
- **Exporters**: OSF → CSV (per-channel and unified single-timeline) and, on Windows, OSF → HDF5
- **osftool**: a command-line tool for merging, exporting, inspecting, converting and verifying OSF files

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

## HDF5 Export

On Windows, osftool can export an OSF file to HDF5:

```
osftool export input.osf output.h5 --format hdf5
```

Each channel becomes a chunked, shuffled and deflated 1-D dataset of `{int64 timestamp_ns; value}` compound records; the hierarchical channel name is split on the namespace separator into an HDF5 group path, and file- and channel-level metadata are written as HDF5 attributes. `--chunk-size`, `--deflate-level`, `--no-shuffle`, `--namespace-sep` and `--hdf5-lib-dir` tune the output.

`hdf5.dll` is loaded dynamically at run time, not linked. The official HDF Group runtime (HDF5 1.14.4-3) is fetched by `../../dataformats/hdf5/lib/install-hdf5.ps1` and is not part of the repository. The binding lives in `src/hdf5/` (`Hdf5.Types` / `Hdf5.Api` / `Hdf5.Wrapper`, a reusable OSF-agnostic HDF5 wrapper) and the exporter in `src/OSF.Export.HDF5.pas`.

`setup/osftool.iss` is an Inno Setup script that builds a Windows installer bundling osftool with the HDF5 runtime and adding it to PATH.

## Status

**In progress.** Core reader and writer classes are under active development.

## Dependencies

- Delphi (RAD Studio) — no third-party libraries required for the core implementation
- Standard Delphi RTL (classes, streams, XML parser for OSF4 metadata, JSON for OSF5)
- HDF5 export (Windows, optional): `hdf5.dll` 1.14.4-3, loaded dynamically at run time — see HDF5 Export above

## Structure

```
delphi/
  src/        — library units (src/hdf5/ holds the HDF5 DLL wrapper)
  tools/      — osftool command-line tool
  setup/      — Inno Setup installer for osftool
  demos/      — sample applications demonstrating read and visualization
  tests/      — DUnit/DUnitX test projects (planned)
```
