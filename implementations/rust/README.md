# OSF — Rust Implementation

![Status](https://img.shields.io/badge/status-planned-lightgrey.svg)

## Target Platform

Systems programming, high-performance server applications, and embedded targets via `no_std`. Safe, zero-cost abstractions over OSF data.

## What This Implementation Will Provide

- **Reader**: full OSF4 and OSF5 reader; zero-copy where possible
- **Writer**: OSF5 writer
- `no_std` compatible writer for embedded targets (with `alloc`)
- Published as a crate on crates.io

## Status

**Planned.** Implementation has not started.

## Dependencies

- Rust 1.70+
- `serde` and `serde_json` for OSF5 header (optional feature flag)

## Notes

Rust is a natural fit for both the highest-performance desktop readers and for embedded writers where memory safety without a runtime is required.
