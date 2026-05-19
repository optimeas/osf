# OSF — C++ implementation

![Phase](https://img.shields.io/badge/phase-1%3A%20skeleton-orange)
[![License](https://img.shields.io/badge/license-MIT-blue)](../../LICENSE)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue)

A standalone C++17 implementation of the [Open Streaming Format](../../README.md) specification. Reads and writes `.osf` and `.osfz` files natively — no FFI, no Rust dependency, idiomatic modern C++. This directory currently holds the Phase 1 skeleton: CMake build system, vendored `tl::expected`, foundation `osf::Error` and `osf::Result<T>` types, and a GoogleTest smoke test.

## Build quickstart

```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

For platform-specific instructions, CMake options, and FAQ, see [`BUILD.md`](BUILD.md). For the architectural rationale, see [`DECISIONS.md` §20](../../DECISIONS.md).

## Roadmap

The Phase 1 skeleton lands the build system and the foundation API types. Subsequent phases bring the real OSF functionality in focused sessions (see [DECISIONS.md §20](../../DECISIONS.md) for the full eleven-phase plan):

- Magic header parser, OSF5 JSON metablock, OSF4 XML metablock, block reader, `DataManager`, OSF5 writer, transparent OSFZ decompression, throwing convenience layer.
- CI integration on Linux/macOS/Windows.
- C ABI shared library as a separate target for cross-language consumption (own DECISIONS entry to follow).

Qt integration is intentionally **not** part of the core library (see DECISIONS.md §20). A separate Qt-aware module may follow once the core is stable.
