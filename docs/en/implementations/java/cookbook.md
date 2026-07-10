---
title: Cookbook
description: Recipes for typical tasks with the OSF Java library — from file inspection and CSV export to a crash-safe embedded recording loop
sidebar_position: 7
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - Examples
  - Cookbook
  - Recipes
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Cookbook

Compact, copy-paste recipes for the Java library. All of them assume the
JPMS module `com.optimeas.osf` on the module path (Java 21) and import the
public types from the `com.optimeas.osf` package:

```java
import com.optimeas.osf.*;
import java.nio.file.Path;
```

Error handling is trimmed to the minimum. The read and write APIs report
failures via the unchecked `OsfException` (see
[Error handling](./error-handling.md)); for the class-level overview see
[Architecture](./architecture.md), [Reading](./reading.md) and
[Writing](./writing.md).

## Inspect a file (metadata + channel list)

`loadFromFile` detects OSF4, OSF5 and compressed **OSFZ** (gzip/zlib)
transparently from the leading bytes — the same call works for `.osf` and
`.osfz`.

```java
DataManager mgr = DataManager.loadFromFile(Path.of(path));   // .osf or .osfz

System.out.println("version:  " + mgr.version());            // OSF4 / OSF5
System.out.println("creator:  " + mgr.metadata().getOrDefault("creator", "-"));
System.out.println("created:  " + mgr.metadata().getOrDefault("created_utc", "-"));
System.out.println("channels: " + mgr.channels().size());

for (DataChannel ch : mgr.channels()) {
    System.out.printf("  [%d] %s  (%d samples, unit: %s)%n",
            ch.index(), ch.name(), ch.sampleCount(),
            ch.physicalUnit() == null ? "-" : ch.physicalUnit());
}
```

## Get one channel as `double` values with timestamps

`asDoubles()` widens every numeric data type to `double`; `timestampsNs()`
returns the parallel timestamp array — reconstructed from start time and
sample rate for equidistant channels, explicit for timestamped channels.
Both arrays have the same length.

```java
DataChannel ch = mgr.channelByName("Motor.Speed").orElseThrow();

long[]   ts = ch.timestampsNs();
double[] v  = ch.asDoubles();          // throws OsfException.UnsupportedType if not numeric

for (int i = 0; i < v.length; i++) {
    long   tNs   = ts[i];
    double value = v[i];
    // … process tNs, value
}
```

`channelByName` returns an `Optional<DataChannel>`; alternatively
`channelByIndex(int)` looks up by the metablock index.

## Iterate generically over mixed data types

A `switch` over `dataType()` makes export tooling datatype-agnostic. Each
`as…()` accessor throws `OsfException.UnsupportedType` when it does not match
the channel — the `switch` prevents that.

```java
long[] ts = ch.timestampsNs();
switch (ch.dataType()) {
    case DOUBLE, FLOAT, INT8, INT16, INT32, INT64,
         UINT8, UINT16, UINT32, UINT64, BOOL -> {
        double[] v = ch.asDoubles();
        for (int i = 0; i < v.length; i++) row(ts[i], v[i]);
    }
    case GPS_LOCATION -> {
        GpsLocation[] g = ch.asGps();
        for (int i = 0; i < g.length; i++) row(ts[i], g[i].latitude(), g[i].longitude());
    }
    case STRING -> {
        String[] s = ch.asStrings();
        for (int i = 0; i < s.length; i++) row(ts[i], s[i]);
    }
    case BINARY -> {
        byte[][] b = ch.asBinaries();
        for (int i = 0; i < b.length; i++) row(ts[i], b[i].length + " bytes");
    }
    default -> { /* UNSUPPORTED: channel loaded, but values not projectable */ }
}
```

## Minimal CSV export

```java
import java.io.BufferedWriter;
import java.nio.file.Files;

DataChannel ch = mgr.channelByName(name).orElseThrow();
long[]   ts = ch.timestampsNs();
double[] v  = ch.asDoubles();

try (BufferedWriter w = Files.newBufferedWriter(Path.of("channel.csv"))) {
    w.write("timestamp_ns,value\n");
    for (int i = 0; i < v.length; i++) {
        w.write(ts[i] + "," + v[i] + "\n");
    }
}
```

A wide table (one column per channel, one row per timestamp) is produced by
the `osf-cli` tool with `osf convert --to csv` — see [Tools](./tools.md).

## Convert OSF4 → OSF5 (also OSFZ input)

`BlockWriter.fromManager` reconstructs a writer from a loaded file — its
metadata, channel definitions and every sample. The output is **always
OSF5**, regardless of the source format; samples stay bit-exact.

```java
DataManager mgr = DataManager.loadFromFile(Path.of("old_osf4.osf")); // or .osfz
BlockWriter.fromManager(mgr).writeToFile(Path.of("new_osf5.osf"));
```

For **OSFZ output** write the `BlockWriter` into a `GZIPOutputStream`:

