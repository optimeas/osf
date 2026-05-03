# OSF — C++ Implementation

![Status](https://img.shields.io/badge/status-planned-lightgrey.svg)

## Target Platform

Industrial measurement systems, high-performance desktop and server applications, and Qt-based tooling. Targets C++17 and later.

## What This Implementation Will Provide

- **Writer**: high-throughput streaming writer for OSF4 and OSF5
- **Reader**: full random-access and sequential reader
- Qt integration: `QIODevice`-compatible streams, signal/slot hooks for streaming data
- Optional: `std::span` and `std::ranges`-compatible channel data access

## Status

**Planned.** Implementation has not started.

## Dependencies

- C++17 standard library
- Optional: Qt 6 for Qt-integrated builds

## Notes

The C++ implementation is the primary target for desktop measurement software in the industrial automation and test equipment space.
