---
title: Writing
description: Writing OSF5 with osf-java — StreamingWriter (embedded, power-loss safe), BlockWriter (analyst-friendly), the crc integrity profile and round-trip
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - StreamingWriter
  - BlockWriter
  - crc32c
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Writing

The library writes **OSF5 only** — even when the source was an OSF4 or OSFZ
file. Two writer classes in the `com.optimeas.osf` package cover two very
different deployment profiles:

| | `StreamingWriter` | `BlockWriter` |
|---|---|---|
| Use | embedded recording | analysis, conversion, export |
| Memory | bounded (one block buffer per channel, dropped after emit) | accumulates all samples in RAM |
| Durability | `FileChannel.force(true)` per block — power-loss safe | no `force`; file is produced at the end |
| Sink | file path (`Path`) | `Path` **or** any `OutputStream` (memory, socket) |
| `sizeOfLengthValue` | fixed from channel declaration (metablock is on disk) | auto-bump 2 → 4 for variable channels |
| Lifecycle | `create()` → channels → `begin()` → write → `close()` | `new` → accumulate → `writeToFile()` / `writeTo()` |
| Multiple emission | no (one file per instance, `Closeable`) | yes (`writeTo*` any number of times) |

Both share the same channel types, the same chunking arithmetic and the same
write families (equidistant, timestamped numeric, GPS, string/binary). For the
same channels, samples, `sizeOfLengthValue` and `created_utc` they produce
**byte-identical** OSF5 files.

## Declaring channels — `ChannelDef`

A channel is not constructed directly; it is declared through an
`add…Channel` method on the writer, which builds a `ChannelDef` internally and
returns the **channel index** (sequential from 0) that every write call uses.
The `ChannelDef` (a `record`) describes the channel as it appears in the
metablock:

```java
public record ChannelDef(
    int index,               // channel index (0..65535), referenced by the block stream
    String name,             // fully-qualified channel name (required)
    DataType dataType,       // resolved data type (required)
    ChannelType channelType, // data shape: SCALAR/VECTOR/MATRIX/BINARY
    int sizeOfLengthValue,   // length-prefix width: 2 or 4
    long timeIncrementNs,    // equidistant period in ns; 0 = timestamped
    String physicalUnit,     // physical unit or null
    Map<String,String> attributes) { }  // e.g. displayname, comment, reference
```

Two declaration styles per writer:

```java
// Timestamped channel (each sample carries its own timestamp):
int rpm = writer.addTimestampedChannel("motor.speed", DataType.DOUBLE, 2);
int evt = writer.addTimestampedChannel("event", DataType.STRING, 2,
                                       "1/min", Map.of("displayname", "Event"));

// Equidistant channel (fixed rate; only the segment start carries a timestamp):
int sig = writer.addEquidistantChannel("acceleration", DataType.DOUBLE, 4,
                                       1000.0 /* Hz */);
```

Rejected with `IllegalArgumentException`: an empty name, a `null` or
`UNSUPPORTED` data type, `sizeOfLengthValue` ≠ 2/4, an equidistant channel of
any type other than `FLOAT`/`DOUBLE`, and a non-positive or non-finite sample
rate. On the `StreamingWriter` any `add…Channel` call after `begin()` throws an
`OsfException` (the Configure phase is over by then). The `channeltype` is always
normalised to `scalar` on write — equidistance is carried by the `timeincrement`
alone, not by the `channeltype`.

### Choosing `sizeOfLengthValue`

Each block's length field is 2 or 4 bytes wide and bounds the block size
(~64 KB resp. ~2 GB). Practical rules:

- **String/binary channels with large samples** (images, audio, blobs): on the
  `StreamingWriter` you must declare `4` — it cannot change the value after
  `begin()`. The `BlockWriter` promotes a variable channel it is allowed to size
  itself from 2 to 4 automatically (auto-bump at emit), so `2` is always a fine
  starting point there.
- **Numeric channels** are never promoted — they split into more blocks instead.
  On the `StreamingWriter`, `4` spares a high-rate channel the chunking into many
  small, individually fsync'd blocks.
