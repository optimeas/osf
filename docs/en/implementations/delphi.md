---
title: Delphi implementation
description: The Delphi reference implementation of the Open Streaming Format — library, demos and the osftool command line
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Delphi
  - osftool
  - RAD Studio
  - Reference implementation
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
---

🇩🇪 [German version](../../de/implementations/delphi.md)

# Delphi implementation

The Delphi implementation is the **reference implementation** of the Open
Streaming Format: a complete read and write path for OSF4 and OSF5, a set of
demos, and the `osftool` command line. It also produces the set of
[reference files](../examples/osf_file_examples.md) the other
implementations are checked against. Built with RAD Studio 23.0 (Delphi 12).

## Library

The library units live under `implementations/delphi/src/`. The most
important public building blocks:

| Unit | Contents |
|---|---|
| `OSF.Types` | Data and channel types (`TOSFDataType`, `TOSFVersion`, `TOSFGpsLocation`) and helper functions |
| `OSF.Filer` | `TOSFFile` — streaming reader/writer for OSF4 and OSF5; transparent OSFZ decompression; OSF4 XML parsing via OmniXML (no MSXML dependency) |
| `OSF.Data.Manager` | `TOSFDataManager` — high-level read, populates typed channels; access by name |
| `OSF.Data.Channels` | typed in-memory channels incl. equidistant segments (each `bcStartData` opens a segment) |
| `OSF.Log` | process-wide logging/progress dispatcher (`Logger: TOSFLog`) with a listener pattern (DECISIONS §22) |
| `OSF.Export.CSV` / `OSF.Export.CSV.Unified` | CSV export — per channel (XY) and shared timeline respectively |
| `OSF.Export.HDF5` | HDF5 export (Windows; one compressed compound dataset per channel) |
| `OSF.Merger` | `TOSFMerger` — merge several OSF files over a time interval into one output file |
| `OSF.Meta.Cache` | sidecar `.json` cache per OSF file (time ranges, sample counts) for fast metadata |

A no-form program `OSFCompileCheck.dpr` in the implementation root covers
every library unit via `uses` and gives a clean compile signal after
refactorings.

### Logging architecture

Every library unit writes log and progress events to the global
`Logger: TOSFLog`. Applications register a `TLoggerListener` (with its own
`MinLevel` and event callbacks); the singleton handles the fan-out. Details
in [DECISIONS §22](https://github.com/optimeas/osf/blob/main/DECISIONS.md).

## Demos

Under `implementations/delphi/demos/`:

| Demo | Purpose |
|---|---|
| `osfviewer/` | load an OSF file, list channels, render one channel as a TeeChart (IDE only) |
| `osfgenerator/` | writes the 17-file reference set; the console variant `OSFGeneratorCLI` produces it non-interactively |
| `osfcsvexport/` | pipeline demo `TOSFFile` → `TOSFDataManager` → CSV export |
| `osfmerger/` | VCL front-end for `TOSFMerger` (Win64) |

## Command line: `osftool`

`osftool` is the verb-based command-line tool of the Delphi implementation
(Win64) with nine verbs — `merge`, `export`, `info`, `channels`, `stat`,
`cache`, `config`, `convert`, `verify`.

The full description is in its own chapter
**[osftool](../tools/osftool.md)**.

## Building

The library and most demos are compiled with `dcc32` (Win32) or `dcc64`
(Win64) from the respective project directory — `osfmerger` and `osftool`
as Win64. Example (compile check):

```powershell
cd implementations\delphi
dcc32 -B -Q OSFCompileCheck.dpr
```

The HDF5 export needs `hdf5.dll` at runtime; the binding (`src/hdf5/`) is a
standalone, OSF-independent Delphi binding to the HDF5 C library.

## Source code and further reading

- Source code on GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  directory `implementations/delphi/`
- Command line: chapter [osftool](../tools/osftool.md)
- Format specification: chapter [OSF format](../osf_general.md)
