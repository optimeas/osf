# OSF — Swift Implementation

![Status](https://img.shields.io/badge/status-planned-lightgrey.svg)

## Target Platform

iOS, macOS, iPadOS, and watchOS applications in the Apple ecosystem. Intended for field measurement tools, data visualization apps, and inspection applications that need to read OSF files on Apple devices, as well as sensor-logging apps on iPhone and Apple Watch that write OSF files.

## What This Implementation Will Provide

- **Reader**: load OSF4 and OSF5 files into Swift data structures; primary use case on Mac, iPad, and iPhone
- **Writer**: streaming OSF5 writer for sensor data capture on iPhone and Apple Watch
- Swift-idiomatic API using `async`/`await` for streaming reads
- Compatible with SwiftUI and UIKit data pipelines

## Status

**Planned.** Implementation has not started.

## Dependencies

- Swift 5.9+
- Foundation (no third-party dependencies)

## Notes

Embedded or bare-metal Apple targets are not in scope. For embedded use cases, use the C or MicroPython implementations instead.

The writer targets the embedded-style streaming mode defined in the project decisions: iPhone and Apple Watch write sample by sample without buffering all data in RAM, matching the same pattern as C and MicroPython on microcontrollers. Mac and iPad use the reader and do not require the streaming writer.
