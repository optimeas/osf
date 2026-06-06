---
title: File examples
description: OSF example files — synthetic reference files and real field recordings for testing and learning
image: "/img/om_social_card.png"
keywords:
  - File format
  - OSF4
  - OSF5
  - OSF
  - Examples
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
---

🇩🇪 [German version](../../de/examples/osf_file_examples.md)

## OSF example files

The [`examples/`](https://github.com/optimeas/osf/tree/main/examples)
directory in the repository contains two kinds of files for learning the
format and checking a reader piece by piece: **generated reference files**
(synthetic, one feature per file) and **field samples** (real recordings from
optiMEAS measurement devices).

## Generated reference files

The files under `examples/generated/` are produced by the
[Delphi reference implementation](../implementations/delphi.md)
(reproducible non-interactively with `OSFGeneratorCLI`). Each file targets
**one** feature, and each exists as an OSF4 variant (XML header) and an OSF5
variant (JSON header):

| File (OSF4 / OSF5) | Demonstrates |
|---|---|
| `osf4_equidistant.osf` / `osf5_equidistant.osf` | Equidistant channel (fixed sample rate) |
| `osf4_scalar_numeric.osf` / `osf5_scalar_numeric.osf` | Timestamped floating-point scalar channels |
| `osf4_scalar_int64.osf` / `osf5_scalar_int64.osf` | Signed 64-bit integer channel |
| `osf4_scalar_unsigned.osf` / `osf5_scalar_unsigned.osf` | Unsigned integer channels |
| `osf4_gpslocation.osf` / `osf5_gpslocation.osf` | GPS location channel |
| `osf4_timestamped_string.osf` / `osf5_timestamped_string.osf` | Timestamped `string` payloads |
| `osf4_timestamped_binary.osf` / `osf5_timestamped_binary.osf` | Timestamped `binary` blobs |
| `osf4_mixed.osf` / `osf5_mixed.osf` | Several channel types in one file |
| — / `osf5_mixed_extended.osf` | Extended mixed OSF5 file |

In total **17 files** (8 OSF4 + 9 OSF5). A correct reader must process all of
them without error. These files are the cross-language read fixtures; the
OSF4/OSF5 pairing also exercises the version-deterministic rules — for
example the null-termination of `string`/`binary` payloads (OSF4 with, OSF5
without a trailing `0x00`).

## Field samples — real recordings

Real data from optiMEAS devices. They cover what synthetic files cannot:
large channel counts, real timestamp patterns and abruptly ending streams. A
reader should read them without crashing and return the data that is present.

| Path | Format | Description |
|---|---|---|
| `motorbike.osf` | OSF4 | 81 channels of motorbike telemetry — speeds, temperatures, GPS, system status |
| `steam_loco.osf` (+ `.csv`) | OSF4 | 123 channels from a steam-locomotive recording (the `.csv` is a reference export of the same data) |
| `weather_station.osfz` | OSF4, gzip | 28 channels, gzip-compressed OSFZ — exercises transparent decompression on read |
| `Testdata Motorbike/` | OSFZ | Multi-day motorbike recordings in daily subfolders (`YYYYMMDD/…`) — robustness/bulk read tests |

## Opening a file

The quickest way is with the Python package
[`osfdata`](../integrations/python.md):

```python
import osf

mgr = osf.load("examples/steam_loco.osf")
print(f"{len(mgr)} channels")
for ch in mgr.channels:
    print(ch.name, ch.data_type, ch.sample_count)
```

The files can equally be opened with the [Rust](../implementations/rust.md)
(`cargo run --example inspect -- <path>`), [C++](../implementations/cpp.md)
or [Delphi](../implementations/delphi.md) implementation (`osftool info`).
For a format introduction see the chapter [OSF format](../osf_general.md).
