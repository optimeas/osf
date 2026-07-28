# OSF — Example Files

This directory contains real and synthetic OSF files used to learn the
format and to verify reader correctness across all language
implementations. There are two kinds of files: **generated reference
files** (synthetic, one feature per file) and **field samples** (real
recordings from optiMEAS measurement devices).

## `generated/` — synthetic reference files

Produced by the Delphi reference implementation (regenerate with the
headless `OSFGeneratorCLI`, see the repo `CLAUDE.md`). Each file targets
one feature so a correct reader can be validated piece by piece — for
every feature there is an OSF4 (XML-header) and an OSF5 (JSON-header)
variant.

| File (OSF4 / OSF5) | Demonstrates |
|---|---|
| `osf4_equidistant.osf` / `osf5_equidistant.osf` | Equidistant (fixed-rate) channel |
| `osf4_scalar_numeric.osf` / `osf5_scalar_numeric.osf` | Time-stamped floating-point scalar channels |
| `osf4_scalar_int64.osf` / `osf5_scalar_int64.osf` | Signed 64-bit integer scalar channel |
| `osf4_scalar_unsigned.osf` / `osf5_scalar_unsigned.osf` | Unsigned integer scalar channels |
| `osf4_gpslocation.osf` / `osf5_gpslocation.osf` | GPS-location channel |
| `osf4_timestamped_string.osf` / `osf5_timestamped_string.osf` | Time-stamped `string` payloads |
| `osf4_timestamped_binary.osf` / `osf5_timestamped_binary.osf` | Time-stamped `binary` blob payloads |
| `osf4_mixed.osf` / `osf5_mixed.osf` | Several channel types in one file |
| — / `osf5_mixed_extended.osf` | Extended mixed-channel OSF5 file |

17 files in total (8 OSF4 + 9 OSF5) directly under `generated/`. A correct
reader must parse all of them without error. These files are the
cross-implementation read fixtures; the OSF4/OSF5 pairing also exercises
the version-deterministic rules (e.g. the `string`/`binary` null-terminator
handling).

Two sub-directories extend this set beyond the flat file list above. Both
are reached only through `examples/reference_manifest.json` — the
directory-scanning harnesses that glob `generated/*.osf` **non-recursively**
never see them:

| Directory | Contents |
|---|---|
| `integrity/` | OSF5 files at integrity profile level `crc`; a CRC-unaware reader would misparse them. See `generated/integrity/README.md`. |
| `malformed/` | Deliberately **non-conforming** OSF files (e.g. a zero-length data block). These do *not* fall under "a correct reader must parse all of them without error" above — the opposite: a correct reader must detect and skip the anomaly while still recovering the surrounding valid data. See `generated/malformed/README.md`. |

## Field samples — real recordings

Real data from optiMEAS devices. They cover what synthetic files cannot:
large channel counts, real timestamp patterns, and abrupt stream
endings. Readers are expected to read them without crashing and return
whatever data is present.

| Path | Format | Description |
|---|---|---|
| `motorbike.osf` | OSF4 | 81 channels of motorbike telemetry — speeds, temperatures, GPS, system status |
| `steam_loco.osf` (+ `.csv`) | OSF4 | 123 channels from a steam-locomotive recording (`.csv` is a reference export of the same data) |
| `weather_station.osfz` | OSF4, gzip | 28 channels, gzip-compressed OSFZ — exercises transparent decompression on read |
| `Testdata Motorbike/` | OSFZ | Multi-day motorbike recordings in daily subdirectories (`YYYYMMDD/YYYYMMDD_HHMMSS.osfz`) — robustness/bulk read testing |

## Quick start

The fastest way to open any of these is the Python package — see the
[root README Quick Start](../README.md#quick-start-python) and the
runnable scripts in
[`implementations/python/examples/`](../implementations/python/examples/),
several of which load `motorbike.osf`.
