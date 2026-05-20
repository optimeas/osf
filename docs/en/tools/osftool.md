---
title: osftool — Command-Line Tool
description: The osftool command-line tool for merging, exporting, inspecting, and converting OSF files
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - osftool
  - CLI
  - command-line
  - merge
  - export
last_update:
  date: 2026-05-20
  author: Optimeas GmbH
---

🇩🇪 [Deutsche Version](../../de/tools/osftool.md)

# osftool — Command-Line Tool

**osftool** is a verb-based command-line tool for working with Open Streaming Format files. It reads and writes OSF4 and OSF5, transparently handles compressed OSFZ files, and bundles the everyday tasks around OSF data into a single executable: merging files over a time interval, exporting channels to CSV, inspecting metadata, computing statistics, converting between format versions, and checking file integrity.

osftool is built on the Delphi OSF library (`implementations/delphi/src/`). The primary build target is Windows 64-bit; the project also carries macOS (Intel and Apple Silicon) and Linux 64-bit build configurations. The current version is **1.1.0**.

## What osftool is for

osftool covers the recurring tasks of an OSF-based workflow from a script or a terminal — no IDE and no code required:

- **Combining field data.** Many OSF/OSFZ files scattered across a directory tree are scanned and merged into a single file for a chosen time interval and channel selection.
- **Getting data into analysis tools.** Channels are exported to CSV — either one XY block per channel, or a single shared timeline with one column per channel.
- **Inspecting files quickly.** Metadata, time range, channel lists, and per-channel statistics are available without opening the file in an application.
- **Migrating between format versions.** OSF4 files are rewritten as OSF5 and vice versa.
- **Checking integrity.** Files are walked block by block and verified for structural consistency.
- **Automation.** Every command has a machine-readable `--json` mode and a uniform set of exit codes, so osftool composes cleanly into scripts and CI pipelines.

## Installation

### Building from source

osftool is part of the OSF repository and is built with Embarcadero Delphi 12 (RAD Studio 23) or newer.

```
cd implementations/delphi/tools/osftool
dcc64 -B -Q OsfTool.dpr
```

This produces `OsfTool.exe` for Windows 64-bit. The `OsfTool.dproj` project file additionally carries `OSX64`, `OSXARM64`, and `Linux64` build configurations for use from the RAD Studio IDE; the source is conditional-compilation clean for those targets.

### Adding osftool to PATH

To call `osftool` from any directory, add its location to the search path:

```
osftool config install-path
```

On Windows this writes the executable's directory to the per-user `PATH` (`HKCU\Environment`) — no administrator rights required. On macOS and Linux the command prints the shell snippet to add to `~/.zshrc` or `~/.bashrc`. The change is undone with `osftool config uninstall-path`.

## General usage

```
osftool <command> [options] [arguments]
osftool <command> --help
```

Running `osftool` with no arguments, or `osftool --help`, prints the global help with the command list. Appending `--help` to any command prints that command's detailed help without doing any work. `osftool --version` (or `-V`) prints the version and build timestamp; add `--short` for just the version number.

### Commands

| Command    | Purpose |
|------------|---------|
| `merge`    | Merge OSF files from a directory over a time interval |
| `export`   | Export OSF channels to CSV or other formats |
| `info`     | Show file metadata and time range |
| `channels` | List all channels in a file |
| `stat`     | Compute statistics (min, max, mean, …) per channel |
| `cache`    | Manage `.json` sidecar cache files |
| `config`   | View and edit default settings |
| `convert`  | Convert between OSF4 and OSF5 |
| `verify`   | Check file integrity and block consistency |

### Global options

These options are understood by every command:

| Option      | Effect |
|-------------|--------|
| `--json`    | Emit a machine-readable JSON result on stdout instead of human-readable text |
| `--quiet`   | Suppress informational prose; warnings and errors are still shown |
| `--verbose` | Print debug-level log messages to stderr |

Results go to **stdout**, log messages and errors to **stderr**, so the two can be redirected independently. In `--json` mode stdout carries only the JSON document.

### Exit codes

| Code | Meaning |
|------|---------|
| `0`  | Success |
| `1`  | Bad arguments |
| `2`  | File or directory not found |
| `3`  | I/O error |
| `4`  | Format error (invalid or corrupt OSF file) |

## The sidecar cache

Several commands can use a `.json` **sidecar file** stored next to each OSF file. The sidecar holds per-channel metadata — sample counts, first and last timestamps, the global time range — that the OSF metablock alone does not carry. With a valid sidecar present, commands such as `info` and `channels` answer instantly instead of scanning every block.

