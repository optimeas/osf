---
title: Tools — CLI and viewer
description: The bundled Java tools osf-cli (picocli command line with info/channels/dump/convert) and osf-viewer (JavaFX multi-channel plotter with min/max-per-pixel decimation)
sidebar_position: 5
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - osf-cli
  - osf-viewer
  - picocli
  - JavaFX
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Tools — CLI and viewer

Besides the library, the Java reactor ships two standalone applications:
**`osf-cli`** — a scriptable command line for inspecting, exporting and
converting OSF files — and **`osf-viewer`** — a JavaFX viewer that plots
many channels at once. Both build only on the public library API (see
[Reading](./reading.md) and [Writing](./writing.md)) and double as
complete usage examples for it.

Baseline: **Java 21**, Maven reactor. A full build (`mvn -f
implementations/java/pom.xml package`) produces both tools in one pass;
reactor details are on the [Building](./building.md) page.

## osf-cli

`osf-cli` is a [picocli](https://picocli.info/) application with four
subcommands. The entry class
[`OsfCli`](https://github.com/optimeas/osf/blob/main/implementations/java/osf-cli/src/main/java/com/optimeas/osf/cli/OsfCli.java)
registers `info`, `channels`, `dump` and `convert`; every command carries
`-h`/`--help` and `-V`/`--version` (via `mixinStandardHelpOptions`).

### Build as a runnable jar

The `osf-cli` module is bundled into a **self-contained runnable jar**
via the `maven-shade-plugin` (main class
`com.optimeas.osf.cli.OsfCli`, `finalName` `osf-cli`):

```bash
mvn -f implementations/java/pom.xml -pl osf-cli -am package
java -jar implementations/java/osf-cli/target/osf-cli.jar --help
```

The shade jar bundles the OSF library and picocli, so it runs with no
extra classpath. For brevity, `osf` below stands for
`java -jar …/osf-cli.jar`.

### `info` — metadata and channel summary

```bash
osf info measurement.osf
```

Loads the file (OSF4, OSF5, or transparent OSFZ) and prints the format,
file metadata, compression status, and one line per channel:

```text
format: OSF5
creator: optiMEAS
created_utc: 2026-01-15T09:30:00Z
compressed: false
channels: 3
  [0] temperature  type=double  mode=equidistant  samples=10000  unit=°C
  [1] status       type=string  mode=variable     samples=42     unit=
  [2] position     type=gps_location  mode=timestamped  samples=500  unit=
```

The `format:` line is `OSF4` or `OSF5`; the metadata is taken verbatim
from the metablock (`creator`, `created_utc`, `location`, …); a
compressed input prints `compressed: true (gzip)`.

### `channels` — channel table

```bash
osf channels measurement.osf --sort NAME
```

Prints an aligned table (columns *index, name, datatype, mode, samples,
unit*). `--sort` accepts `INDEX` (default) or `NAME`:

```text
index   name                                      datatype      mode          samples   unit
------------------------------------------------------------------------------------------
0       temperature                               double        equidistant   10000     °C
2       position                                  gps_location  timestamped   500
1       status                                    string        variable      42
```

### `dump` — channel data to CSV

`dump` writes channel values as CSV — by default **all chartable
channels** (numeric + `bool`; `string`/`binary`/`gps` are skipped).

| Option | Effect |
|---|---|
| `--channel <name\|index>` | Select a channel by name **or** integer index; repeatable. Omitted: all chartable channels |
| `--format <csv\|unified-csv>` | `csv` (default): one block per channel; `unified-csv`: one wide table |
| `--timestamp-format <…>` | `DATETIME` (default), `SECONDS`, `ISO8601`, `NANOSECONDS` |
| `--out <file>` | Write to a file instead of stdout |

Timestamp formats: `DATETIME` = `uuuu-MM-dd HH:mm:ss.SSS` (UTC, ms),
`SECONDS` = decimal seconds with 9 fractional digits, `ISO8601` =
`uuuu-MM-dd'T'HH:mm:ss'Z'`, `NANOSECONDS` = raw nanosecond integer.
Integral `double` values render without a decimal point (`1` not `1.0`).

**Per-channel CSV** (default) — each block with a `# channel:` header and
`timestamp,value` rows:

```bash
osf dump measurement.osf --channel temperature --timestamp-format SECONDS
```
```text
# channel: temperature
timestamp,value
0.000000000,21.5
0.001000000,21.6
```

**Unified CSV** — one wide table with a row per distinct timestamp; cells
are blank where a channel has no sample at that time:

```bash
osf dump measurement.osf --format unified-csv --channel 0 --channel 3 --out values.csv
```
```text
timestamp,temperature,humidity
1970-01-01 00:00:00.000,21.5,48
1970-01-01 00:00:00.001,21.6,
```

Channel names containing a comma, quote, or newline are CSV-quoted.

### `convert` — to OSF5 (optionally compressed)

`convert` reads any input (OSF4/OSF5, possibly compressed) and writes it
as **OSF5**:

```bash
osf convert old-osf4.osf new.osf                     # OSF4 → OSF5
osf convert measurement.osf measurement.osfz --compress  # OSF5 → gzip (OSFZ)
```

| Option | Effect |
|---|---|
| `--compress` | Wrap the output in gzip (produces an OSFZ file) |
| `--writer <BLOCK\|STREAMING>` | Writer back-end; `BLOCK` (default) buffers in memory and writes in one pass, `STREAMING` replays sample-by-sample through a `FileChannel` |

`STREAMING` does not support `--compress`; combining the two falls back
to `BLOCK` with a note. On success `convert` prints
`wrote <file> (N channels)`. This makes the command the simplest
OSF4→OSF5 converter.

Full source:
[`osf-cli/src/main/java/com/optimeas/osf/cli/`](https://github.com/optimeas/osf/tree/main/implementations/java/osf-cli/src/main/java/com/optimeas/osf/cli).

## osf-viewer

`osf-viewer` is a **JavaFX** application that displays many channels of an
OSF file at once. Its core idea is a **min/max-per-pixel decimation**: no
matter how many millions of samples land in one screen column, an outlier
is never dropped.

### Running it

The viewer runs most easily through the JavaFX Maven plugin (main class
`com.optimeas.osf.viewer.ViewerApp`):

```bash
mvn -pl osf-viewer -f implementations/java/pom.xml javafx:run
```

Optionally, open a file immediately at startup — the first launch argument
is interpreted as a path:

```bash
mvn -pl osf-viewer -f implementations/java/pom.xml javafx:run \
    -Djavafx.args="measurement.osf"
```

A 1000×700 window titled "OSF Viewer" opens, with a toolbar, channel
list, plot area, and status bar.

### User interface

- **Toolbar** — *Open…* opens a file dialog (filters `*.osf`, `*.osfz`);
  *Zoom Reset* returns the visible range to the full time extent of all
  chartable channels.
- **Channel list** (left) — a table with columns *Plot, Name, DataType,
  Mode, Samples, Unit*. The Plot checkbox toggles a channel into the
  drawing; for non-chartable channels (string, binary, GPS) it is disabled
  and carries the tooltip "not plotted in v1".
- **Plot area** (center) — the actual trace rendering; it fills the
  remaining space and repaints on every resize.
- **Status bar** (bottom) — load status and cursor readout.

Loading always happens **in the background** (a JavaFX `Task`) so the UI
stays responsive while large files are read.

### Decimation — min/max per pixel

For each pixel column the
[`Decimator`](https://github.com/optimeas/osf/blob/main/implementations/java/osf-viewer/src/main/java/com/optimeas/osf/viewer/Decimator.java)
determines the **minimum AND maximum** of all samples falling into that
time window and draws a vertical stroke from `minY` to `maxY`. Even under
extreme compression every peak stays visible — unlike simply dropping
intermediate points. Sample assignment uses a binary search
(`lowerBound`) over the ascending timestamps, so it is fast even for
millions of samples. Each selected channel autoscales its Y axis
independently (over the cached value range); colours rotate through a
palette of six easily distinguishable tones.

### Interaction

- **Pan** — drag horizontally with the mouse button held to shift the
  time window.
- **Zoom** — mouse wheel; scrolling up zooms in (factor 0.8), down zooms
  out (factor 1.25), each **around the cursor position** so the time under
  the cursor stays fixed.
- **Cursor readout** — on mouse move the status bar shows the cursor time
  and, for each selected channel, the nearest sample value.

Coordinate mapping (time↔X, value↔Y with an inverted Y axis) is
encapsulated in `AxisTransform`, the readout and drawing logic in
`PlotCanvas`; both are deliberately kept apart from the pure model layer
(`ViewerModel`, `Decimator`, `AxisTransform` — no JavaFX imports) and are
therefore testable without a running JavaFX runtime. See
[Architecture](./architecture.md) and [Internals](./internals.md) for that
split.

Full source:
[`osf-viewer/src/main/java/com/optimeas/osf/viewer/`](https://github.com/optimeas/osf/tree/main/implementations/java/osf-viewer/src/main/java/com/optimeas/osf/viewer).

## Next

- [Reading](./reading.md) and [Writing](./writing.md) — the library API
  both tools rely on.
- [Error handling](./error-handling.md) — how `OsfException` surfaces in
  the CLI output.
- [Cookbook](./cookbook.md) — short, copy-paste recipes.
- [Back to the Java overview](../java.md).

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.en). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
