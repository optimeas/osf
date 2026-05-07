---
title: Python Integration
description: Python bindings for the Open Streaming Format (OSF) — read and write with native performance via osfdata
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Python
  - osfdata
  - PyO3
  - NumPy
  - pandas
last_update:
  date: 2026-05-06
  author: Optimeas GmbH
---

🇩🇪 [Deutsche Version](../../de/integrations/python.md)

# Python Integration

The **`osfdata`** package provides a complete Python binding for the Open Streaming Format. It reads and writes OSF4 and OSF5 files, transparently detects compressed OSFZ files, and integrates seamlessly with the scientific Python ecosystem around NumPy.

Unlike pure-Python implementations, the actual work is done in a Rust library underneath. Python sees and uses only the thin wrapper on top. The result: load times in the millisecond range even for several hundred thousand samples, very low memory overhead, and numeric data transferred to NumPy arrays without copying.

## What `osfdata` is for

`osfdata` solves the typical tasks around OSF data in Python:

- **Bringing data into analysis pipelines.** OSF files are loaded, individual channels are accessed directly as NumPy arrays, and forwarded to libraries like SciPy, scikit-learn, or PyTorch.
- **Combining data from different sources.** OSF files from the field are read, filtered or merged, and written out as new OSF files.
- **Migrating legacy data.** OSF4 files are read and rewritten as OSF5 — a convenient migration without a separate tool.
- **Handling compressed files transparently.** OSFZ files (zlib or gzip) are automatically detected and decompressed without any user configuration.

The package is aimed at data analysts, engineers, and scientists who want to integrate OSF data into Python workflows. It is not aimed at embedded developers who *produce* the data — separate, lightweight writer implementations exist for use directly on devices.

## Relationship to `python-osf`

