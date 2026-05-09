# Building osf (C++)

This document covers building the C++ library from source on Windows, Linux, and macOS, the available CMake options, and answers to common questions.

## Prerequisites

- **CMake 3.20 or newer**
- **A C++17 compiler:**
  - MSVC: Visual Studio 2017 15.7+ (the first version with full `/std:c++17`); 2019, 2022, and 2026 are all fine.
  - GCC 7 or newer.
  - Clang 5 or newer.
  - AppleClang 10 or newer (Xcode 10+).
- **Internet access on first configure.** GoogleTest is downloaded once via `FetchContent` from a SHA256-pinned tarball, then cached under the build tree. Subsequent configures are offline.
- ~150 MB free disk space for the build tree (most of it goes to the GoogleTest source and its objects).

## Quickstart

```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

Default settings: `osf::osf` is built as a static library, tests are on, examples are on (currently a no-op until later phases ship example sources), the C ABI wrapper is off.

## Platform-specific instructions

### Windows (MSVC)

You need either Visual Studio 2017+ with the **Desktop development with C++** workload, or the standalone **Build Tools for Visual Studio**. CMake ships bundled with VS, or you can install it separately from [cmake.org/download](https://cmake.org/download/).

From a regular PowerShell with CMake on `PATH`:

```powershell
cmake -B build -G "Visual Studio 17 2022"   # adjust generator for your VS version
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

For VS 2026 the generator is `"Visual Studio 18 2026"`. From a Developer Command Prompt or Developer PowerShell, the VS-bundled `cmake.exe` is on `PATH` automatically.

Alternative: `cmake -B build -G Ninja` works with Ninja installed and the right toolchain selected (run from a Developer Command Prompt so MSVC `cl.exe` is found).

### Linux (GCC or Clang)

```bash
sudo apt install build-essential cmake     # Debian / Ubuntu
# or:
sudo dnf install gcc-c++ cmake             # Fedora / RHEL / CentOS Stream
# or:
sudo pacman -S base-devel cmake            # Arch

cmake -B build -G "Unix Makefiles"          # or -G Ninja if installed
cmake --build build -j
ctest --test-dir build
```

To force Clang instead of the default GCC:

```bash
CC=clang CXX=clang++ cmake -B build -G Ninja
```

### macOS (AppleClang)

```bash
xcode-select --install     # if the command-line tools are not yet installed
brew install cmake         # if you don't have CMake
cmake -B build -G Ninja    # or "Unix Makefiles"
cmake --build build -j
ctest --test-dir build
```

Apple Silicon (arm64) and Intel (x86_64) hosts both build natively. Universal-binary builds are not configured here; if you need one, set `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` at configure time.

## CMake options

| Option | Default | Effect |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Build `osf::osf` as shared library instead of static. |
| `OSF_BUILD_TESTS` | `ON` | Configure GoogleTest and the unit-test executables. |
| `OSF_BUILD_EXAMPLES` | `ON` | Reserved for later phases. Currently has no effect because no examples ship yet. |
| `OSF_BUILD_C_API` | `OFF` | Build the C ABI shared-library wrapper. Reserved for Phase 11; currently has no effect. |
| `OSF_USE_SYSTEM_ZLIB` | `OFF` | Prefer the system zlib over a `FetchContent` build. Relevant once Phase 8 (OSFZ decompression) lands. |

Set with `-D<OPTION>=<VALUE>`, for example:

```bash
cmake -B build -DBUILD_SHARED_LIBS=ON -DOSF_BUILD_TESTS=OFF
```

The C++ language standard is **not** a CMake option. C++17 is hard-pinned in `CMakeLists.txt`; moving to a newer standard is a deliberate library upgrade, not a build switch (see [DECISIONS.md §20](../../DECISIONS.md)).

## FAQ

### `cmake` not found on Windows

Install CMake from [cmake.org/download](https://cmake.org/download/) (the installer offers to add it to `PATH`), or invoke the VS-bundled binary directly. Visual Studio 2022 ships it at:

```
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
```

Adjust `Community` for `Professional` / `Enterprise` / `BuildTools` and `2022` for `2019` / `2026` as appropriate. Use the full path or run from a Developer PowerShell, where the VS-bundled CMake is already on `PATH`.

### `LNK4098` warnings on MSVC

If you see `LNK4098: defaultlib 'MSVCRT' conflicts with use of other libs`, GoogleTest is linking against a different CRT than `osf_core`. The `tests/CMakeLists.txt` already sets `gtest_force_shared_crt=ON` so both end up on `/MD` (dynamic CRT). If a downstream project that consumes this repo via `add_subdirectory` overrides that setting, restore it.

### FetchContent download fails (proxy / offline build machine)

GoogleTest is fetched as a tarball pinned to **v1.15.2** with a SHA256 hash. If `https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz` is not reachable from the build host (corporate proxy, isolated network), mirror the same tarball internally and change the `URL` in `tests/CMakeLists.txt` to point at your mirror:

```cmake
FetchContent_Declare(
    googletest
    URL      https://your-mirror.example.com/googletest-1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
```

Keep the **same SHA256** so reproducibility is unaffected. No source patch is needed — just the URL.

### `ctest` runs but reports zero tests

`gtest_discover_tests` runs the test binary at configure time to enumerate test cases. If your environment requires extra runtime dependencies for that enumeration step (instrumentation wrappers, sandboxed runners), it can fail silently. Inspect the configure log for `gtest_discover_tests` lines and run the test executable directly (`build/tests/Debug/test_error.exe` on Windows, `build/tests/test_error` on Unix) to see whether the binary itself works.

### Cross-compilation

Not exercised by our local builds. The Phase 1 code is plain C++17 with no system-specific dependencies, so it should configure under a CMake toolchain file (`-DCMAKE_TOOLCHAIN_FILE=…`) in principle. Once Phase 8 lands (OSFZ via zlib), cross-builds may need additional plumbing for the zlib build step. If you cross-build today and run into something, please open an issue.
