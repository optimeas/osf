# OSF — Architecture and Design Decisions

This document records key decisions made during the design and development of the OSF project. Each entry explains what was decided and why, so future contributors understand the reasoning without having to reconstruct it from git history.

---

## 1. Two Format Versions (OSF4 and OSF5)

**Decision:** Support both OSF4 (XML header) and OSF5 (JSON header) in all reader implementations. Writers target OSF5 only, except where backward compatibility is explicitly required.

**Why:** OSF4 is already deployed in production measurement systems. Dropping reader support would break existing workflows. OSF5 is simpler to implement and parse, so new writers default to it. The two formats share the same binary data block structure, so a reader that handles both requires only a header parser for each.

---

## 2. Streaming-First Design

**Decision:** Writers must produce valid, partially-readable files at every point during writing. Readers must be able to decode whatever data is present even in a truncated file.

**Why:** Measurement systems can lose power or crash mid-recording. A file that is only readable if it is complete is a liability in field use. Streaming-first means partial files are always recoverable up to the last complete data block.

---

## 3. One Implementation Per Language Directory

**Decision:** Each language gets its own self-contained directory under `implementations/`. No shared code across language implementations.

**Why:** Cross-language shared code creates coupling that makes individual implementations harder to distribute, test, and maintain. Each implementation should be independently publishable as a package for its ecosystem (PyPI, npm, NuGet, crates.io, etc.).

---

## 4. Planned Implementations

The following language implementations are planned. Each targets a specific platform niche and is listed with its primary rationale.

| Language | Target Platform | Primary Rationale |
|---|---|---|
| Delphi | Windows desktop, industrial systems | Reference implementation; existing OSF4 ecosystem |
| C | Embedded and desktop | Lowest-level reference; baseline for porting |
| C++ | Industrial measurement, Qt | High-performance desktop; Qt ecosystem |
| C# | Windows desktop, automation | .NET and HMI ecosystem |
| Python | Data analytics, scientific computing | NumPy/pandas integration; prerequisite for AI integrations |
| Java | Enterprise, Android | JVM ecosystem; Android mobile logging |
| Swift | iOS, macOS, iPadOS | Apple ecosystem; field measurement and analysis apps |
| MicroPython | ESP32, RP2040 | Sensor node writer; minimal footprint |
| Rust | Systems, embedded | Memory-safe high-performance; no_std support |
| MATLAB | Engineering analysis | Reader-only; Simulink workflow integration |
| JavaScript | Browser, Node.js | Web dashboards; streaming over HTTP range requests |

---

## 5. Integrations Are Separate from Implementations

**Decision:** Bridges to external ecosystems (Arrow, PyTorch, TensorFlow, MCP, LangChain) live in `integrations/`, not in `implementations/python/`.

**Why:** Integrations have heavier dependencies (torch, tensorflow, pyarrow) that most users of the Python implementation do not need. Keeping them separate allows the core Python package to remain lightweight.

---

## 6. OSF5 Has No Trailer

**Decision:** OSF5 files do not include a trailer block. OSF4 files may include a trailer; readers must handle its presence or absence.

**Why:** The trailer in OSF4 was used for index acceleration and integrity verification. In OSF5, the JSON header carries all necessary metadata, and streaming-first design means the file is always readable without a trailer. Removing the trailer simplifies both writers and readers.

---

## 7. English Only in Code and Documentation

**Decision:** All code, comments, commit messages, and documentation in this repository must be in English.

**Why:** OSF is an open format intended for international use. English ensures that contributors and users from any background can read and contribute to the project.

---

## 8. Implementation Priority Order

Implementations will be developed in the following order. Priority is based on the size of the existing user base, the availability of contributors, and the strategic importance of the platform.

1. Delphi — reference implementation, already in progress
2. Python — required by all AI/data integrations
3. C — low-level reference for porting to other languages
4. C++ — industrial measurement systems
5. C# — Windows tooling and automation
6. JavaScript — web and Node.js ecosystem
7. Java — enterprise and Android
8. Swift — Apple ecosystem field and analysis apps
9. MicroPython — embedded sensor nodes
10. Rust — high-performance and embedded
11. MATLAB — engineering analysis (reader only)