`osfdata` is the modern successor to the existing [`python-osf`](https://github.com/optimeas/python-osf) package. `python-osf` is a pure-Python implementation that supports OSF4 reading only, and is significantly slower for large files. Beyond that, `osfdata` provides:

- full OSF4 and OSF5 support (read *and* write),
- substantially higher performance via the Rust foundation,
- complete coverage of all data types, including `binary`, `gpslocation`, and unsigned integers,
- conformance with the current specification revision (2026-05-04),
- transparent OSFZ decompression for both zlib- and gzip-compressed files.

`python-osf` will be marked deprecated once `osfdata` covers all production use cases in practice.

## Supported platforms

`osfdata` is distributed as a precompiled binary package (wheel) for the following platforms:

| Platform | Architecture | Python versions |
|---|---|---|
| Linux | x86_64 | 3.9 – 3.13 |
| Linux | aarch64 (ARM 64-bit) | 3.9 – 3.13 |
| macOS | arm64 (Apple Silicon) | 3.9 – 3.13 |
| Windows | x86_64 | 3.9 – 3.13 |

On all supported platforms, installation runs without a compiler or other tools. Intel-macOS is not shipped as a wheel; installation from the source distribution is possible but requires a local Rust toolchain. The same applies to other exotic platforms (FreeBSD, Windows-on-ARM, older Linux distributions without manylinux compatibility).

## Installation

`osfdata` can be installed with both common Python package managers.

### With pip

```bash
pip install osfdata
```

### With uv

```bash
uv pip install osfdata
```

`uv` is a newer, much faster package manager that combines `pip` and `venv`. If you haven't used it yet: installation and documentation at [docs.astral.sh/uv](https://docs.astral.sh/uv).

### From the pre-release on TestPyPI

During the stabilization phase, `osfdata` is initially published on TestPyPI. To install from there:

```bash
pip install --index-url https://test.pypi.org/simple/ \
            --extra-index-url https://pypi.org/simple/ \
            osfdata
```

The second index reference is required because TestPyPI does not provide all dependencies (NumPy etc.) — pip needs to look them up on the regular PyPI.

## Import and first example

After installation, the package is imported under the short name `osf` (the distribution name `osfdata` is used on PyPI; the import name is independent — a common Python pattern, comparable to `scikit-learn` ↔ `import sklearn`).

```python
import osf

mgr = osf.load("measurement.osf")
print(f"File contains {len(mgr)} channels")

temp = mgr.channel("Sensor.Temperature")
samples = temp.samples()      # NumPy array, dtype matches the OSF data type
timestamps = temp.timestamps_ns()
```

That covers the essentials: load file, address channel by name, get values as a NumPy array.

## API overview

The library consists of a few clear public building blocks. Further details — such as how a channel with multiple segments is handled, or how timestamped and equidistant data are distinguished — emerge naturally when working with the objects listed here.

### Module-level functions

| Function | Purpose |
|---|---|
| `osf.load(path)` | Loads an OSF or OSFZ file and returns a `DataManager`. Detects the format automatically. |
| `osf.save(manager, path)` | Writes a `DataManager` as an OSF5 file. |

### Class `DataManager`

Represents a loaded OSF file with all channels and metadata.

| Attribute / method | Description |
|---|---|
| `len(mgr)` | Number of channels. |
| `mgr.channels` | List of all channels (`list[Channel]`). |
| `mgr.channel(name)` | Address a channel by name. Returns `None` if not present. |
| `mgr.channel_by_index(i)` | Address a channel by numeric index. |
| `mgr.stats` | `ReaderStats` object with statistics about the load operation. |

### Class `Channel`

A single channel with metadata and values. Three flavors — equidistant, timestamped-numeric, timestamped-variable (for strings and binary data) — are addressed through the same API.

| Attribute / method | Description |
|---|---|
| `ch.name` | Channel name (often hierarchical, e.g. `"Motor.RPM"`). |
| `ch.index` | Numeric channel index in the file. |
| `ch.data_type` | Data type as a string (`"double"`, `"int32"`, `"string"`, …). |
| `ch.channel_type` | `"equidistant"`, `"timestamped"`, or `"variable"`. |
| `ch.sample_count` | Number of samples. |
| `ch.physical_unit` | Physical unit (if specified). |
| `ch.is_empty` | True if the channel contains no data. |
| `ch.samples()` | Values as a NumPy array. |
| `ch.timestamps_ns()` | Timestamps in nanoseconds since epoch as an `int64` NumPy array. |
| `ch.segments` | List of segments (only meaningful for equidistant channels). |

### Class `Segment`

Describes one section of an equidistant channel — for example, when a recording is divided into multiple phases by trigger events or drift corrections.

| Attribute | Description |
|---|---|
| `seg.start_timestamp_ns` | Start time of the segment in nanoseconds since epoch. |
| `seg.sample_rate_hz` | Sample rate within this segment in Hertz. |
| `seg.sample_count` | Number of samples in this segment. |

### Class `ReaderStats`

Diagnostic information about the most recent load operation.

| Attribute | Description |
|---|---|
| `stats.compressed` | True if the file was OSFZ-compressed. |
| `stats.compression_format` | `"gzip"`, `"zlib"`, or `None`. |
| `stats.channels_total` | Number of channels in the file. |
| `stats.blocks_total` | Number of data blocks read. |
| `stats.elapsed_ms` | Load time in milliseconds. |
| `stats.file_size_bytes` | File size on disk. |

### Class `WriterBuilder`

Builds OSF5 files from channel definitions and sample data. The builder accepts channels one at a time; each channel can hold multiple segments or sample blocks.

```python
b = osf.WriterBuilder().creator("measurement-system-v1").tag("pretest")

idx = b.add_channel(
    name="Sensor.Pressure",
    data_type="double",
    channel_type="scalar",
    physical_unit="bar",
)

import numpy as np
values = np.array([1.013, 1.015, 1.014, 1.012], dtype=np.float64)
b.add_equidistant_segment(
    idx,
    start_ns=1_700_000_000_000_000_000,
    sample_rate_hz=1.0,
    values=values,
)

b.write_to_file("output.osf")
```

The most important methods:

| Method | Purpose |
|---|---|
| `b.creator(s)` / `b.tag(s)` / `b.reason(s)` | Set file metadata. |
| `b.location(lat, lon, alt)` | Geographic position of the recording. |
| `b.add_channel(...)` | Define a new channel; returns the channel index. |
| `b.add_equidistant_segment(idx, start_ns, sample_rate_hz, values)` | Add an equidistant segment (only `float`/`double`). |
| `b.add_timestamped_samples(idx, ts_ns, values)` | Add numeric timestamped samples. |
| `b.add_string_samples(idx, ts_ns, values)` | Add string samples with timestamps. |
| `b.add_binary_samples(idx, ts_ns, values)` | Add binary-data samples (e.g. images, audio). |
| `b.write_to_file(path)` | Write the assembled data to a file. |

## Usage notes

**Timestamps.** All time values are 64-bit integers in nanoseconds since Unix epoch (UTC). This precision covers both high-frequency vibration measurements and slow process data without requiring different time data types. An optional convenience layer to Python's `datetime` type is planned for a later version.

**NumPy data types.** The dtype of the returned NumPy array matches the OSF data type directly: `double` → `float64`, `int32` → `int32`, `bool` → `bool`, and so on. No implicit conversion happens — if the channel contains `int16`, the NumPy array will also be `int16`.

**Memory management.** Numeric sample arrays are transferred between Rust and Python without copying. This makes reading large channels (millions of samples) very fast, even on modest hardware.

**Error handling.** All functions raise `osf.OsfError` (a subclass of `Exception`) when something goes wrong — file not found, invalid format, unknown data type. The behavior on unknown or corrupted data blocks follows the best-effort principle: data is delivered up to the last well-readable block, after which the read terminates cleanly.

## Usage examples

Detailed example notebooks and scripts will follow in a separate section. Planned topics:

- Quickly exploring an unknown OSF file
- Migration from OSF4 to OSF5
- Filtering and merging multiple recordings
- Transitioning to pandas for tabular analysis
- Integration into PyTorch datasets

## Source code and further information

- The package on PyPI: [pypi.org/project/osfdata](https://pypi.org/project/osfdata/)
- Source code on GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf), directory `implementations/python/`
- Build and release process: see [`BUILD.md`](https://github.com/optimeas/osf/blob/main/implementations/python/BUILD.md) in the repository
- Format specification: see the [OSF format](../osf_general.md) chapter in this documentation
