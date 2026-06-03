# Open Streaming Format (OSF)

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/optimeas/osf/actions/workflows/ci.yml/badge.svg)](https://github.com/optimeas/osf/actions/workflows/ci.yml)
[![osfdata on TestPyPI](https://img.shields.io/badge/osfdata-TestPyPI%20v0.1.0-blue)](https://test.pypi.org/project/osfdata/)

OSF is an open, lightweight streaming format for time-series measurement and process data. It is designed to be written efficiently by embedded devices and read at high speed by desktop, server, and AI workloads — without lossy conversion through CSV or Parquet intermediates.

---

## Why OSF?

Industrial and scientific measurement systems produce continuous streams of time-series data: sensor readings, actuator states, images, and audio. Today this data travels through lossy intermediates — CSV exports strip metadata, Parquet conversions break streaming semantics — before reaching AI training pipelines.

OSF solves this by defining a format that is:

- **Streaming-first** — designed for sequential writing; readers can begin processing before the file is complete
- **Robust** — channel metadata is stored in a header so a truncated file is still partially readable
- **Flexible** — supports equidistant and time-stamped channels, scalars, vectors, matrices, and binary blobs (images, audio)
- **AI-ready** — native bridges to Apache Arrow, PyTorch, TensorFlow, and LLM tool ecosystems

---

## Format Versions

| Version | Metadata | Status |
|---------|----------|--------|
| OSF4 | XML header | Stable, widely deployed |
| OSF5 | JSON header | Active development, fully backward-compatible with OSF4 |

See [`docs/en/`](docs/en/) for the full specification. German version available under [`docs/de/`](docs/de/).

---

## Implementations

The Delphi reference implementation, the Rust foundation, and the Python bindings are usable today; the C++ implementation is in active development. The remaining languages follow the same architecture and are planned. Status legend: ✅ usable · 🚧 in active development · 📋 planned.

| Language | Platform | Status |
|----------|----------|--------|
| [Delphi](implementations/delphi/) | Windows desktop, industrial | ✅ Reference implementation; full OSF4/OSF5 reader + writer. Generates the test files in [`examples/generated/`](examples/generated/). |
| [Rust](implementations/rust/) (`osf-core`) | Systems, embedded, foundation for bindings | ✅ Read + write + OSFZ decompression; full OSF4/OSF5 support. |
| [Python](implementations/python/) (`osfdata`) | Analytics, NumPy integration | ✅ Pre-release v0.1.0 on [TestPyPI](https://test.pypi.org/project/osfdata/). Built on the Rust foundation via PyO3. |
| [C++](implementations/cpp/) | Industrial measurement, embedded + desktop | 🚧 In active development. Full OSF4/OSF5 reader, typed `DataManager`, and both OSF5 writers (streaming + block). OSFZ-on-read and the C ABI wrapper are pending. |
| [C](implementations/c/) | Embedded + desktop | 📋 Planned. |
| [C#](implementations/csharp/) | Windows desktop, automation | 📋 Planned. |
| [Java](implementations/java/) | Enterprise + Android | 📋 Planned; architecture decided ([DECISIONS.md §21](DECISIONS.md)). |
| [Swift](implementations/swift/) | iOS / macOS / iPadOS / watchOS | 📋 Planned. |
| [MATLAB](implementations/matlab/) | Engineering analysis (reader) | 📋 Planned. |
| [JavaScript](implementations/javascript/) | Browser + Node.js | 📋 Planned. |

**Which one should I try first?** For reading and analyzing OSF data today, the Python package is the fastest path — see [Quick Start](#quick-start-python) below. For native integration, use [Rust](implementations/rust/) (the foundation for the bindings) or the [Delphi](implementations/delphi/) reference implementation.

See [`DECISIONS.md`](DECISIONS.md) for the architectural rationale and implementation priority order.

## Integrations

| Integration | Ecosystem | Status |
|-------------|-----------|--------|
| [Apache Arrow](integrations/arrow/) | Parquet, DuckDB, Polars, HuggingFace | 📋 Planned. |
| [PyTorch](integrations/pytorch/) | Training pipelines | 📋 Planned. |
| [TensorFlow](integrations/tensorflow/) | tf.data connectors | 📋 Planned. |
| [MCP](integrations/mcp/) | LLM tool use (Claude, etc.) | 📋 Planned. |
| [LangChain](integrations/langchain/) | RAG pipelines | 📋 Planned. |

---

## Quick Start (Python)

The fastest way to read and analyze OSF data today is via the Python package `osfdata` on TestPyPI:

```bash
pip install --index-url https://test.pypi.org/simple/ \
            --extra-index-url https://pypi.org/simple/ \
            osfdata
```

Then in Python:

```python
import osf

mgr = osf.load("motorbike.osf")
print(f"{len(mgr)} channels in this file")

speed = mgr.channel("v_hinterrad")
print(f"{speed.sample_count:,} speed samples in {speed.physical_unit}")

values = speed.samples()           # NumPy array, dtype matches channel
timestamps = speed.timestamps_ns() # int64 nanoseconds since epoch
```

For more comprehensive examples — channel inventories, statistical analysis, writing OSF5 files — see the runnable scripts in [`implementations/python/examples/`](implementations/python/examples/).

The full Python documentation is at [`docs/en/integrations/python.md`](docs/en/integrations/python.md) and [`docs/de/integrations/python.md`](docs/de/integrations/python.md). For the build and release process, see [`implementations/python/BUILD.md`](implementations/python/BUILD.md).

---

## Sample Data

The [`examples/`](examples/) directory contains real and synthetic OSF files for testing and learning:

- `motorbike.osf` — 81 channels of real motorbike telemetry (speeds, temperatures, GPS, system status).
- `steam_loco.osf` — 123 channels from a steam locomotive recording (OSF4 format).
- `weather_station.osfz` — 28 channels, gzip-compressed OSFZ.
- [`generated/`](examples/generated/) — synthetic files covering all data types, produced by the Delphi reference implementation.

---

## Documentation

The specification is maintained in English under [`docs/en/`](docs/en/) and mirrored in German under [`docs/de/`](docs/de/).

- [Format introduction](docs/en/index.md) ([🇩🇪 Deutsch](docs/de/index.md))
- [General OSF concepts](docs/en/osf_general.md) ([🇩🇪 Deutsch](docs/de/osf_general.md))
- [OSF4 specification](docs/en/references/osf4.md) ([🇩🇪 Deutsch](docs/de/references/osf4.md))
- [OSF5 specification](docs/en/references/osf5.md) ([🇩🇪 Deutsch](docs/de/references/osf5.md))
- [Vector & matrix channels](docs/en/references/osf_vector_matrix.md) ([🇩🇪 Deutsch](docs/de/references/osf_vector_matrix.md))
- [Python integration](docs/en/integrations/python.md) ([🇩🇪 Deutsch](docs/de/integrations/python.md))

Project-wide architectural decisions are in [`DECISIONS.md`](DECISIONS.md). Release history per package is in the respective `CHANGELOG.md` files (per-package) plus the project-wide [`CHANGELOG.md`](CHANGELOG.md).

---

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.

---

## License

Copyright 2026 Optimeas GmbH

Licensed under the MIT License. See [LICENSE](LICENSE) for the full text.
