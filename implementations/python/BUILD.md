# Building and Releasing `osfdata`

This document explains how `osfdata` is built, tested, and released — from local development setup all the way to publishing wheels on PyPI. It is written for developers who have experience with C, C++, Rust, Delphi, or Java but may be new to Python's packaging conventions or to the PyO3 + maturin toolchain.

The text is intentionally verbose and explanatory rather than terse. You should be able to read this top-to-bottom once and understand both *what* happens and *why* each step exists.

---

## 1. The big picture

`osfdata` is a Python package whose performance-critical code is written in Rust. The Python side is a thin wrapper that exposes Rust functions, structs, and enums to Python code. From a Python user's perspective, `import osf` looks like any other Python module — but underneath, it loads a compiled binary file (`.pyd` on Windows, `.so` on Linux, `.dylib` on macOS) that contains native machine code.

This combination delivers two properties at once: Rust's performance and safety for the heavy lifting (parsing OSF binary blocks, decompressing OSFZ, managing typed channels), and Python's ease of use for the application layer (reading data into NumPy arrays, integrating with pandas, writing analysis scripts).

The toolchain that makes this possible has four main pieces:

| Piece | Role |
|---|---|
| **`osf-core`** | The pure-Rust library that does all the actual work. Lives at `implementations/rust/osf-core/`. Has no Python dependencies. |
| **`pyo3`** | A Rust crate that provides the `#[pyclass]`, `#[pyfunction]`, and `#[pymodule]` macros. These convert ordinary Rust types and functions into something Python can call. |
| **`maturin`** | A build tool that knows how to take a PyO3-using Rust crate and produce a Python wheel. Replaces `setuptools` + custom build scripts. |
| **`numpy` crate** | A second Rust crate that provides zero-copy interop between Rust `Vec<T>` and NumPy `ndarray`. Lets us return large arrays to Python without copying. |

The Python-facing crate that ties these together lives at `implementations/python/`. It is a Rust crate of type `cdylib` (C-compatible dynamic library) that depends on `osf-core`, wraps each part of its API in PyO3 macros, and is compiled by maturin into a Python wheel.

---

## 2. Local development setup

The first time you set up a development machine, install these tools in this order. The order matters because Rust and Python both need a C compiler available, and Rust looks for the linker that comes with that compiler.

### 2.1 C/C++ compiler infrastructure

**Windows:** Install Visual Studio Community (the free edition is fine for individual developers, open-source projects, and small teams) with the workload "Desktop development with C++", which is required for Rust's MSVC toolchain.

**Linux:** Install `build-essential` (Debian/Ubuntu) or the equivalent group package (`Development Tools` on Fedora). This brings GCC and the necessary headers.

**macOS:** Install Xcode Command Line Tools with `xcode-select --install`. This brings clang and the macOS SDK.

You will not invoke the C compiler directly. It exists for Rust's linker step and for any C dependencies that crates pull in transitively.

### 2.2 Rust toolchain

Install through `rustup` from https://rustup.rs. On Windows, run `rustup-init.exe` and select option 1 (default installation). This installs the stable Rust toolchain, including:

- `rustc` — the Rust compiler.
- `cargo` — Rust's build tool, package manager, and test runner all in one.
- The standard library and platform-specific support files.

On Windows, rustup will choose the MSVC toolchain by default, which links against Visual Studio's runtime. That is what we want.

Verify after a fresh shell session:

```bash
rustc --version
cargo --version
```

### 2.3 Python and uv

A modern Python (3.9 or newer) is required. The system Python that ships with macOS or comes with most Linux distributions works.

We strongly recommend installing **uv** alongside the system Python. `uv` is a fast Python package manager (written in Rust, ironically) that replaces `pip`, `venv`, `pyenv`, and several other tools. Install it from https://docs.astral.sh/uv. On Windows:

```powershell
powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
```

`uv` lets us create virtual environments and install packages much faster than the traditional tools, and it is now the de facto standard in the Python ecosystem.

### 2.4 maturin

`maturin` is the build tool that creates wheels from PyO3 projects. Install it as a system-wide tool (isolated, but globally callable):

```bash
uv tool install maturin
```

Verify with `maturin --version`.

### 2.5 IDE

For day-to-day development, VS Code with the `rust-analyzer` extension provides excellent Rust support, including type information, diagnostics, and refactoring. The Python and Pylance extensions cover the Python side. The Even Better TOML extension is useful for reading and editing `Cargo.toml` and `pyproject.toml`. The Dependi extension flags outdated dependency versions inline.

