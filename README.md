# Open Streaming Format (OSF)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Status](https://img.shields.io/badge/status-active%20development-orange.svg)]()

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

See [`docs/`](docs/) for the full specification.

---

## Implementations

| Language | Platform | Status |
|----------|----------|--------|
| [Delphi](implementations/delphi/) | Windows desktop, industrial | In Progress |
| [C](implementations/c/) | Embedded + desktop, reference | Planned |
| [C++](implementations/cpp/) | Industrial measurement, Qt | Planned |
| [C#](implementations/csharp/) | Windows desktop, automation | Planned |
| [Python](implementations/python/) | Analytics, NumPy + pandas | Planned |
| [MicroPython](implementations/micropython/) | ESP32, RP2040 | Planned |
| [Java](implementations/java/) | Enterprise + Android | Planned |
| [Rust](implementations/rust/) | Systems, embedded | Planned |
| [MATLAB](implementations/matlab/) | Engineering analysis (reader) | Planned |
| [JavaScript](implementations/javascript/) | Browser + Node.js | Planned |

## Integrations

| Integration | Ecosystem |
|-------------|-----------|
| [Apache Arrow](integrations/arrow/) | Parquet, DuckDB, Polars, HuggingFace |
| [PyTorch](integrations/pytorch/) | Training pipelines |
| [TensorFlow](integrations/tensorflow/) | tf.data connectors |
| [MCP](integrations/mcp/) | LLM tool use (Claude, etc.) |
| [LangChain](integrations/langchain/) | RAG pipelines |

---

## Quick Start

OSF files begin with a metadata header (XML for OSF4, JSON for OSF5) followed by a stream of data blocks. A minimal OSF5 header looks like this:

```json
{
  "osf_version": 5,
  "channels": [
    { "id": 1, "name": "temperature", "unit": "°C", "type": "float32", "rate_hz": 100 }
  ]
}
```

Full examples and sample `.osf` files are provided in [`examples/`](examples/).

---

## Documentation

- [General OSF concepts](docs/osf_general.md)
- [OSF4 specification](docs/osf4.md)
- [OSF5 specification](docs/osf5.md)

---

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.

---

## License

Copyright 2026 Optimeas GmbH

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for the full text.
