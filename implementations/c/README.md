# OSF — C Implementation

![Status](https://img.shields.io/badge/status-planned-lightgrey.svg)

## Target Platform

Embedded systems (bare-metal and RTOS) and desktop. This is the **reference implementation** for all low-level targets. It is intended to be compilable with a standard C99 compiler with minimal dependencies.

## What This Implementation Will Provide

- **Writer**: sequential writing of OSF4 and OSF5 files; suitable for resource-constrained environments
- **Reader**: full OSF4 and OSF5 decoding on desktop; partial decoding on embedded targets
- No dynamic memory allocation required for the writer (configurable)

## Status

**Planned.** Implementation has not started.

## Dependencies

- C99 standard library only (for the core)
- Optional: expat or similar for XML header parsing on embedded targets

## Notes

The C implementation serves as the reference for porting to other low-level languages. It is the lowest-level target and should prioritize correctness and portability over convenience.