The sidecar is consulted automatically; pass `--no-cache` to ignore it and scan the source file directly. The `cache` command builds, rebuilds, inspects, and removes sidecars in bulk.

## Command reference

### merge

Scans a directory recursively for `.osf` and `.osfz` files, selects those overlapping a time interval, and merges the chosen channels into a single output file.

```
osftool merge <rootdir> <outputfile> [channel ...] [options]
```

| Argument     | Description |
|--------------|-------------|
| `rootdir`    | Root directory; scanned recursively for `.osf` and `.osfz` |
| `outputfile` | Output file path (`.osf`) |
| `channel`    | Optional channel names; omit to merge all channels |

| Option         | Description |
|----------------|-------------|
| `--start <ts>` | Interval start, ISO 8601 (default: `1970-01-01T00:00:00`) |
| `--end <ts>`   | Interval end, ISO 8601 (default: the current date and time) |
| `--osf4`       | Write OSF4 output (default: from config `output.format`) |
| `--overwrite`  | Overwrite overlapping timestamps (default: from config `output.overlap`) |
| `--no-cache`   | Do not read or write `.json` sidecar files |
| `-q`, `--quiet`   | Suppress the live display; print only errors, on stderr |
| `-v`, `--verbose` | Print every log line (the classic scrolling output, no live bar) |
| `--json`          | Emit a machine-readable JSON-Lines event stream on stdout |
| `--log <path>`    | Write a full diagnostic log (every level) to a file |

`--start` and `--end` are optional and independent: each defaults on its own, and a flag, when given, overrides only that bound. With neither flag, `merge` covers the full available range.

```
# Merge everything under ./field-data into one file
osftool merge ./field-data combined.osf

# Merge only two channels within a time window
osftool merge ./field-data window.osf Sensor/Temperature Sensor/Pressure \
  --start 2026-05-05T10:00:00 --end 2026-05-05T12:00:00
```

By default `merge` shows a **live progress display**: a header for each phase, the file currently being read, and a progress bar. Error lines stay pinned permanently above the bar, while the per-channel informational and warning chatter is suppressed.

```
Reading files...
  V:\field-data\20260517\20260517_192802.osfz
  [████████████████████░░░░░░░░░░░░░░░░░░░░] 50% (173/346)
```

The output flags change this presentation and are mutually exclusive: `-v` / `--verbose` prints the full scrolling log instead; `-q` / `--quiet` prints only errors on stderr and is otherwise silent; `--json` emits a JSON-Lines event stream for pipelines. `--log <path>` is orthogonal — it writes the complete diagnostic log to a file in any mode. When stdout is redirected to a pipe or file the live display is automatically replaced by periodic plain progress lines.

### export

Exports the channels of a single OSF/OSFZ file to CSV.

```
osftool export <inputfile> <outputfile> [channel ...] [options]
```

| Argument     | Description |
|--------------|-------------|
| `inputfile`  | Source `.osf` or `.osfz` |
| `outputfile` | Output file path |
| `channel`    | Optional channel names; omit to export all channels |

| Option                     | Description |
|----------------------------|-------------|
| `--format <fmt>`           | `csv` (default) — one XY block per channel; or `unified-csv` — a single shared timeline with one column per channel |
| `--timestamp-format <fmt>` | Timestamp format for `unified-csv`: `datetime` (default), `seconds`, `iso8601`, `nanoseconds` |
| `--start <time>`           | Only export samples from this UTC time (ISO 8601) |
| `--end <time>`             | Only export samples up to this UTC time (ISO 8601) |
| `--decimal-sep <c>`        | Decimal separator: `comma` (default) or `dot` |
| `--encoding <enc>`         | `iso-8859-1` (default) or `utf-8` |
| `--exclude-empty`          | Skip channels with zero samples |

`--start` and `--end` must be supplied together — one without the other is rejected. The defaults for `--decimal-sep` and `--encoding` come from the configuration file.

```
osftool export motorbike.osf motorbike.csv --format unified-csv \
  --timestamp-format iso8601 --decimal-sep dot
```

### info

Shows file metadata and the global time range.

```
osftool info <file> [options]
```

| Option       | Description |
|--------------|-------------|
| `--no-cache` | Do not consult the `.json` sidecar; scan the OSF file directly |
| `--json`     | Output as JSON |

The report includes the format version, creator and creation time, tag, reason, comment, channel count, the first and last data timestamps, and the resulting duration.

```
osftool info motorbike.osf
```

### channels

Lists every channel in the metablock.

```
osftool channels <file> [options]
```

| Option               | Description |
|----------------------|-------------|
| `--filter <pattern>` | Wildcard filter on the channel name, e.g. `GPS.*` |
| `--no-cache`         | Do not consult the `.json` sidecar |
| `--json`             | Output as a JSON array |

