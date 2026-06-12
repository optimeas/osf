---
title: Implementations
description: OSF implementations per programming language — Delphi, Rust, Python, C++, Java and planned languages
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Implementation
  - Delphi
  - Rust
  - Python
  - C++
  - Java
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
---

🇩🇪 [German version](../../de/implementations/index.md)

## OSF implementations

OSF is an open format — deliberately kept simple enough to be implemented
standalone in any language. This chapter describes the **concrete
implementations** of the format per programming language: what they can do,
how to install or build them, and where the source code lives.

This is distinct from the [Integrations](../integrations/index.md) chapter,
which is about connecting to **ecosystems** (Arrow, PyTorch, MCP …), not
about the language implementations themselves.

### Status overview

Legend: ✅ available · 🚧 in active development · 📋 planned

| Implementation | Status | Summary |
|---|---|---|
| **[Delphi](delphi.md)** | ✅ | Reference implementation — full library, demos and the `osftool` CLI (Windows / RAD Studio) |
| **[Rust](rust.md)** (`osf-core`) | ✅ | Read, write and transparent OSFZ; also the foundation of the Python binding |
| **[Python](python.md)** (`osfdata`) | ✅ | PyO3 bindings over the Rust core, NumPy integration; see [Python integration](../integrations/python.md) |
| **[C++](cpp.md)** | ✅ | Standalone C++17 implementation — reader, both writers, C ABI; CI on Linux/macOS/Windows. Detailed developer handbook under [C++ in detail](cpp/architecture.md) |
| **[Java](java.md)** | 📋 | Architecture decided (Java 25, Maven, JPMS); no code yet |
| **[Other languages](planned.md)** | 📋 | C, C#, MATLAB, JavaScript/TypeScript, Swift — planned |

The repository on [GitHub](https://github.com/optimeas/osf) carries the most
up-to-date status.

### Where to start?

- **Analyse data** (Python/notebook, pandas, ML): the
  [`osfdata`](../integrations/python.md) package — `pip install osfdata`,
  load a file, channels as NumPy arrays.
- **Native integration / high performance** (server, embedded): the
  [Rust](rust.md) or [C++](cpp.md) implementation.
- **Windows tooling / reference behaviour**: the [Delphi](delphi.md)
  implementation together with the `osftool` command line.

All implementations read the same set of
[example files](../examples/osf_file_examples.md) and follow the same
semantic rules of the specification.