```java
import java.util.zip.GZIPOutputStream;

try (var os = new GZIPOutputStream(Files.newOutputStream(Path.of("new.osfz")))) {
    BlockWriter.fromManager(mgr).writeTo(os);
}
```

## Write a new file with analysis data

The `BlockWriter` accumulates every sample in memory and emits the file in
one pass. Channels are declared first (the return value is the channel
index), then filled.

```java
BlockWriter w = new BlockWriter();
w.setMetadata("creator", "my-tool/1.0");

int fftPeak = w.addTimestampedChannel("result.fft_peak", DataType.DOUBLE);

for (var e : results) {
    w.writeSample(fftPeak, e.timestampNs(), e.peakHz());
}

w.writeToFile(Path.of("result.osf"));   // created_utc is injected automatically
```

For equidistant analysis series (fixed rate, `float`/`double` only) instead
of timestamped:

```java
int spectrum = w.addEquidistantChannel("result.psd", DataType.DOUBLE, 2, 1000.0); // 1 kHz
w.startEquidistantSegment(spectrum, startNs, block1);   // double[]
w.appendEquidistantSamples(spectrum, block2);           // extends the same segment
```

## Crash-safe embedded recording loop

The `StreamingWriter` writes the preamble and each completed block
immediately and calls `force(true)` (fsync) after every block. Durability is
therefore **per block**: after a power loss the `DataManager` reads the file
up to the last whole block and sets `stats().truncationSeen()` for the
trailing partial bytes. It implements `Closeable` — try-with-resources emits
any remaining blocks, forces and closes.

```java
try (StreamingWriter w = StreamingWriter.create(Path.of("/data/rec_0001.osf"))) {
    w.setMetadata("creator", "logger-fw/3.2");

    int temp = w.addTimestampedChannel("temp", DataType.DOUBLE, 2, "degC", null);
    int door = w.addTimestampedChannel("door", DataType.BOOL, 2);   // event channel
    w.begin();   // pin the preamble early (otherwise lazy on the first sample)

    while (running) {
        long now = nowNs();

        if (newSample)   w.writeSample(temp, now, value);
        if (doorChanged) w.writeSample(door, now, open);

        // Optional: w.flush() forces the still-open partial blocks now.
        waitForNextTick();
    }
}   // close(): emit remaining blocks, force, close
```

The `StreamingWriter` fixes `sizeoflengthvalue` per channel and cannot bump
it later — so declare channels with large variable samples (see below) with
`4` up front.

## Images/blobs as a binary channel

Large variable samples (JPEGs, raw buffers) easily exceed the 2-byte length
field, so declare the channel with `sizeoflengthvalue = 4`. An attributes map
lets you write a `mimetype` into the metablock. Variable samples are never
batched — one block per sample.

```java
// Writing (StreamingWriter — sov=4 because of the sample size):
int camera = w.addTimestampedChannel(
        "camera.snapshots", DataType.BINARY, 4,
        null, java.util.Map.of("mimetype", "image/jpeg"));

w.writeSample(camera, tsNs, jpegBytes);   // byte[]
```

```java
// Reading:
DataChannel ch = mgr.channelByName("camera.snapshots").orElseThrow();
long[]   ts    = ch.timestampsNs();
byte[][] blobs = ch.asBinaries();

for (int i = 0; i < blobs.length; i++) {
    long   t    = ts[i];
    byte[] jpeg = blobs[i];
    // … store/decode jpeg
}
```

The `BlockWriter` knows the maximum sample size up front: there the short
form `addTimestampedChannel(name, DataType.BINARY)` is enough — it auto-bumps
`sizeoflengthvalue` from 2 to 4 when needed.

## Write and verify the `crc` integrity profile

Both writers can emit level `crc`: a CRC32C over the metablock in the magic
header plus a frame CRC32C per block.

```java
BlockWriter w = new BlockWriter();
w.setIntegrity(IntegrityProfile.CRC32C);
// … channels + samples …
w.writeToFile(Path.of("secured.osf"));
```

On read the `DataManager` verifies fail-closed: a wrong metablock CRC throws
`OsfException.MetablockCrcMismatch`, corrupt blocks are skipped and counted.

```java
DataManager mgr = DataManager.loadFromFile(Path.of("secured.osf"));
ReaderStats s = mgr.stats();

System.out.println(s.verificationStatus());   // "crc_valid" or "invalid"
if (s.blocksCrcFailed() > 0) {
    System.out.println("WARNING: " + s.blocksCrcFailed() + " blocks failed their CRC");
}
```

## Print the reader statistics

```java
ReaderStats s = mgr.stats();
System.out.printf("blocks read: %d%n", s.blocksRead());
if (s.truncationSeen()) {
    System.out.println("WARNING: the file was truncated");
}
if (s.compressed()) {
    System.out.println("source was OSFZ (" + s.compressionFormat() + ")");
}
```

Internals (block stream, assembler, chunking) are covered under
[Internals](./internals.md); build and Maven coordinates under
[Building](./building.md). The overview of all tools is on the
[Java landing page](../java.md).

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.en). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
