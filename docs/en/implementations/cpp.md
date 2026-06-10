---
title: C++ implementation
description: Standalone C++17 implementation of the Open Streaming Format — reader, DataManager, both writers, transparent OSFZ and a C ABI
sidebar_position: 5
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - C++17
  - CMake
  - C-ABI
  - osf-c
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
---

🇩🇪 [German version](../../de/implementations/cpp.md)

# C++ implementation

A **standalone C++17 implementation** of the Open Streaming Format — no FFI,
no Rust dependency, idiomatic modern C++. It reads `.osf` and `.osfz` files
and writes OSF5. The implementation was built as a parallel implementation
to the Rust core, not as a port.

## Capabilities

The planned implementation order (see
[DECISIONS §20](https://github.com/optimeas/osf/blob/main/DECISIONS.md)) is
**fully complete**. The read and write paths are covered by a
GoogleTest/ctest suite (0 warnings under MSVC `/W4 /permissive-`), and CI
builds and tests on **Linux, macOS and Windows**.

**Read path**

- Magic-header parser; OSF5 JSON and OSF4 XML metablock parsers
- Block-stream reader and the typed `DataManager` (unified in-memory reader
  with typed channels)
- Transparent **OSFZ** decompression (gzip/zlib) — `.osf` and `.osfz` are
  read through the same API

**Write path (OSF5)**

- `StreamingWriter` — embedded, sample by sample, `fsync` per block
  (power-loss safe), constant memory footprint
- `BlockWriter` — analyst-friendly, accumulates in memory and writes the
  whole file at the end; auto-bumps `sizeoflengthvalue` from 2 → 4 when
  needed
- `StaleValueGuard` — optional freshness layer that re-emits the last value
  of idle channels

**Convenience and bindings**

- A **throwing convenience layer** (`osf::throwing`) over the `Result<T>`
  core for callers that prefer exceptions
- The **C ABI library `osf-c`** (`osf/c_api.h`) — a pure C99 layer for
  cross-language use (DLL/shared object)

## Building — quick start

```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

Platform-specific notes, CMake options and FAQ are in
[`BUILD.md`](https://github.com/optimeas/osf/blob/main/implementations/cpp/BUILD.md).

### CMake options

| Option | Default | Effect |
|---|---|---|
| `OSF_BUILD_TESTS` | `ON` | build the GoogleTest/ctest suite |
| `OSF_BUILD_C_API` | `OFF` | also build the C ABI library `osf-c` (+ C test) |
| `OSF_USE_SYSTEM_ZLIB` | `OFF` | use system zlib instead of FetchContent |
| `OSF_WARNINGS_AS_ERRORS` | `OFF` | warnings as errors (`/WX` or `-Werror`); `ON` in CI |
| `BUILD_SHARED_LIBS` | `OFF` | build the core library as a shared library |

C++17 is the firmly-defined language baseline. Moving to C++20 or later is a deliberate library upgrade, not a build option. Third-party code (`tl::expected`, `nlohmann/json`,
`pugixml`) is vendored in the repository under `third_party/`; zlib comes via
FetchContent or the system.

### Linking

The library exports two CMake targets:

- `osf::osf` — the core library (static by default; file name `libosf.a` /
  `osf.lib`)
- `osf::headers` — an INTERFACE target with the public include paths

## API at a glance

The core is **exception-free**: operations that can fail return
`osf::Result<T>` (a `tl::expected<T, osf::Error>`).

### Reading

```cpp
#include <osf/manager.hpp>

auto result = osf::DataManager::load_from_file("measurement.osf");  // also .osfz
if (!result) {
    // result.error().message  —  structured error, no exception
    return;
}
osf::DataManager const& mgr = *result;

// Address a channel by name (mandatory, DECISIONS §10)
if (osf::DataChannel const* ch = mgr.channel("Sensor.Temperature")) {
    auto values = ch->as_doubles_flat();   // typed access
}
```

Callers who prefer exceptions use the opt-in layer:

```cpp
#include <osf/throwing.hpp>

auto mgr = osf::throwing::load("measurement.osf");   // throws osf::Exception on error
```

### Writing (OSF5)

```cpp
#include <osf/block_writer.hpp>

osf::BlockWriter writer;
auto idx = writer.add_channel(/* name, data type, channel type, … */);
// … add samples to idx …
writer.write_to_file("output.osf");
```

For embedded, power-loss-safe writing there is the `StreamingWriter`
(`fsync` per block) instead. A loaded `DataManager` can be written straight
back as OSF5 with the free function `osf::write_to_file(mgr, path)`
(round-trip / OSF4 → OSF5).

### C ABI (`osf-c`)

With `-D OSF_BUILD_C_API=ON` the shared library `osf-c` is built in addition,
with a pure C99 interface (`osf/c_api.h`): opaque handles (`osf_manager`,
`osf_channel`), `osf_status` codes, a thread-local
`osf_last_error_message()` and copy-out readers for timestamps and values —
plus `osf_write_to_file` for the round-trip write path. No C++ exception
crosses the ABI boundary. Intended for binding from C, C#/P-Invoke,
ActiveX/OCX and future language bindings.

## Notes

- **Only OSF5 is written** (DECISIONS §6) — even when the source was an OSF4
  file.
- **No OSFZ output**: compression is a downstream concern (DECISIONS §12);
  OSFZ is read transparently.
- **Best-effort on read**: truncated files yield all data up to the last
  fully readable block, without crashing.
- The library is **Qt-neutral**; a Qt-aware add-on may follow later as a
  separate `integrations/` entry.

## Source code and further reading

- Source code on GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  directory `implementations/cpp/`
- Build guide: [`BUILD.md`](https://github.com/optimeas/osf/blob/main/implementations/cpp/BUILD.md)
- Architecture and phased plan:
  [DECISIONS §20](https://github.com/optimeas/osf/blob/main/DECISIONS.md) and
  the C ABI in §23
- Format specification: chapter [OSF format](../osf_general.md)
