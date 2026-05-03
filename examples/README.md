# OSF — Example Files

This directory contains sample `.osf` files used to verify reader correctness across all language implementations. Files are organized into two subdirectories with different sources and purposes.

## Directory Structure

| Directory | Source | Purpose |
|---|---|---|
| `generated/` | Delphi reference implementation | Correctness testing against the OSF specification |
| `field/` | Real optiMEAS field devices | Robustness testing against real-world data |

### `generated/`

Reference files produced by the Delphi implementation. Each file targets a specific feature or edge case defined in the OSF specification. A correct reader must parse all files in this directory without error.

Planned files:

| File | Format | Description |
|---|---|---|
| `equidistant_scalar.osf5` | OSF5 | Single equidistant channel, 1 kHz, float32 scalar — the minimal case |
| `timestamped_scalar.osf5` | OSF5 | Single time-stamped channel with irregular sample intervals |
| `multi_channel.osf5` | OSF5 | Mixed equidistant channels at different sample rates (1 Hz, 100 Hz, 10 kHz) |
| `vector_channel.osf5` | OSF5 | Equidistant vector channel (3-axis accelerometer, shape [3]) |
| `matrix_channel.osf5` | OSF5 | Matrix channel (e.g., a camera frame or FFT spectrogram slice) |
| `binary_blob.osf5` | OSF5 | Binary blob channel with image and audio MIME types |
| `all_data_types.osf5` | OSF5 | One channel per OSF5 data type — completeness test |
| `truncated.osf5` | OSF5 | Intentionally truncated file — readers must return partial data without crashing |
| `legacy_osf4.osf` | OSF4 | OSF4 file with XML header for backward-compatibility testing |
| `legacy_osf4_trailer.osf` | OSF4 | OSF4 file with a complete trailer block |

Each generated file is accompanied by a `.json` sidecar describing the expected decoded content (channel list, sample count, first/last values), so test harnesses can validate reader output without hard-coding expected values in source code.

### `field/`

Real recordings from optiMEAS measurement devices. These files cover edge cases that synthetic data cannot — abrupt stream endings, large channel counts, mixed data types, and real-world timestamp patterns.

Field files will be added as they become available. They are not accompanied by sidecar files; implementations are expected to read them without crashing and return whatever data is present.

## Status

Example files have not been added yet. Generated files will be produced once the Delphi reference implementation reaches a stable state. Field files will follow from optiMEAS device recordings.