You can also use any editor or IDE you prefer; rust-analyzer works well in JetBrains products, Vim, Emacs, and others.

---

## 3. How Rust code becomes Python-callable

This is the conceptual heart of the build, and the part that surprises developers coming from C, Java, or Delphi backgrounds. The mechanism is straightforward once you have seen it once.

### 3.1 The PyO3 macros

PyO3 is a Rust crate whose only job is to provide macros that make Rust types and functions callable from Python. The three macros you will see most often are:

- `#[pyfunction]` — marks a free Rust function as Python-callable.
- `#[pyclass]` — marks a Rust struct as a Python class.
- `#[pymethods]` — marks an `impl` block whose methods become methods of the Python class.
- `#[pymodule]` — marks a Rust function that defines the top-level module structure.

Here is a minimal example, drawn from our actual code:

```rust
use pyo3::prelude::*;

#[pyclass(name = "Channel")]
pub struct PyChannel {
    inner: osf_core::data_channel::Channel,
}

#[pymethods]
impl PyChannel {
    #[getter]
    fn name(&self) -> &str {
        self.inner.name()
    }

    fn samples(&self, py: Python<'_>) -> PyResult<PyObject> {
        // ... convert internal Rust Vec into a NumPy array ...
    }
}

#[pymodule]
fn _osf(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_class::<PyChannel>()?;
    Ok(())
}
```

The `#[pyclass(name = "Channel")]` line says: this Rust struct shall appear in Python as a class called `Channel`. The `#[getter]` annotation says: when Python code reads `channel.name`, call this Rust method.

When `cargo` compiles this code, the macros expand at compile time into a large amount of additional Rust code that registers the class with the Python interpreter, manages reference counting, converts arguments, and so on. None of this expansion is visible in the source — you write the small annotated version, and the macro produces the verbose machinery.

### 3.2 The crate type that matters: cdylib

Most Rust libraries compile to `.rlib` files, a Rust-internal format that other Rust crates link against. For Python, we need something different: a **dynamic library with a C-compatible ABI** that the Python interpreter can load at runtime.

This is configured in `Cargo.toml` of the Python-binding crate:

```toml
[lib]
name = "osf"
crate-type = ["cdylib"]
```

`cdylib` tells Rust to produce, on each platform, the kind of file that an external runtime (Python in our case, but it could also be a C program or any FFI-capable language) can load:

- Windows: `osf.dll` — but renamed to `osf.pyd` for Python.
- Linux: `libosf.so`.
- macOS: `libosf.dylib`.

These files contain Rust code (and all its statically-linked dependencies) compiled to native machine code.

### 3.3 Static linking inside the wheel

A subtle but important property: every Rust crate that `osf-core` and the binding crate depend on is **statically linked into the final cdylib**. So `serde_json`, `quick-xml`, `flate2`, `byteorder`, the PyO3 runtime itself — they all get baked into a single binary file that ships in the wheel.

The wheel does dynamically link against two things only: the system C runtime (`vcruntime140.dll` on Windows, `libc.so` on Linux) and the Python runtime (`python3.dll` on Windows, etc.). Both are guaranteed to be present on any system that has Python installed, so the wheel works without further dependencies.

This is why the wheel is around 800 KB despite containing a complete OSF reader, writer, decompression, JSON parser, XML parser, and NumPy bindings.

### 3.4 The Python-side wrapper

The compiled native module is the heart, but Python users do not import it directly. Convention dictates that a thin Python file sits on top, re-exports the native symbols, and provides a clean public API.

In our project, the layout is:

```
implementations/python/
├── src/                    — Rust binding code (.rs files)
│   ├── lib.rs              — defines the #[pymodule]
│   ├── manager.rs
│   ├── channel.rs
│   └── ...
├── python/
│   └── osf/
│       ├── __init__.py     — Python wrapper, re-exports from _osf
│       └── py.typed        — marker for PEP 561 type stub support
└── pyproject.toml
```

When a wheel is built, the contents of `python/osf/` are placed alongside the compiled native module (`_osf.pyd` or `_osf.so`) inside the wheel. After installation, `import osf` triggers `python/osf/__init__.py`, which in turn imports from `_osf`, which is the native module.

This indirection lets us add pure-Python convenience functions in `__init__.py` later (e.g. for pandas integration) without touching the native code.

---

## 4. The build process

