# OSF — Example Files

This directory will contain sample `.osf` files that demonstrate different OSF features and can be used to test readers across all language implementations.

## Planned Example Files

| File | Format | Description |
|---|---|---|
| `equidistant_scalar.osf5` | OSF5 | Single equidistant channel, 1 kHz, float32 scalar — the minimal case |
| `timestamped_scalar.osf5` | OSF5 | Single time-stamped channel with irregular sample intervals |
| `multi_channel.osf5` | OSF5 | Mixed equidistant channels at different sample rates (1 Hz, 100 Hz, 10 kHz) |
| `vector_channel.osf5` | OSF5 | Equidistant vector channel (3-axis accelerometer, shape [3]) |
| `matrix_channel.osf5` | OSF5 | Matrix channel (e.g., a camera frame or FFT spectrogram slice) |
| `binary_blob.osf5` | OSF5 | Binary blob channel for arbitrary payload data |
| `long_recording.osf5` | OSF5 | Multi-hour recording to test streaming and partial-read performance |
| `truncated.osf5` | OSF5 | Intentionally truncated file to verify that readers handle incomplete files gracefully |
| `legacy_osf4.osf` | OSF4 | OSF4 file with XML header for backward-compatibility testing |
| `legacy_osf4_trailer.osf` | OSF4 | OSF4 file with a complete trailer block |

## Purpose

These files serve two purposes:

1. **Specification conformance** — each file targets a specific feature or edge case defined in the OSF specification. A correct reader must be able to parse all non-error files and reject malformed ones gracefully.
2. **Regression testing** — language implementations should include these files in their test suites and assert that the decoded values match the expected output documented alongside each file.

## Expected Output Files

Each `.osf` or `.osf5` file will be accompanied by a `.json` sidecar that describes the expected decoded content (channel list, sample count, first/last values), so test harnesses can validate reader output without hard-coding expected values in source code.

## Status

Example files have not been created yet. They will be added once the Delphi reference implementation reaches a stable state and can produce verified output files.
