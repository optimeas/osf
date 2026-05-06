# OSF — Python Implementation

![Status](https://img.shields.io/badge/status-in%20progress-orange.svg)
[![CI](https://github.com/optimeas/osf/actions/workflows/ci.yml/badge.svg)](https://github.com/optimeas/osf/actions/workflows/ci.yml)

## Target Platform

Data analytics, scientific computing, and AI/ML pipelines. The Python
bindings are the primary entry point for data exploration and the
foundation for ecosystem integrations (Arrow, PyTorch, TensorFlow,
LangChain).

The crate sits on top of the Rust [`osf-core`](../rust/osf-core/) library
via [PyO3](https://pyo3.rs) — see [DECISIONS §18](../../DECISIONS.md#18-rust-as-foundation-for-python).
One codebase, two audiences.

## Status

**In progress.** Reader, manager, and writer bindings are functional;
the Python wheel builds via [maturin](https://maturin.rs) with `abi3`
so a single artefact covers Python 3.9 / 3.10 / 3.11 / 3.12 / 3.13.

| Capability                                              | State                |
|---------------------------------------------------------|----------------------|
| `osf.load(path)` — read OSF or OSFZ                     | ✅                   |
| `osf.save(mgr, path)` — write OSF5                      | ✅                   |
| `DataManager.channel(name)` (DECISIONS §10)             | ✅                   |
| `DataManager.channels` / `channel_by_index`             | ✅                   |
| Channel `samples()` → NumPy array (numeric / GPS)       | ✅                   |
| Channel `samples()` → `list[str]` / `list[bytes]`       | ✅                   |
| Channel `timestamps_ns()` → NumPy `int64`               | ✅                   |
| Channel `segments` for equidistant channels             | ✅                   |
| `WriterBuilder` with chainable setters                  | ✅                   |
| Transparent OSFZ (gzip + zlib) on read                  | ✅                   |
| Type stubs (`*.pyi`) for IDE support                    | ✅                   |
| pandas `DataFrame` convenience                          | Pending (session 7b) |
| CI + wheel-build matrix + PyPI publishing               | Pending (session 8)  |

## Distribution name vs. import name

- **PyPI:** `pip install osfdata`
- **Python:** `import osf`

The split follows the established Python convention (scikit-learn
imports as `sklearn`, PyYAML imports as `yaml`, beautifulsoup4 imports
as `bs4`). The PyPI name `osf` is registered to an unrelated 2015
package; `osfdata` is the Optimeas distribution.

## Installation

Three installation paths, in increasing order of stability:

### Development build (current state)

Build the native extension from a source checkout. Required while the
package is not yet on PyPI / TestPyPI; also the right path for any
local Rust-side hacking.

```bash
git clone https://github.com/optimeas/osf
cd osf/implementations/python

# Create a virtual environment.
uv venv
.venv/Scripts/Activate.ps1            # Windows PowerShell
# source .venv/bin/activate           # Linux / macOS

# Install build + test tooling.
uv pip install maturin pytest

# Build the native extension and install it editably into the venv.
maturin develop --release

# Run the test suite.
pytest tests/
```

`maturin develop` produces an editable install — code changes to
`python/osf/*.py` and `python/osf/*.pyi` are picked up immediately;
Rust changes need another `maturin develop`.

### TestPyPI (after first release)

```bash
pip install --index-url https://test.pypi.org/simple/ \
            --extra-index-url https://pypi.org/simple/ \
            osfdata
```

The `--extra-index-url` is required because TestPyPI does not host
the runtime dependencies (`numpy` etc.); the extra index lets pip
pull those from production PyPI.

### PyPI (production, future)

```bash
pip install osfdata
```

Both forms install the same package. The PyPI distribution name is
`osfdata` (the short name `osf` is taken by an unrelated 2015
package); the Python import name is `osf` for brevity.

## Quick start

```python
import osf
import numpy as np

# Reader: convenience path
mgr = osf.load("examples/steam_loco.osf")
print(f"Channels: {len(mgr)}")
print(f"Compressed: {mgr.stats.compressed}")
print(mgr.stats)

# Channel access by name (DECISIONS §10)
ch = mgr.channel("GPS.PosFixMode")
print(f"{ch.name}: {ch.data_type}, {ch.sample_count} samples")

arr = ch.samples()           # NumPy float64 array
ts = ch.timestamps_ns()      # NumPy int64 array

# Equidistant segments are first-class
for seg in ch.segments:
    print(f"  start={seg.start_timestamp_ns} rate={seg.sample_rate_hz} n={seg.sample_count}")

# Writer: convenience path — round-trip an existing manager
osf.save(mgr, "out.osf")

# Writer: builder path — construct from scratch
b = osf.WriterBuilder().creator("my-app").tag("preview")
idx = b.add_channel(
    name="Sensor/Temp",
    data_type="double",
    channel_type="scalar",
    physical_unit="°C",
)
b.add_equidistant_segment(
    idx,
    start_ns=1_700_000_000_000_000_000,
    sample_rate_hz=1.0,
    values=np.array([18.4, 18.5, 18.6], dtype=np.float64),
)
b.write_to_file("synthetic.osf")
```

Both `osf.load()` and `osf.save()` always emit OSF5 (DECISIONS §6),
so an OSF4 source file becomes an OSF5 target after a round-trip.

## API surface

| Object                | Provides                                                                                      |
|-----------------------|-----------------------------------------------------------------------------------------------|
| `osf.load(path)`      | Open and parse an OSF or OSFZ file                                                            |
| `osf.save(mgr, path)` | Write a `DataManager` back as OSF5                                                            |
| `osf.DataManager`     | `channel(name)`, `channel_by_index(i)`, `channels`, `stats`, `len`                            |
| `osf.Channel`         | `index`, `name`, `data_type`, `channel_type`, `samples()`, `timestamps_ns()`, `segments`      |
| `osf.Segment`         | `start_timestamp_ns`, `sample_rate_hz`, `sample_count`                                        |
| `osf.ReaderStats`     | `compressed`, `compression_format`, channel/block counts, sizes, elapsed                      |
| `osf.WriterBuilder`   | Chainable file-info setters plus `add_channel`, `add_*_samples`, `write_to_file` (see stubs)  |
| `osf.OsfError`        | Single exception class for all reader / writer errors                                         |

NumPy is the data type for every numeric and `gpslocation` channel.
`gpslocation` arrives as a `(N, 3) float64` array with columns
`[latitude, longitude, altitude]`. `string` channels return
`list[str]`; `binary` channels return `list[bytes]`.

## Performance

`osf.load("examples/steam_loco.osf")` (123 channels, ~164 k samples)
measures **~3 ms** on a release-build extension on the dev box —
same order of magnitude as the underlying Rust read. Channel access
plus NumPy array conversion adds ~0.3 ms per channel. The clone
strategy (each `mgr.channel(name).samples()` call clones the
`Vec<T>` once) is fast enough that an `Arc<Channel>` optimisation is
unnecessary at this point.

## Spec revision tracked

OSF specification revision **2026-05-04** ([English](../../docs/en/osf_general.md),
[Deutsch](../../docs/de/osf_general.md)). All spec-level constraints
implemented in `osf-core` carry through automatically: removed
datatypes raise `OsfError`, deprecated channel-level fields produce a
log warning and are dropped, equidistant blocks limit to `float` /
`double`, and the writer never emits OSFZ.

## Dependencies

| Package          | Purpose                                            |
|------------------|----------------------------------------------------|
| `numpy>=1.20`    | Array data type for numeric channels               |

Build-time only:

| Crate              | Purpose                                          |
|--------------------|--------------------------------------------------|
| `pyo3 = "0.22"`    | Python C-API bindings (matched pair with numpy)  |
| `numpy = "0.22"`   | Rust → NumPy ndarray conversions                 |
| `osf-core` (path)  | The pure-Rust core library                       |

`pyo3` and `numpy` must agree on their major version per the
rust-numpy README; bumping one requires bumping the other.

## Next steps

1. **Session 7b** — pandas `DataFrame` convenience: build a DataFrame
   from a `DataManager` (one column per channel, optional time
   alignment).
2. **Session 8** — CI matrix building wheels for Linux / macOS /
   Windows × Python 3.9–3.13, TestPyPI upload, automated release on
   tag.

## Relationship to `python-osf`

`osfdata` is the modern successor to the existing
[python-osf](https://github.com/optimeas/python-osf) package. While
`python-osf` is a pure-Python implementation supporting OSF4 reading
only, `osfdata` provides:

- Full OSF4 and OSF5 support (read and write)
- Significantly higher performance via a Rust foundation
- Complete data type coverage including `binary`, `gpslocation`, and
  unsigned integers
- Compatibility with the current spec revision (2026-05-04)
- Transparent OSFZ decompression (zlib + gzip)

`python-osf` will be deprecated in favor of `osfdata` once feature
parity for all production use cases is verified.

## License

Apache 2.0. © 2026 Optimeas GmbH.
