# OSF Implementations

This directory contains standalone OSF reader and writer implementations in various programming languages. Each implementation is self-contained and does not depend on any other implementation in this repository.

| Language | Target Platform | Status | Notes |
|----------|----------------|--------|-------|
| [Delphi](delphi/) | Windows desktop, industrial measurement systems | Stable | Reference implementation; full OSF4/OSF5 reader + writer, `osftool` CLI, DUnitX suite. Generates the test files in `examples/generated/` |
| [C](c/) | Embedded systems and desktop | Planned | Reference implementation; target for low-level and resource-constrained environments |
| [C++](cpp/) | Industrial measurement, embedded + desktop | Beta | Standalone C++17; full OSF4/OSF5 reader (incl. OSFZ), typed `DataManager`, both OSF5 writers, the `osf-c` C ABI, and the crc integrity profile; CI on Linux/macOS/Windows |
| [Python](python/) | Data analytics, scientific computing | Beta | PyO3 bindings on osf-core (DECISIONS §18); PyPI distribution `osfdata`, import as `osf`; reader, writer, OSFZ, crc all live |
| [Java](java/) | Enterprise systems, Android | Beta | Java 21, JPMS; both OSF5 writers, transparent OSFZ, crc integrity profile; `osf-cli` + `osf-viewer` ([DECISIONS §21](../DECISIONS.md)) |
| [Rust](rust/) | Systems programming, embedded | Beta | Foundation for Python bindings (see [DECISIONS §18](../DECISIONS.md#18-rust-as-foundation-for-python)); full OSF4/OSF5 read + write + OSFZ + crc integrity profile |

---

## Status Definitions

| Status | Meaning |
|--------|---------|
| Planned | Directory and README exist; implementation not yet started |
| In Progress | Active development; not yet ready for production use |
| Beta | Feature-complete but not fully validated |
| Stable | Production-ready; breaking changes follow semantic versioning |