Now that the structure is clear, the build process becomes simple.

### 4.1 What `maturin develop` does

For local development, the canonical command is:

```bash
cd implementations/python
maturin develop --release
```

This invokes the following sequence:

1. Read `pyproject.toml` and `Cargo.toml` to understand the project.
2. Run `cargo build --release` to compile the Rust code into a cdylib.
3. Copy the cdylib into the active Python virtual environment under the correct name (`_osf.pyd` on Windows, `_osf.so` on Linux/macOS).
4. Install the contents of `python/osf/` into the same virtual environment.

After this, `python -c "import osf"` works inside that venv. `--release` enables compiler optimizations; without it, the binary is much slower and only useful for fast iteration when correctness, not speed, matters.

### 4.2 What `maturin build` does

For producing a wheel intended for distribution:

```bash
cd implementations/python
maturin build --release --out dist
```

Same compilation as `develop`, but instead of installing into a venv, the output is a `.whl` file in `dist/`. This wheel is a ZIP archive containing:

- The compiled native module.
- The Python wrapper code from `python/osf/`.
- A `*.dist-info/` directory with metadata (license, version, dependencies, the Python and platform tags).

The wheel filename encodes important information:

```
osfdata-1.1.0-cp39-abi3-win_amd64.whl
        ^^^^^ ^^^^ ^^^^ ^^^^^^^^^
        version  py-tag abi-tag platform-tag
```

`cp39-abi3` means: built against CPython 3.9 ABI, but using the stable abi3 ABI which is compatible with all Python versions from 3.9 onward. `win_amd64` means: this wheel runs on 64-bit Windows. A wheel with a different platform tag would not install on this system.

### 4.3 Why abi3 matters

Without the `abi3` feature, we would need to build a separate wheel for each Python minor version: 3.9, 3.10, 3.11, 3.12, 3.13 — five wheels per platform, twenty-five wheels total for our five-platform matrix. That would be both slower in CI and more error-prone.

`abi3` is a stable subset of the Python C API guaranteed to remain compatible across minor versions. By limiting ourselves to abi3, we build one wheel per platform that works for Python 3.9 and all later minor versions. Total wheel count drops to five.

This is configured in `Cargo.toml`:

```toml
[dependencies]
pyo3 = { version = "0.22", features = ["extension-module", "abi3-py39"] }
```

The minor cost is that a few PyO3 features (mostly performance optimizations and access to internal Python implementation details) are unavailable. For our use case, that cost is invisible.

### 4.4 The sdist (source distribution)

In addition to wheels, we also build a source distribution:

```bash
maturin sdist --out dist
```

This produces `osfdata-1.1.0.tar.gz`, a tarball containing all source files needed to build the project from scratch. Users on platforms we did not pre-build wheels for (FreeBSD, exotic ARM variants) can install with `pip install osfdata` and pip will fall back to the sdist, compile it on the fly, and produce a wheel locally.

Compiling from sdist requires the user to have a Rust toolchain installed. For mainstream platforms, the pre-built wheels mean users never see this complexity.

---

## 5. Local testing

Before any code goes into CI, it should pass two layers of tests locally.

### 5.1 Rust tests

```bash
cd implementations/rust
cargo test --release
```

This runs all unit tests in `osf-core` plus the integration tests in `osf-core/tests/`. Currently grey numbers: 123 unit tests plus 16 integration tests, covering the magic header parser, both metablock parsers, the block reader, the data manager, the writer, and OSFZ decompression. The integration tests run against the 19 sample files in `examples/`.

The Rust tests do not exercise the Python bindings at all. They validate the underlying library.

### 5.2 Python tests

```bash
cd implementations/python
maturin develop --release
uv pip install pytest numpy
pytest tests/
```

These are pytest tests that import the freshly built native module and exercise the user-visible API: loading files, accessing channels, reading samples as NumPy arrays, the writer round-trip. Currently 13 tests, all passing in well under a second.

The two test layers serve different purposes. The Rust tests catch logic errors in the underlying library. The Python tests catch problems in the binding layer — incorrect type conversions, lifetime issues, NumPy interop bugs.

---

## 6. The CI pipeline

Local testing is the first line of defense; the GitHub Actions CI is the second. It exists because:

- Developers test on one platform; users run on many. CI tests on five.
- Rebuilding wheels on every push catches breakage early.
- A green CI badge signals to outside observers that the project is maintained.

Two workflow files drive this:

