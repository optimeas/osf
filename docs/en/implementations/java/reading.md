---
title: Reading
description: Reading OSF and OSFZ files in Java — DataManager, DataChannel, the block stream, ReaderStats and transparent decompression
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - DataManager
  - DataChannel
  - OSFZ
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Reading

The Java implementation reads **OSF4**, **OSF5** and, transparently,
**OSFZ** (gzip/zlib) through one API. It also reads the `crc` integrity
profile, verifying every checksum along the way
([Error handling](./error-handling.md)). There is exactly one public
entry point:

- **`com.optimeas.osf.DataManager`** — the default and only public read
  path. It loads the whole file, assembles typed channels from the block
  stream, and resolves all block boundaries. For analysis, export and
  tooling.

The actual block-stream decoder lives underneath in the non-exported
package `com.optimeas.osf.internal` — it is an implementation detail of
`DataManager`, not a public surface (see
[The block-stream level](#the-block-stream-level)).

## Quick start

```java
import com.optimeas.osf.DataManager;
import com.optimeas.osf.DataChannel;
import java.nio.file.Path;

DataManager mgr = DataManager.loadFromFile(Path.of("measurement.osf")); // also .osfz

// List every channel (metablock order)
for (DataChannel ch : mgr.channels()) {
    System.out.printf("%-30s %-12s %d samples%n",
            ch.name(), ch.dataType(), ch.sampleCount());
}

// Address a channel by its name (the primary access form)
mgr.channelByName("Sensor.Temperature").ifPresent(ch -> {
    double[] values     = ch.asDoubles();     // values widened to double
    long[]   timestamps = ch.timestampsNs();  // parallel timestamps (ns)
    // …
});
```

## `DataManager`

### Loading

| Method | Source | Notes |
|---|---|---|
| `DataManager.loadFromFile(Path)` | file | OSF, OSFZ; opens and closes the stream itself |
| `DataManager.load(InputStream)` | any `InputStream` | consumed in full; must be positioned at the start of the file |

Both paths run the same pipeline: OSFZ detection → magic header →
(the metablock checksum, under `crc`) → metablock parser (JSON for
OSF5, XML/StAX for OSF4) → block-stream decoder to EOF → channel
assembly. The result is immutable and may be read from any number of
threads concurrently.

### Access

```java
mgr.version();                 // OsfVersion — OSF4 or OSF5, from the magic header
mgr.metadata();                // Map<String,String> — file metadata from the "file" block
mgr.stats();                   // ReaderStats — telemetry of the load
mgr.channels();                // List<DataChannel> — metablock order
mgr.channelByName("a.b.c");    // Optional<DataChannel> — empty when unknown
mgr.channelByIndex(7);         // Optional<DataChannel> — index from the metablock
```

`channelByName` is the primary access form; `channelByIndex` is
convenience. Both return an empty `Optional` rather than an error,
because "channel not present" is a normal case when exploring foreign
files. On a duplicate name the **first** definition wins.

The `metadata()` keys match the wire field names of the `file` block
exactly (for example `creator`, `created_utc`, `tag`, `reason`).

### What can happen on load

- **Truncated file:** no exception. Every fully readable block ends up
  in the channels, `mgr.stats().truncationSeen() == true`.
- **Unknown (future) data type:** the channel is **dropped** from
  `channels()` (its blocks were skipped at the reader level). An unknown
  *channel type* (the data shape) keeps the channel instead — readability
  hinges solely on the data type and the block types, never on
  `channeltype`.
- **Metablock checksum mismatch** (under `crc` only): the load aborts
  **fail-closed** with `OsfException.MetablockCrcMismatch` — a tampered
  metablock is never parsed.
- **Structural errors:** a broken header or metablock, a metablock length
  exceeding `Integer.MAX_VALUE`, or an I/O error abort the load with
  `OsfException.MalformedFile` — see
  [Error handling](./error-handling.md).

## `DataChannel` — the typed channels

`DataChannel` is **one** class; its storage layout is distinguished by
`kind()` over the enum `DataChannel.Kind`:

```java
enum Kind { EQUIDISTANT, TIMESTAMPED, VARIABLE }
```

The on-disk block boundaries are resolved; the samples appear as one
flat run with parallel absolute timestamps.

### Common metadata

```java
ch.index();          // int    — on-disk channel index from the metablock
ch.name();           // String — fully-qualified name
ch.dataType();       // DataType — resolved data type of the samples
ch.channelType();    // ChannelType — data shape (scalar/vector/matrix/binary)
ch.physicalUnit();   // String — physical unit, or null
ch.kind();           // DataChannel.Kind — storage layout
ch.sampleCount();    // long   — number of samples (summed across segments)
ch.timestampsNs();   // long[] — absolute timestamps, parallel to the values
ch.segments();       // List<DataChannel.Segment> — populated for EQUIDISTANT only
```

`timestampsNs()` returns the channel's own backing array — do not
mutate it.

### `EQUIDISTANT` — segments instead of per-sample timestamps

Equidistant channels store **no timestamp per sample**. Instead they
carry a flat value run plus a list of segments. Every `bcStartData`
block in the file opens a segment; every following `bcContinuedData`
block extends the most recent one:

```java
public record Segment(long startTimestampNs, double sampleRateHz,
                      int startIndex, int sampleCount) {}
```

`timestampsNs()` **reconstructs** the timestamps: sample `i` of a
segment lands at `startTimestampNs + (long)(i * 1e9 / sampleRateHz)`
(truncated toward zero, saturating add). Gaps **between** segments are
not interpolated — each segment starts at its own `startTimestampNs`, so
a recording pause stays a pause.

```java
DataChannel ch = mgr.channelByName("Acceleration.X").orElseThrow();
for (DataChannel.Segment seg : ch.segments()) {
    // seg.startTimestampNs(), seg.sampleRateHz(), seg.startIndex(), seg.sampleCount()
}
double[] values = ch.asDoubles();      // flat run across all segments
long[]   times  = ch.timestampsNs();   // the matching, reconstructed timestamps
```

### `TIMESTAMPED` — parallel runs

Numeric or GPS channels with explicit timestamps: `timestampsNs()` and
the value run run in parallel. `bcAbsTimeStampData` blocks land here
directly; OSF4 `bcContinuedRelStampData` deltas are folded to absolute
timestamps on load (anchor = the last absolute timestamp observed on the
channel).

### `VARIABLE` — string and binary

String and binary channels: always carried by `bcAbsTimeStampData` with
one timestamp per sample.

```java
String[] texts = ch.asStrings();     // string channel
byte[][] blobs = ch.asBinaries();    // binary channel
```

Null-terminator handling is version-deterministic: for **OSF4** the
reader has already stripped the trailing byte, for **OSF5** the payload
arrives verbatim.

### Typed accessors

Each accessor projects the stored values into a fresh array and throws
`OsfException.UnsupportedType` when the `dataType()` does not match:

| Accessor | Returns | valid for |
|---|---|---|
| `asDoubles()` | `double[]` | every numeric type (bool→0/1, all integer widths, float, double) |
| `asLongs()` | `long[]` | integer types `int8`…`int64`, `uint8`…`uint64`, `bool` (unsigned zero-extended) |
| `asBooleans()` | `boolean[]` | `bool` only |
| `asStrings()` | `String[]` | `string` only |
| `asBinaries()` | `byte[][]` | `binary` only |
| `asGps()` | `GpsLocation[]` | `gpslocation` only |

For `uint64`, `asLongs()` yields the raw bits — use
`Long.toUnsignedString(...)` for display. `GpsLocation` is a record of
`latitude`, `longitude` (degrees) and `altitude` (metres).

### Data types

`DataType` covers `bool`, `int8`…`int64`, `uint8`…`uint64`, `float`,
`double`, `string`, `binary` and `gpslocation`; `bytearray` is accepted
on read as an alias for `binary`. An unknown but not-removed data type
resolves to `DataType.UNSUPPORTED` (the file loads, the channel is
dropped); the types **removed** from the OSF standard — `pair`,
`triple`, `candata` and `gpsdata` — deliberately raise
`OsfException.UnsupportedType` when resolved.

## The block-stream level

Underneath `DataManager`, an internal block-stream reader
(`com.optimeas.osf.internal.BlockReader`) decodes the binary blocks that
follow the metablock. That package is **not** exported from the JPMS
module — so there is no public streaming API in this version;
application-side, `DataManager` is the sole entry. Its behaviour is
still worth knowing, because it explains the telemetry in `ReaderStats`:

- **Best-effort truncation:** a short or garbled trailing block stops
  the read silently — everything decoded before it is kept, and
  `stats().truncationSeen()` is set. It never throws on truncation.
- **Skipped blocks stay visible:** deprecated or reserved control bytes,
  and blocks of `UNSUPPORTED`-typed channels, are discarded by length
  without parsing and counted in `ReaderStats`.
- **OSF4 trailer:** the optional `0xFFFF` info block plus its 40-byte
  trailer is consumed silently.
- **Integrity:** under an active `crc` profile every block carries a
  trailing CRC32C over the whole frame; it is verified before the typed
  parse (fail-closed). A failure skips the block and increments
  `blocksCrcFailed()`. Signature blocks on the reserved channel
  `0xFFFE` are skipped and counted so a signed file stays readable.

The block-stream reader does **not** decompress itself — OSFZ input is
inflated beforehand (see below).

## Transparent OSFZ

`loadFromFile("x.osfz")` works with no extra effort: before the magic
header, the read chain inspects the leading two bytes and, when needed,
slots a decompressor in front. It detects gzip (`1F 8B`) and zlib (`78`
followed by `01`/`5E`/`9C`/`DA`); real OSF starts with `O` = `0x4F`, so
it never collides. Decompression uses the JDK's built-in `java.util.zip`
(`GZIPInputStream` / `InflaterInputStream`) and is streaming. The finding
is recorded in `stats().compressed()` and `stats().compressionFormat()`
(`"gzip"` or `"zlib"`); for uncompressed files the label stays `"none"`.

## `ReaderStats` — telemetry

After every load, via `mgr.stats()`:

| Field | Meaning |
|---|---|
| `blocksRead()` | number of fully decoded blocks |
| `truncationSeen()` | the stream ended on a partial/garbled block |
| `compressed()` / `compressionFormat()` | OSFZ detection (`"none"` / `"gzip"` / `"zlib"`) |
| `integrity()` | integrity profile declared by the header (`NONE` / `CRC32C` / `ED25519`) |
| `blocksCrcFailed()` | data blocks whose frame CRC32C did not verify (skipped) |
| `blocksSignatureSkipped()` | skipped signature blocks (reserved channel `0xFFFE`) |
| `verificationStatus()` | summarising verification status (see below) |

`verificationStatus()` condenses the integrity finding into one string:

- `"none"` — no integrity profile;
- `"crc_valid"` — level `crc`, every block CRC verified;
- `"invalid"` — level `crc`, at least one block failed its CRC;
- `"signature_unverifiable"` — a signed file whose signatures this
  `crc`-level reader cannot verify.

```java
ReaderStats s = mgr.stats();
System.out.printf("%d blocks, integrity=%s, compressed=%s (%s)%n",
        s.blocksRead(), s.verificationStatus(),
        s.compressed(), s.compressionFormat());
```

## Performance notes

- `DataManager` holds all samples in memory, in primitive arrays
  (`double[]`, `long[]`, …) with no boxing. As a rule of thumb a file
  needs about its decompressed size in RAM. There is no public streaming
  API — so very large corpora are best processed file by file, or
  filtered before loading.
- The typed accessors (`asDoubles()`, `asLongs()`, …) **copy** into a
  new array on each call. In loops, hold the result once in a local
  variable rather than calling the accessor repeatedly.
- Real field files in the single-digit MB range load in milliseconds.
  Transparent OSFZ decompression is streaming and needs no second buffer
  for the whole file.

Continue with [Writing](./writing.md),
[Error handling](./error-handling.md), the [Tools](./tools.md) or the
[Architecture](./architecture.md); the binary format itself is described
by the [OSF specification](../../osf_general.md).

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
