# OSF — Delphi Implementation

![Status](https://img.shields.io/badge/status-in%20progress-orange.svg)

## Target Platform

Windows desktop and industrial measurement applications built with Delphi (RAD Studio). Suitable for data acquisition systems, HMI applications, and test equipment software.

## What This Implementation Provides

- **Writer**: stream OSF4 and OSF5 files with equidistant and time-stamped channels
- **Reader**: read and decode OSF4 and OSF5 files; random access to channel metadata
- Support for scalar values, vectors, matrices, and binary blobs

## Status

**In progress.** Core reader and writer classes are under active development.

## Dependencies

- Delphi (RAD Studio) — no third-party libraries required for the core implementation
- Standard Delphi RTL (classes, streams, XML parser for OSF4 metadata, JSON for OSF5)

## Structure

```
delphi/
  src/        — source units
  tests/      — DUnit/DUnitX test projects
  examples/   — example projects demonstrating read and write
```
