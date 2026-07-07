---
title: Building & integrating
description: CMake build of the OSF C++ library — prerequisites, options, targets, integration into your own project, Doxygen, tests and CI
sidebar_position: 6
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - CMake
  - Build
  - Doxygen
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Building & integrating

The authoritative, continuously maintained build guide is the `BUILD.md`
right in the library directory (including an FAQ on proxy environments,
linker warnings and cross-compilation). This page summarizes the
essentials and adds the integration scenarios.

## Prerequisites

- **CMake ≥ 3.20**
- **C++17 compiler:** MSVC (VS 2017 15.7+), GCC ≥ 7, Clang ≥ 5,
  AppleClang ≥ 10
- **Internet on the first configure:** GoogleTest and zlib come as
  SHA256-pinned tarballs via `FetchContent` and are cached in the build
  tree; after that the build is offline.

## Quick start

```bash
git clone https://github.com/optimeas/osf.git
cd osf/implementations/cpp

cmake -B build
cmake --build build
ctest --test-dir build
```

Windows (Visual Studio generator, multi-config):

```powershell
cmake -B build -G "Visual Studio 17 2022"    # VS 2026: "Visual Studio 18 2026"
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

## CMake options

| Option | Default | Effect |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | core as a shared library instead of static |
| `OSF_BUILD_TESTS` | `ON` | build the GoogleTest/ctest suite |
| `OSF_BUILD_EXAMPLES` | `ON` | build the example programs (`inspect`, `dump`, `write`, `copy`) |
| `OSF_BUILD_DOCS` | `OFF` | Doxygen target `osf-docs` (needs Doxygen; if absent, skipped cleanly) |
| `OSF_BUILD_C_API` | `OFF` | also build the C ABI library `osf-c` + C99 test |
| `OSF_USE_SYSTEM_ZLIB` | `OFF` | system zlib (`find_package(ZLIB)`) instead of FetchContent zlib |
| `OSF_WARNINGS_AS_ERRORS` | `OFF` | `/WX` or `-Werror`; `ON` in CI, deliberately lenient locally |

**C++17 is not an option,** but the firmly-defined baseline of the
library — moving to C++20+ would be a deliberate library upgrade, not a
build switch.

## Targets

| Target | Kind | Contents |
|---|---|---|
| `osf::osf` | static (or shared) library | the whole core; file name `libosf.a` / `osf.lib` (internal CMake target `osf_core`, `OUTPUT_NAME osf`) |
| `osf::headers` | INTERFACE | public include paths plus the vendored `tl::expected` and `nlohmann/json` directories (SYSTEM-attached so upstream warnings stay silent) |
| `osf-c` | shared library | the C99 ABI (only with `OSF_BUILD_C_API=ON`) |
| `osf-docs` | custom | Doxygen HTML (only with `OSF_BUILD_DOCS=ON`; not part of `ALL`) |

## Integrating into your own project

The direct way today is `add_subdirectory` on a checkout (a
`cmake --install` / package workflow is planned as a future extension):

```cmake
# Variant A: repo as a submodule/checkout next to your own code
add_subdirectory(extern/osf/implementations/cpp osf-build)
target_link_libraries(my_app PRIVATE osf::osf)
```

```cmake
# Variant B: FetchContent on the GitHub repo
include(FetchContent)
FetchContent_Declare(osf
    GIT_REPOSITORY https://github.com/optimeas/osf.git
    GIT_TAG        main                       # better: pin a tag/commit
    SOURCE_SUBDIR  implementations/cpp)
set(OSF_BUILD_TESTS OFF)
set(OSF_BUILD_EXAMPLES OFF)
FetchContent_MakeAvailable(osf)
target_link_libraries(my_app PRIVATE osf::osf)
```

For consumers `#include <osf/osf.h>` is enough; `osf::throwing` and the
C ABI are included separately when needed.

## Vendored dependencies

Under `third_party/` lie, byte-identical (tag-pinned, SHA256-verified,
only the LICENSE file renamed with provenance lines):

| Library | License | Purpose |
|---|---|---|
| `tl::expected` | CC0-1.0 | `Result<T>` monad |
| `nlohmann/json` 3.12.0 | MIT | OSF5 metablock (JSON), exception-free parse mode |
| `pugixml` | MIT | OSF4 metablock (XML) |

zlib 1.3.2 (for OSFZ) is not vendored but comes via FetchContent
(tarball + SHA256 pin) or, with `OSF_USE_SYSTEM_ZLIB=ON`, from the
system. Never relicense the vendored licenses — they keep their upstream
license, independent of the project's MIT license.

## API reference (Doxygen)

```bash
cmake -B build -D OSF_BUILD_DOCS=ON
cmake --build build --target osf-docs        # Windows: additionally --config Debug
# result: build/doxygen/html/index.html
```

The public headers are documented throughout with Doxygen comments; the
generated reference complements these handbook pages with the full
signature level. Without Doxygen installed the target is skipped with a
STATUS message.

## Tests

```bash
ctest --test-dir build                        # Windows: -C Debug
```

- **Unit tests** (`tests/unit/`) work on synthetic data and cover each
  layer individually.
- **Integration tests** (`tests/integration/`) read the real example
  files under `examples/` (field data + the 17 generated reference
  files) and prove, among other things, bit-exact round-trips.
- **C ABI test** (`tests/capi/test_capi.c`) is a standalone C99
  program — it proves C compatibility and DLL linkage.

Expectation: **all tests green** (as of 2026-06-12: 321 tests with
`OSF_BUILD_C_API=ON`), 0 warnings under `/W4 /permissive-`.

## CI

`.github/workflows/ci.yml` builds and tests the C++ tree on every push
touching `implementations/cpp/**` on **ubuntu-latest, macos-14 and
windows-latest** with `OSF_WARNINGS_AS_ERRORS=ON` and
`OSF_BUILD_C_API=ON`. Note for local development: the CI MSVC version may
differ from the local one — a locally clean `/WX` build does not replace
the CI run; verify a branch with `gh workflow run ci.yml --ref <branch>`.

## Known pitfalls

- **Corporate proxy / TLS interception:** if the FetchContent download
  fails (`CRYPT_E_NO_REVOCATION_CHECK` or similar), download the
  tarballs once manually (e.g. with PowerShell `Invoke-WebRequest`, which
  uses the Windows certificate store), extract them and point CMake at
  the local copies with
  `-D FETCHCONTENT_SOURCE_DIR_GOOGLETEST=…` /
  `-D FETCHCONTENT_SOURCE_DIR_ZLIB=…`. Alternatively register an internal
  mirror (keep the same SHA256) — details in the FAQ of `BUILD.md`.
- **`LNK4098` (MSVC):** CRT mismatch between GoogleTest and `osf_core`;
  `tests/CMakeLists.txt` already sets `gtest_force_shared_crt=ON` — do
  not override it.
- **ctest finds 0 tests:** `gtest_discover_tests` runs the test binaries
  at configure time; in sandboxed environments start the binary directly
  and check the configure log.

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