Each row shows the channel index, name, data type, physical unit, and — when a valid sidecar is available — the sample count and first/last timestamps. Without the sidecar those columns report `?`.

```
osftool channels motorbike.osf --filter "Sensor/*"
```

### stat

Computes minimum, maximum, mean, and standard deviation per numeric channel, using Welford's single-pass online algorithm.

```
osftool stat <file> [channel ...] [options]
```

| Option           | Description |
|------------------|-------------|
| `--start <time>` | Only consider samples from this UTC time (ISO 8601) |
| `--end <time>`   | Only consider samples up to this UTC time (ISO 8601) |
| `--json`         | Output as JSON |

`--start` and `--end` are each independently optional. String, binary, and gpslocation channels are listed but marked *not numeric*. Int64/UInt64 channels are flagged for possible precision loss when converted to `Double` for the calculation.

```
osftool stat motorbike.osf --start 2026-05-05T10:00:00
```

### cache

Manages the `.json` sidecar cache files for every `.osf`/`.osfz` file under a directory.

```
osftool cache <subcommand> <rootdir> [options]
```

| Subcommand | Description |
|------------|-------------|
| `build`    | Build missing sidecars; skip files whose sidecar is already valid |
| `rebuild`  | Force-rebuild every sidecar |
| `clean`    | Delete all sidecars under `rootdir` |
| `status`   | Report which files have no valid sidecar |

| Option           | Description |
|------------------|-------------|
| `--no-recursive` | Restrict to the root directory only (default: include subdirectories) |
| `--json`         | Output as JSON |

```
osftool cache build ./field-data
osftool cache status ./field-data
```

### config

Inspects and changes the persistent settings used as default values by other commands, and manages the `PATH` entry.

```
osftool config                    Show all current settings
osftool config set <key> <value>  Set a value
osftool config reset              Reset all settings to defaults
osftool config install-path       Add osftool to the user PATH
osftool config uninstall-path     Remove osftool from the user PATH
osftool config --json             Show settings as JSON
```

See [Configuration](#configuration) below for the available keys.

### convert

Converts a single file between OSF4 and OSF5 by round-tripping it through the merger.

```
osftool convert <inputfile> <outputfile> [options]
```

| Option    | Description |
|-----------|-------------|
| `--osf4`  | Write the output as OSF4 |
| `--osf5`  | Write the output as OSF5 |
| `--json`  | Output as JSON |

`--osf4` and `--osf5` are mutually exclusive. When neither is given, the target version comes from the config key `output.format`. Note that the converter always emits absolute-timestamp data blocks regardless of the source layout.

```
osftool convert legacy.osf modern.osf --osf5
```

### verify

Walks a file end to end and checks it for structural integrity.

```
osftool verify <file> [options]
```

The following checks are performed:

1. Magic header readable and version recognised.
2. Metablock parseable (valid XML or JSON).
3. Every block's channel index is present in the metablock.
4. No block declares a length exceeding the file size.
5. Timestamps increase monotonically per channel.
6. The file ends cleanly — the last block is not truncated.

| Option      | Description |
|-------------|-------------|
| `--strict`  | Treat warnings as errors (affects the exit code) |
| `--json`    | Output as JSON |

Integrity-breaking problems are reported as errors and produce exit code `4`; recoverable concerns (such as a truncated final block) are warnings. With `--strict`, warnings also produce exit code `4`.

```
osftool verify motorbike.osf --strict
```

## Configuration

osftool stores persistent settings in a JSON file. Other commands read it for their default values, so the configuration acts as a per-user policy.

| Platform | Location |
|----------|----------|
| Windows  | `%APPDATA%\osftool\config.json` |
| macOS / Linux | `~/.config/osftool/config.json` |

The file is optional — when it is missing, every setting falls back to its built-in default.

| Key                  | Default      | Meaning |
|----------------------|--------------|---------|
| `output.format`      | `osf5`       | Default output format for `merge` and `convert` |
| `output.overlap`     | `skip`       | Overlap strategy: `skip` or `overwrite` |
| `export.decimal_sep` | `,`          | Default CSV decimal separator |
| `export.encoding`    | `iso-8859-1` | Default CSV encoding |
| `cache.enabled`      | `true`       | Use `.json` sidecar files |
| `cache.auto_build`   | `true`       | Auto-build the cache during a scan |

Values are read and written with the `config` command:

```
osftool config                          # show all settings
osftool config set output.format osf4   # change a default
osftool config reset                    # restore the defaults
```

A command-line flag always overrides the configured default for that single invocation.
