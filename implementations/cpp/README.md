# OSF — C++ implementation

![Status](https://img.shields.io/badge/status-complete-brightgreen)
[![License](https://img.shields.io/badge/license-MIT-blue)](../../LICENSE)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue)

A standalone C++17 implementation of the [Open Streaming Format](../../README.md) specification — no FFI, no Rust dependency, idiomatic modern C++. Reads `.osf` and `.osfz` files and writes OSF5.

## Status

Built as a phased plan (see [DECISIONS.md §20](../../DECISIONS.md) for the full list). **All phases are complete.** The read and write surface is covered by the GoogleTest/ctest suite (0 warnings under MSVC `/W4 /permissive-`), and CI builds and tests on **Linux, macOS and Windows** with warnings-as-errors.

**Read path:**

- Magic-header parser; OSF5 JSON and OSF4 XML metablock parsers
- Block-stream reader and the typed `DataManager` (uniform in-memory reader exposing typed channels)
- Transparent OSFZ (gzip/zlib) decompression — `.osf` and `.osfz` read through the same API

**Write path (OSF5):**

- `StreamingWriter` — embedded, sample-by-sample, fsync per block (power-loss safe)
- `BlockWriter` — analyst-style, accumulate in memory and emit a complete file
- `StaleValueGuard` — optional freshness layer re-emitting idle channels' last value

**Convenience and bindings:**

- A throwing convenience layer (`osf::throwing`) over the `Result<T>` core for callers who prefer exceptions
- The C ABI shared library `osf-c` (`osf/c_api.h`) — a pure C99 layer for cross-language consumption (built with `-D OSF_BUILD_C_API=ON`)

## Build quickstart

```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

For platform-specific instructions, CMake options, and FAQ, see [`BUILD.md`](BUILD.md). For the architectural rationale and the full phased plan, see [`DECISIONS.md` §20](../../DECISIONS.md).

Qt integration is intentionally **not** part of the core library (see DECISIONS.md §20). A separate Qt-aware module may follow once the core is stable.

## Naming conventions

Types use `PascalCase` (`DataManager`, `BlockWriter`, `ControlKind`, …) and functions/methods use `snake_case` (`load_from_file()`, `write_to_file()`, …). This matches the Rust reference implementation and the C ABI surface (`osf_load_file`, …). The `Kind` suffix on variant-tag enumerations (`BlockKind`, `ControlKind`, `ChannelData::Kind`) is a deliberate idiom and is kept consistently. See [DECISIONS.md §20](../../DECISIONS.md) for the full rationale.
