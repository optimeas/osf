# OSF — C++ implementation

![Status](https://img.shields.io/badge/status-in%20development-yellow)
[![License](https://img.shields.io/badge/license-MIT-blue)](../../LICENSE)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue)

A standalone C++17 implementation of the [Open Streaming Format](../../README.md) specification — no FFI, no Rust dependency, idiomatic modern C++. Reads `.osf` and `.osfz` files and writes OSF5. Cross-language CI and a C ABI wrapper are the remaining milestones.

## Status

Built as a phased plan (see [DECISIONS.md §20](../../DECISIONS.md) for the full list). The core read and write surface is complete and covered by the GoogleTest/ctest suite (0 warnings under MSVC `/W4 /permissive-`).

**Read path:**

- Magic-header parser; OSF5 JSON and OSF4 XML metablock parsers
- Block-stream reader and the typed `DataManager` (uniform in-memory reader exposing typed channels)
- Transparent OSFZ (gzip/zlib) decompression — `.osf` and `.osfz` read through the same API

**Write path (OSF5):**

- `StreamingWriter` — embedded, sample-by-sample, fsync per block (power-loss safe)
- `BlockWriter` — analyst-style, accumulate in memory and emit a complete file
- `StaleValueGuard` — optional freshness layer re-emitting idle channels' last value

**Convenience:**

- A throwing convenience layer over the `Result<T>` core for callers who prefer exceptions

**In progress / pending:**

- Cross-platform CI (Linux/macOS/Windows) — in progress
- A C ABI shared library as a separate target for cross-language consumption — pending

## Build quickstart

```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

For platform-specific instructions, CMake options, and FAQ, see [`BUILD.md`](BUILD.md). For the architectural rationale and the full phased plan, see [`DECISIONS.md` §20](../../DECISIONS.md).

Qt integration is intentionally **not** part of the core library (see DECISIONS.md §20). A separate Qt-aware module may follow once the core is stable.
