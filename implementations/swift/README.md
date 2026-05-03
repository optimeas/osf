# OSF — Swift Implementation

![Status](https://img.shields.io/badge/status-planned-lightgrey.svg)

## Target Platform

iOS, macOS, and iPadOS applications in the Apple ecosystem. Intended for field measurement tools, data visualization apps, and inspection applications that need to read OSF files on Apple devices.

## What This Implementation Will Provide

- **Reader** (primary): load OSF4 and OSF5 files into Swift data structures
- **Writer** (optional): OSF5 writer for apps that need to produce OSF files on-device
- Swift-idiomatic API using `async`/`await` for streaming reads
- Compatible with SwiftUI and UIKit data pipelines

## Status

**Planned.** Implementation has not started.

## Dependencies

- Swift 5.9+
- Foundation (no third-party dependencies)

## Notes

This implementation targets the Apple device ecosystem — iOS, macOS, and iPadOS. Embedded or bare-metal Apple targets are not in scope. For embedded use cases, use the C or MicroPython implementations instead.