### 6.1 `.github/workflows/ci.yml`

Runs on every push to `main` and on every pull request. Its jobs are:

- **`test-rust`** — runs `cargo test` and `cargo clippy` on Linux. Fast, fails first if Rust code itself is broken.
- **`build-wheels`** — a 5-element matrix that builds wheels on each target platform (Linux x86_64, Linux aarch64, macOS x86_64, macOS arm64, Windows x86_64). On native architectures, it also installs the wheel and runs pytest on it. On cross-built platforms (Linux aarch64 via QEMU on an x86_64 host), the install-and-test step is skipped because the wheel's architecture does not match the runner.
- **`build-sdist`** — builds the source distribution.
- **`summary`** — reports which artifacts were produced.

The matrix uses `fail-fast: false` so that a Linux-aarch64 failure does not cancel the macOS or Windows builds; each platform's outcome is independent.

### 6.2 `.github/workflows/release.yml`

Triggered only by Git tags matching `v*` (e.g. `v1.1.0`, `v1.2.0`). Same build matrix as `ci.yml`, plus an additional job:

- **`publish-pypi`** — downloads all built wheels and the sdist, then publishes them to production PyPI using Trusted Publishing. It runs only for `refs/tags/v*` and declares `permissions: id-token: write`, which is what mints the OIDC token.

Publishing is live: a `v*` tag push performs a real upload. Since a published version can never be replaced, treat pushing a tag as the irreversible step of a release.

### 6.3 Caching

Without caching, every CI run would download and recompile every Rust dependency from scratch, which takes minutes. The `Swatinem/rust-cache@v2` action caches the Cargo registry and `target/` directory between runs, reducing incremental builds from minutes to seconds.

### 6.4 Plat detection and the cross-build special case

Linux aarch64 wheels are built using QEMU emulation on an x86_64 GitHub runner. The build itself works, but the resulting aarch64 wheel cannot be installed and tested on the x86_64 host afterwards. We handle this with a `cross: true` matrix flag that turns off the test-after-build step for cross-built wheels. Native ARM testing is still covered by the macOS arm64 runner, which is sufficient to detect ARM-specific issues.

If GitHub releases native Linux ARM runners and they prove stable, we can switch and remove the QEMU complexity. For now, QEMU is the established and maintained path.

---

## 7. The release pipeline: publishing to PyPI

Building wheels in CI is one thing; getting them to users is another. Wheels are distributed through PyPI, the Python Package Index.

### 7.1 What PyPI is