- Otherwise stay on the default `2` (more compact blocks).

## `StreamingWriter` — embedded, power-loss safe

```java
import com.optimeas.osf.*;

try (StreamingWriter w = StreamingWriter.create(Path.of("recording.osf"))) {
    w.setMetadata("creator", "logger-fw/3.2");   // metadata before begin()
    w.setMetadata("tag", "test-bench-7");

    int rpm = w.addTimestampedChannel("motor.speed", DataType.DOUBLE, 2);
    int gps = w.addTimestampedChannel("vehicle.gps", DataType.GPS_LOCATION, 2);
    w.begin();                                    // header + metablock on disk, fsync

    while (running) {
        w.writeSample(rpm, nowNs(), readRpm());   // per block: encode, write, fsync
    }
}                                                 // close() emits trailing blocks + fsync
```

Guarantees and behaviour:

- **Every `writeSample` that returns is on disk as a whole block**
  (`FileChannel.force(true)` = fsync). After a power loss the file stays readable
  up to the last confirmed block; the best-effort reader recovers every block
  before the cut and flags a partial trailing remainder via
  `ReaderStats.truncationSeen()` (see [Reading](./reading.md)).
- **Preamble lazy or eager:** `begin()` writes the magic-header line
  (`OSF5 <len>\n`) and the JSON metablock once and fsyncs them; if not called
  explicitly it happens automatically on the first sample. Afterwards the
  metablock is never touched again — **metadata setters take effect only before**.
- **`created_utc`** is stamped automatically at `begin()` if not set (ISO-8601
  UTC, e.g. `2026-07-11T08:30:00Z`).
- **Channels are locked to one block family:** writing the same channel once
  timestamped and once equidistant raises an `OsfException`.
- `close()` is idempotent and emits any buffered trailing blocks; as a
  `Closeable`, the writer belongs in a try-with-resources. It is **not
  thread-safe** — serialise access externally.

### Write families

```java
// Timestamped numeric — single- and batch overloads:
w.writeSample(ch, tsNs, 3.14);                        // double
w.writeSample(ch, tsNs, 42L);                         // any integer channel
w.writeSamples(ch, tsArray, valuesArray);             // parallel arrays (bulk)

// GPS (its own overload):
w.writeSample(ch, tsNs, new GpsLocation(lat, lon, alt));

// String / binary (one sample per block; OSF5: no 0x00 terminator):
w.writeSample(ch, tsNs, "Event: door open");
w.writeSample(ch, tsNs, jpegBytes);                   // byte[]

// Equidistant (float/double only; rate comes from addEquidistantChannel):
w.startEquidistantSegment(ch, t0Ns, data);            // opens a segment
w.appendEquidistantSamples(ch, more);                 // extends the open segment
```

`writeSample(int, long, long)` serves any integer channel (`int8…int64`,
`uint8…uint64`); the value is narrowed to the channel's width on encode. Every
batch and every segment call is chunked automatically to the channel's block
capacity — one fsync per emitted block. A new `startEquidistantSegment` closes
the previous one and deliberately opens a **new** segment; gaps between segments
are the spec-conformant way to represent recording pauses.

## `BlockWriter` — accumulate and emit

```java
BlockWriter w = new BlockWriter();
w.setMetadata("creator", "analysis-tool/1.0");

int ch  = w.addTimestampedChannel("measurement", DataType.DOUBLE);   // auto-bump form
int sig = w.addEquidistantChannel("signal", DataType.DOUBLE, 4, 100.0);
w.startEquidistantSegment(sig, t0Ns, samples);
w.writeSample(ch, tsNs, 42.0);

w.writeToFile(Path.of("result.osf"));

var mem = new java.io.ByteArrayOutputStream();   // or any OutputStream
w.writeTo(mem);                                  // emit the same instance again
```

- The `writeSample` / `startEquidistantSegment` family mirrors the streaming
  writer's (same types, same validation), but only accumulates in memory;
  chunking into spec-conformant blocks happens at emit time.
