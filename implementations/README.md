# OSF Implementations

This directory contains standalone OSF reader and writer implementations in various programming languages. Each implementation is self-contained and does not depend on any other implementation in this repository.

| Language | Target Platform | Status | Notes |
|----------|----------------|--------|-------|
| [Delphi](delphi/) | Windows desktop, industrial measurement systems | In Progress | Full reader and writer for OSF4 and OSF5 |
| [C](c/) | Embedded systems and desktop | Planned | Reference implementation; target for low-level and resource-constrained environments |
| [C++](cpp/) | Industrial measurement, Qt ecosystem | Planned | High-performance desktop and server use; Qt integration |
| [Python](python/) | Data analytics, scientific computing | In Progress | PyO3 bindings on osf-core (DECISIONS §18); PyPI distribution `osfdata`, import as `osf`; reader, writer, OSFZ all live |
| [Java](java/) | Enterprise systems, Android | Planned | JVM ecosystem; Android mobile data logging |
| [Rust](rust/) | Systems programming, embedded | In Progress | Foundation for Python bindings (see [DECISIONS §18](../DECISIONS.md#18-rust-as-foundation-for-python)); magic-header parser landed |

---

## Status Definitions

| Status | Meaning |
|--------|---------|
| Planned | Directory and README exist; implementation not yet started |
| In Progress | Active development; not yet ready for production use |
| Beta | Feature-complete but not fully validated |
| Stable | Production-ready; breaking changes follow semantic versioning |
