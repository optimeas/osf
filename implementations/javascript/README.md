# OSF — JavaScript Implementation

![Status](https://img.shields.io/badge/status-planned-lightgrey.svg)

## Target Platform

Browser-based web dashboards, real-time visualization applications, and Node.js server-side data pipelines.

## What This Implementation Will Provide

- **Reader**: OSF4 and OSF5 reader for Node.js and browser (via WebAssembly or pure JS)
- **Writer**: OSF5 writer for Node.js
- TypeScript type definitions included
- Published as an npm package

## Status

**Planned.** Implementation has not started.

## Dependencies

- Node.js 18+ for the Node.js target
- No mandatory runtime dependencies in the browser bundle

## Notes

The JavaScript implementation enables web-based tools to consume OSF files directly, including streaming reads over HTTP range requests.