- `writeToFile` / `writeTo` may be called **multiple times** — the same instance
  can be written to a file and a network stream at once.
- **Auto-bump:** a variable channel declared with the two-argument
  `addTimestampedChannel(name, type)` starts at `sizeOfLengthValue = 2` and is
  promoted to `4` for emission if its largest sample would overflow the 2-byte
  length field. The three-argument form with an explicit value is honoured and
  rejects an over-large sample instead (like the StreamingWriter, which cannot
  bump). Numeric channels are never promoted.
- No fsync — durability is the caller's concern.
- `channelCount()` and `channelIndex("name")` help when indices are not carried
  along (`channelIndex` returns `-1` for an unknown name).

## Automatic metadata defaults

Both writers write the entries set via `setMetadata(key, value)` **verbatim**
into the metablock's `osf.file` object. The only automatic intervention:

| Field | Behaviour when not set |
|---|---|
| `created_utc` | **always** stamped (current UTC time, `YYYY-MM-DDTHH:MM:SSZ`) — via `putIfAbsent`, so an already present value is left untouched |
| all others (`creator`, `tag`, …) | written only when set; never as `null` |

There is therefore no forced `creator` or `tag` default — anything you do not set
does not appear in the metablock.

## Integrity profile on write (`crc`)

Both writers can optionally produce the OSF5 integrity profile at level `crc` —
it is off by default (`IntegrityProfile.NONE`):

```java
w.setIntegrity(IntegrityProfile.CRC32C);   // before begin() resp. writeTo
```

When enabled the writer emits

- a `crc32c` token in the magic-header line carrying the **metablock CRC**, and
- per data block an appended **frame CRC32C** (4 bytes, counted in the block's
  length field). On the `StreamingWriter` the frame CRC is durable with the same
  `force(true)` as the block itself.

The checksum is `java.util.zip.CRC32C` (JDK-native). The 4 frame-CRC bytes eat
into every block's payload budget, so slightly fewer samples fit per block — the
chunking arithmetic accounts for that automatically. How the reader verifies
these values fail-closed is covered under [Reading](./reading.md) and
[Error handling](./error-handling.md).

The signing level (`IntegrityProfile.ED25519`) is **not** supported by the writer
and is rejected with an `OsfException` when the preamble is written.

## Round-trip and conversion

Writing a loaded `DataManager` back out — at the same time the OSF4 → OSF5 resp.
OSFZ → OSF5 conversion:

```java
DataManager mgr = DataManager.loadFromFile(Path.of("old.osf"));  // OSF4 / OSFZ too
BlockWriter.fromManager(mgr).writeToFile(Path.of("new.osf"));     // always OSF5
```

`BlockWriter.fromManager(mgr)` builds a writer from the manager's typed channels
and samples; if you want to filter or rename before writing, keep working on the
returned writer.

Preserved: channel names, data types, sample values (bit-exact), segment
boundaries and the file metadata — **including `created_utc`**, because a loaded
value is already set and is not re-stamped. **Not** carried over is the
`sizeOfLengthValue` (the writer starts at 2 and bumps variable channels as
needed); the channel index is reassigned by list position.

## What the writers deliberately do not do

- **No OSF4 output** — OSF5 is the only write format.
- **No OSFZ output** — the library reads gzip-wrapped files transparently but
  does not compress on its own; compression is a downstream step.
- **No relative timestamps** — the relative time format is an OSF4 read-legacy;
  writers emit absolute timestamps.
- **No timestamp validation** — monotonicity is not required by the spec and is
  not enforced.
- **No signing** — the Ed25519 level is rejected.
- **No thread-safety** — serialise access to a writer instance externally.

The framing and chunking details both writers build on are described in the
[Internals](./internals.md) chapter; the overall architecture and the tooling
live under [Architecture](./architecture.md), [Tools](./tools.md) and
[Building](./building.md). Getting-started examples are collected in the
[Cookbook](./cookbook.md); the authoritative format definition is the
[OSF format](../../osf_general.md) chapter, and the overview is the
[Java implementation](../java.md) page.

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.en). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