PyPI (https://pypi.org) is the central registry for Python packages. When a user runs `pip install osfdata`, pip queries PyPI for the package, finds the appropriate wheel for the user's platform, downloads it, and installs it. PyPI is the equivalent of crates.io for Rust, npm for JavaScript, or Maven Central for Java.

`osfdata` publishes to production PyPI: <https://pypi.org/project/osfdata/>. Earlier pre-releases went to a separate test index during stabilization; that phase ended with the 1.1.0 release and nothing in this repository targets a test index any more.

### 7.2 Trusted Publishing

Historically, publishing to PyPI required an API token: a long secret string that the publisher pasted into GitHub's secrets and that gave permission to upload anything. This had two drawbacks: tokens leak, and they live forever until manually revoked.

Trusted Publishing replaces tokens with OIDC. The flow:

1. On PyPI, we tell the index: "Workflow `release.yml` in repository `optimeas/osf` is allowed to publish package `osfdata`."
2. When the workflow runs, GitHub gives it a short-lived, cryptographically signed token (an OIDC token) that proves the workflow is in fact running where it claims.
3. The publish step presents this token to PyPI, which verifies the signature and grants the upload.

There are no long-lived secrets in the repository. The trust relationship is between PyPI and the specific workflow file path, not a person and a password.

The configuration on PyPI is a one-time manual step done in the web UI under Account Settings → Publishing → Add a new pending publisher. For our project:

- Project name: `osfdata`
- Owner: `optimeas`
- Repository: `osf`
- Workflow: `release.yml`
- Environment: empty (we do not use environment gates)

A "pending publisher" is one that activates on first successful upload. After that, it appears in the active publishers list.

The claims are matched **exactly**, and that includes the environment. Our publish job declares no `environment:`, so its token says `environment: MISSING`; entering any value in that field would make every upload fail with `invalid-publisher`. The publisher must also exist *before* the first upload, since activating it is what claims the project name. `RELEASE.md` documents the recovery path — nothing is uploaded on such a failure, so re-running the failed job after fixing the publisher is enough, without a new tag.

### 7.3 The release sequence

When ready to release, the steps are:

1. Bump the version number in two files (kept in sync):
   - `implementations/python/Cargo.toml` → `[package].version`
   - `implementations/python/pyproject.toml` → `[project].version`
2. Update `CHANGELOG.md` with a description of what changed.
3. Commit and push to `main`.
4. Wait for `ci.yml` to go green.
5. Create and push a tag:
   ```bash
   git tag v1.1.0
   git push origin v1.1.0
   ```
6. The tag push triggers `release.yml`, which builds wheels for the four `(os, target)` pairs, builds the sdist, and uploads everything to PyPI.
7. After a few minutes, verify in a fresh virtual environment:
   ```bash
   pip install osfdata
   python -c "import osf; print(osf.__version__)"
   ```
   No extra index is needed: NumPy and every other runtime dependency resolve from PyPI itself.

`RELEASE.md` is the operational checklist for this sequence; this section explains what the steps do.

---

## 8. The complete change-to-release workflow

This section answers the question "what do I do, step by step, to take a Rust change all the way to a published release?". It assumes everything is already set up and CI is healthy.

### 8.1 The cycle

1. **Make the change in Rust.** Edit code in `implementations/rust/osf-core/` or `implementations/python/src/`. Run `cargo build` to verify it compiles.
2. **Run Rust tests locally:**
   ```bash
   cd implementations/rust
   cargo test --release
   cd ..
   ```
3. **Rebuild and test the Python wheel locally:**
   ```bash
   cd implementations/python
   maturin develop --release
   pytest tests/
   cd ..
   ```
4. **If both pass, commit:**
   ```bash
   git add <changed files>
   git commit -m "feat(rust): clear description of what changed"
   ```
5. **Push to main:**
   ```bash
   git push origin main
   ```
6. **Wait for CI** at https://github.com/optimeas/osf/actions. If green, the change is safe across all five target platforms. If red, read the logs, fix, repeat.
7. **Repeat steps 1-6** for as many changes as belong in the next release.
8. **When ready to release**, bump the version in `Cargo.toml` and `pyproject.toml`, update `CHANGELOG.md`, commit, push.
9. **Wait for CI to confirm** the version-bump commit is green.
10. **Tag and push the tag** — this is the irreversible step:
    ```bash
    git tag v1.1.1
    git push origin v1.1.1
    ```
11. **Watch the release workflow** at https://github.com/optimeas/osf/actions. It builds wheels for all platforms and uploads to PyPI.
12. **Verify the upload** in a fresh virtual environment:
    ```bash
    uv venv /tmp/verify-release
    source /tmp/verify-release/bin/activate    # on Linux/macOS
    # or: /tmp/verify-release/Scripts/Activate.ps1 on Windows
    pip install osfdata==1.1.1
    python -c "import osf; print(osf.__version__)"
    ```
    The version printed should match the tag. Verify against a *fresh* environment installing from the index — not a local `maturin develop` build, which would prove nothing about what users receive.
13. **If verification fails**, investigate. A PyPI version can never be re-uploaded or overwritten, even after deleting it — bump to `1.1.2` and release again. `RELEASE.md` covers yanking a bad release, which hides it from new installs without breaking users who pinned it.

### 8.2 Documentation-only changes

For changes that touch only documentation (README, comments, this file), the same change-to-release cycle in 8.1 still applies — there is currently no `paths:` filter on CI, so a docs-only push runs the full Rust test + five-platform wheel build + sdist matrix.

This is intentional for now: the Rust tests run in under a minute, and the wheel builds reuse the Rust cache, so a docs-only push still completes in a few minutes overall. If CI minutes become tight in the future, a `paths-ignore:` filter on `ci.yml` can skip the wheel matrix for pushes that only touch `*.md` or `docs/**`.

A docs-only change does not need a release in any case; documentation lives in the repository, not in the wheel.

### 8.3 What can go wrong, and where to look

| Symptom | Likely cause | Where to look |
|---|---|---|
| `cargo test` fails locally | Rust logic bug | The failing test's output |
| `pytest` fails locally but `cargo test` passes | Binding layer bug (PyO3) | The pytest output; look for `PanicException`, NumPy dtype mismatches |
| CI fails on one platform only | Platform-specific issue (path separators, line endings, missing dep) | The platform's job logs in GitHub Actions |
| CI fails on all platforms identically | Code regression that local tests missed | Re-run local tests in a fresh checkout to reproduce |
| Wheel uploads but `pip install` fails | Wheel platform tag mismatch | Compare the wheel filename with the target system's tag |
| Publish job fails with `invalid-publisher` | Trusted Publisher missing, or its environment field does not match the workflow | PyPI Account Settings → Publishing; the claims are printed in the failed job's log |

---

## 9. Glossary

For readers coming from other ecosystems, some terms may be unfamiliar.

- **Wheel (`.whl`)** — A pre-built Python package format. Roughly analogous to a `.deb` or `.rpm`: a ZIP archive with metadata and binaries, ready to be installed without further compilation.
- **Sdist (`.tar.gz`)** — A source distribution. The fallback when no wheel is available; users build from it locally.
- **PyPI** — The Python Package Index. Where wheels and sdists are published for the world to install with `pip install`.
- **Trusted Publishing** — Uploading via a short-lived OIDC token minted by the CI provider, instead of a long-lived API token stored as a repository secret.
- **abi3** — A stable subset of the Python C API. Allows one binary to work across multiple Python minor versions.
- **Crate** — A Rust package. Either a library (`.rlib`) or a binary or, in our case, a `cdylib` (dynamic library with C ABI).
- **cdylib** — Crate type that produces `.dll`/`.so`/`.dylib` files loadable from non-Rust code, including Python.
- **Cargo** — Rust's build tool, package manager, and test runner. Equivalent in scope to Maven for Java or npm for JavaScript.
- **PyO3** — A Rust crate that provides macros for exposing Rust types and functions to Python.
- **maturin** — A build tool that produces Python wheels from PyO3 projects. Replaces setuptools for this kind of project.
- **OIDC (OpenID Connect)** — A standard authentication protocol. Used by GitHub Actions to prove its identity to PyPI without long-lived tokens.
- **Trusted Publisher** — A PyPI configuration that authorizes a specific GitHub Actions workflow to publish a specific package via OIDC.
- **manylinux** — A Linux wheel format that runs on many Linux distributions. Built in a controlled Docker container with old enough glibc to be widely compatible.
- **GIL (Global Interpreter Lock)** — A Python lock that allows only one thread to execute Python code at a time. Rust code can release the GIL with `py.allow_threads`, allowing other Python threads to run during long Rust operations.

---

## 10. Reference: directory structure

For quick orientation, here is what lives where in the `implementations/python/` tree:

```
implementations/python/
├── Cargo.toml              — Rust crate config: dependencies, crate-type=cdylib
├── Cargo.lock              — locked Rust dependency versions (committed)
├── pyproject.toml          — Python package config: maturin as build backend
├── README.md               — user-facing documentation
├── BUILD.md                — this file
├── RELEASE.md              — release procedure and Trusted Publisher setup
├── src/                    — Rust binding code
│   ├── lib.rs              — defines the Python module and registers all classes
│   ├── manager.rs          — PyDataManager wrapper for osf_core::DataManager
│   ├── channel.rs          — PyChannel, PySegment, PyStats
│   ├── writer.rs           — PyWriterBuilder
│   ├── numpy_convert.rs    — NumericValues ↔ NumPy array converters
│   └── error.rs            — Rust error → Python exception conversion
├── python/
│   └── osf/                — Python-side wrapper
│       ├── __init__.py     — re-exports from the native _osf module
│       ├── _osf.pyi        — type stubs for IDE support
│       └── py.typed        — PEP 561 marker
├── tests/                  — pytest-based tests
└── dist/                   — build output (wheels and sdist), gitignored
```

The corresponding Rust library lives at `implementations/rust/osf-core/`. Changes there are picked up automatically because `Cargo.toml` references it via path:

```toml
[dependencies]
osf-core = { path = "../rust/osf-core" }
```

---

## 11. Further reading

If you want to go deeper into any part of this stack:

- PyO3 user guide: https://pyo3.rs
- maturin documentation: https://www.maturin.rs
- Python Packaging User Guide: https://packaging.python.org
- Trusted Publishing on PyPI: https://docs.pypi.org/trusted-publishers/
- abi3 and limited API: https://docs.python.org/3/c-api/stable.html
- The Rust Book: https://doc.rust-lang.org/book/
- cibuildwheel (alternative to maturin-action): https://cibuildwheel.pypa.io/

For project-specific decisions and rationale, see `DECISIONS.md` at the repository root.
