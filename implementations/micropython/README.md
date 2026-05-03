# OSF — MicroPython Implementation

![Status](https://img.shields.io/badge/status-planned-lightgrey.svg)

## Target Platform

Embedded microcontrollers running MicroPython: ESP32, RP2040 (Raspberry Pi Pico), and similar. Minimal memory footprint is the primary design constraint.

## What This Implementation Will Provide

- **Writer only**: sequential OSF5 writing to a file or stream; no reader (embedded targets write, desktop reads)
- No dynamic memory allocation beyond MicroPython's own allocator
- Works with `uos.open()` and any MicroPython stream

## Status

**Planned.** Implementation has not started.

## Dependencies

- MicroPython (tested target: ESP32 and RP2040 ports)
- `ujson` (built-in to MicroPython) for OSF5 header serialization

## Notes

The MicroPython implementation is writer-only by design. Sensor nodes write OSF files to flash or SD card; the desktop implementation reads them. Reader functionality is out of scope for this implementation.
