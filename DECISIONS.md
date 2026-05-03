# OSF Project – Architecture & Design Decisions

This document records the key decisions made during the design and setup of the `osf` open source project.
It serves as context for contributors and for AI-assisted development sessions.

---

## 1. Project Goal

The goal of this project is to maximize adoption of the **Open Streaming Format (OSF)** by providing
clean, well-documented, open source implementations in as many relevant programming languages and
ecosystems as possible.

The format itself is developed and maintained by **Optimeas GmbH**. The implementations are intentionally
open and free — anyone may use, modify, and distribute them under the Apache 2.0 license.

> **Vision:** OSF should become the format that carries measurement data seamlessly from sensor to
> AI training — without conversion losses through CSV or Parquet intermediates.

---

## 2. License

**Apache 2.0**, copyright 2026 Optimeas GmbH.

Chosen because:
- It allows anyone to use the code commercially without restrictions.
- It does not require derived works to be open source.
- It maximizes adoption and minimizes friction for integrators.

---

## 3. Repository Structure

```
osf/
├── docs/                   # OSF format specification (Markdown)
├── implementations/        # Standalone readers/writers per language
├── integrations/           # Bridges to existing ecosystems and frameworks
├── examples/               # Sample .osf files for testing
├── DECISIONS.md            # This file
├── CONTRIBUTING.md
├── CHANGELOG.md
└── README.md
```

### `implementations/` vs. `integrations/`

| Directory          | Purpose |
|--------------------|---------|
| `implementations/` | Standalone OSF reader/writer libraries in a specific programming language. No external framework required beyond the language runtime. |
| `integrations/`    | Bridges that connect OSF to existing ecosystems (ML frameworks, data pipelines, LLM tooling). These typically depend on a base implementation. |

---

## 4. Planned Implementations

| Language       | Target Platform                        | Status      | Notes |
|----------------|----------------------------------------|-------------|-------|
| Delphi         | Windows desktop, legacy systems        | In progress | First implementation; reference for the project |
| C              | Embedded + desktop                     | Planned     | Reference for low-level targets (STM32, ESP32, RTOS) |
| C++            | Industrial measurement, Qt ecosystem   | Planned     | Builds on C implementation |
| C# / .NET      | Windows desktop, industrial automation | Planned     | Relevant for Beckhoff, Siemens environments |
| Python         | Data analytics, scripting              | Planned     | NumPy + pandas integration; primary AI/ML entry point |
| MicroPython    | Embedded only                          | Planned     | Minimal footprint; targets ESP32, RP2040 |
| Java           | Enterprise, Android                    | Planned     | |
| Swift          | iOS / macOS / iPadOS                   | Planned     | Reader-focused; Apple ecosystem for field and analysis apps |
| Rust           | Systems programming, embedded          | Planned     | Growing relevance in embedded and cloud |
| MATLAB         | Engineering analysis                   | Planned     | Reader only |
| JavaScript/TS  | Browser + Node.js                      | Planned     | Web dashboards, cloud visualization |

---

## 5. Planned Integrations

| Integration    | Ecosystem                              | Notes |
|----------------|----------------------------------------|-------|
| Apache Arrow   | DuckDB, Polars, HuggingFace, Parquet   | OSF ↔ Arrow bridge; highest strategic value for AI pipelines |
| PyTorch        | ML training                            | `OSFDataset` implementing `torch.utils.data.Dataset` |
| TensorFlow     | ML training                            | `tf.data` connector |
| MCP Server     | LLM tooling (Claude, etc.)             | Enables LLMs to read and reason about OSF files directly |
| LangChain      | RAG pipelines                          | Document Loader for LangChain / LlamaIndex |

---

## 6. Coding Conventions (all languages)

- All code, comments, variable names, class names, and documentation must be written in **English**.
- Each implementation must be self-contained within its subdirectory.
- Every implementation directory must contain a `README.md` describing: purpose, status, dependencies, and usage examples.
- No implementation should depend on another implementation (exception: integrations may depend on the Python implementation).

---

## 7. OSF Format Versions

- **OSF4** — stable, XML metadata header, supported by all existing optiMEAS devices and tools.
- **OSF5** — in development, JSON metadata header, simplified control byte, no trailer, fully backward-compatible with OSF4.

All implementations must support OSF4 reading as a minimum.
OSF5 writing is the target for new implementations.
OSF5 readers must also handle OSF4 files (backward compatibility is mandatory).

The format version is detected automatically:
- Magic header starts with `OSF4`, `OSF5`, or `OCEAN_STREAMING_FORMAT4`
- First character after the magic header: `<` = XML (OSF4), `{` = JSON (OSF5)

---

## 8. Implementation Priority Order

1. **Delphi** — already in progress
2. **C** — broadest embedded reach, foundation for C++ port
3. **Python** — largest community, direct path to AI/ML integrations
4. **C++** — industrial and Qt ecosystem
5. **C#** — Windows tooling ecosystem
6. **Rust** — systems + embedded, growing community
7. **Java** — enterprise + Android
8. **Swift** — Apple ecosystem; field measurement and analysis apps
9. **MicroPython** — embedded, after C is stable
10. **JavaScript/TypeScript** — web and Node.js
11. **MATLAB** — reader only, engineering niche

Integrations (Arrow, PyTorch, TensorFlow, MCP, LangChain) follow after the Python implementation is stable.

---

## 9. Examples

The `examples/` directory will contain sample `.osf` files covering:
- Simple scalar time-stamped channels (double, int16)
- Equidistant channels
- Mixed channel types in one file
- Binary blobs (WAV audio, images)
- Large files for performance testing

These files are generated by the Delphi demo generator and will later be regenerated by each implementation as a correctness test.

---

## 10. AI-Assisted Development

This project uses Claude (Anthropic) for code generation and documentation. Each implementation is
developed in a separate chat session. To maintain context across sessions, every new session should
be initialized with:

- The format specification from `docs/`
- This `DECISIONS.md` file
- The `CONTRIBUTING.md` file
- The target language `README.md`

Suggested session opener:
> "Please read the OSF specification in docs/, DECISIONS.md, and CONTRIBUTING.md.
> We are now implementing OSF in [language]. Start with the reader for OSF4."
