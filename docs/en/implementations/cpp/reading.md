---
title: Reading
description: Reading OSF and OSFZ files — DataManager, DataChannel, BlockReader, ReaderStats and transparent decompression
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - DataManager
  - BlockReader
  - OSFZ
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Reading

The C++ implementation reads **OSF4**, **OSF5** and transparently
**OSFZ** (gzip/zlib) through the same API. There are two read levels:

- **`osf::DataManager`** — the standard path. Loads the whole file,
  assembles typed channels from the block stream and resolves all block
  boundaries. For analysis, export, tooling.
- **`osf::BlockReader`** — the stream level. Yields block by block in
  file order with constant memory. For very large files, custom
  aggregations and specialist tools.

## Quick start

```cpp
#include <osf/osf.h>

auto result = osf::DataManager::loadFromFile("measurement.osf");  // also .osfz
if (!result) {
    std::cerr << result.error().message << "\n";
    return 1;
}
osf::DataManager const& mgr = *result;

// Address a channel by name (the primary access form)
if (osf::DataChannel const* ch = mgr.channel("Sensor.Temperature")) {
    if (auto values = osf::asDoublesFlat(std::get<osf::TimestampedChannel>(*ch))) {
        for (auto const& [tsNs, value] : *values) { /* … */ }
    }
}
```

## `DataManager`

### Loading

| Method | Source | Notes |
|---|---|---|
| `DataManager::loadFromFile(path)` | file | OSF, OSFZ; determines the file size for `stats` |
| `DataManager::loadFromStream(istream&)` | any `std::istream` | the stream must be positioned at the start of the file and (for OSFZ detection) **seekable** |

Both paths run the same pipeline: OSFZ detection → magic header →
metablock parser (JSON or XML) → `BlockReader` until EOF → channel
assembly. The result is immutable and may be read by any number of
threads simultaneously.

### Access

```cpp
mgr.meta;                  // osf::MetaBlock  — FileInfo, channel definitions, infos
mgr.stats;                 // osf::ReaderStats — telemetry of the load
mgr.channels();            // std::vector<DataChannel> const& — metablock order
mgr.channel("a.b.c");      // DataChannel const* — nullptr if unknown (primary form)
mgr.channelByIndex(7);     // DataChannel const* — index from the metablock (optional)
```

`channel(name)` is the primary access form; `channelByIndex` is
convenience. Both return `nullptr` instead of an error, because
"channel not present" is a normal case when exploring unfamiliar files.

### What can happen during load

- **Truncated file:** not an error. All fully readable blocks land in
  the channels, `mgr.stats.blocksTruncated == 1`.
- **Unknown (future) data type:** the channel is **omitted** from the
  channel list (its blocks were skipped at the reader level); the
  definition remains visible in `mgr.meta.channels`, including the
  original spelling in `dataTypeRaw`.
- **Structural error:** `InvalidMetablock`, `UnknownChannelIndex`,
  `ChannelMixedBlockTypes`, etc. abort the load with a structured
  error — see [Error handling](error-handling.md).

## `DataChannel` — the typed channels

`DataChannel` is a `std::variant` over three layouts:

```cpp
using DataChannel = std::variant<EquidistantChannel, TimestampedChannel, VariableChannel>;
```

### Common accessors (free functions)

For variant-agnostic code there are free functions that use `std::visit`
internally:

```cpp
osf::channelIndex(ch);          // std::uint16_t
osf::channelName(ch);           // std::string const&
osf::channelDataType(ch);       // osf::DataType
osf::channelPhysicalUnit(ch);   // std::optional<std::string>
osf::channelDisplayName(ch);    // std::optional<std::string>
osf::channelSampleCount(ch);    // std::size_t (sum over all segments)
osf::channelIsEmpty(ch);        // bool
osf::channelMeta(ch);           // ChannelMeta const& (secondary definition fields)
```

### `EquidistantChannel` — segments instead of timestamps

Equidistant channels store **no timestamp per sample**. Instead: a flat
sample vector (`NumericValues`, a variant over all numeric types) plus a
segment list. Each `bcStartData` block of the file opens a segment:

```cpp
struct Segment {
    std::int64_t startTimestampNs;  // absolute start time
    double       sampleRateHz;      // applies until the next segment
    std::size_t  startIndex;        // first sample index in the flat vector
    std::size_t  sampleCount;       // number of samples in this segment
};
```

Sample `i` of a segment is at
`startTimestampNs + i * (1e9 / sampleRateHz)`. Gaps between segments are
**not** interpolated — a recording pause stays a pause.

If you need `(timestamp, value)` pairs, call `samplesVector()`
(materializing; reconstructs the timestamps from the segments):

```cpp
auto const& eq = std::get<osf::EquidistantChannel>(*ch);
for (auto const& s : eq.samplesVector()) {
    // s.timestampNs, s.value (NumericValueRef = variant over the numeric types)
}
```

### `TimestampedChannel` — parallel vectors

```cpp
auto const& ts = std::get<osf::TimestampedChannel>(*ch);
ts.timestampsNs;  // std::vector<std::int64_t>, stream order
ts.values;        // NumericValues, parallel to it
```

`bcAbsTimeStampData` blocks land here directly; OSF4
`bcContinuedRelStampData` deltas are converted to absolute timestamps on
load (the anchor is the channel's last absolute timestamp).

### `VariableChannel` — string and binary

```cpp
auto const& var = std::get<osf::VariableChannel>(*ch);
var.timestampsNs;                       // one timestamp per sample
auto strs = var.asStrings();            // Result<std::vector<std::string> const*>
auto bins = var.asBinaries();           // Result<std::vector<std::vector<uint8_t>> const*>
var.mimeType;                           // e.g. "image/jpeg" on binary channels
```

Exactly one of the string / binary stores is populated (per `dataType`);
the wrong-typed accessor returns `DataTypeMismatch`. The null-terminator
handling is version-deterministic (spec rev 2026-05-24): on **OSF4** the
reader has already stripped the last byte, on **OSF5** the payload
arrives verbatim.

### Flat accessors — typed copies

For each numeric type (plus GPS) there are `as<type>Flat` helpers in two
forms:

```cpp
// EquidistantChannel: values only
Result<std::vector<double>> osf::asDoublesFlat(EquidistantChannel const&);

// TimestampedChannel: (timestamp, value) pairs
Result<std::vector<std::pair<std::int64_t, double>>> osf::asDoublesFlat(TimestampedChannel const&);
```

(analogously `asFloatsFlat`, `asInt32Flat`, …, `asGpsFlat`). They
**copy** on every call and return `DataTypeMismatch` when the stored
type does not match. For hot paths, access the stored vector once via
`std::get` / `std::visit` instead.

## `BlockReader` — the stream level

When the `DataManager` is too much (RAM, huge files, custom
aggregation), read the block stream yourself:

```cpp
#include <osf/osf.h>
#include <fstream>

std::ifstream in("measurement.osf", std::ios::binary);
auto header = osf::parseMagicHeader(in);          // Result<MagicHeader>
// … read the metablock bytes (header->metablockLen) and parse them …
auto meta = osf::parseMetablockJson(buf.data(), buf.size());

osf::BlockReader reader(in, *meta);
for (auto& blk : reader) {                        // input iterator + sentinel
    if (!blk) { /* hard error, iteration ends */ break; }
    std::visit([](auto const& kind) { /* StartData / ContinuedData / … */ },
               blk->kind);
}
auto stats = reader.stats();
```

Key properties:

- **`next()` primitive:** `std::optional<Result<Block>>` —
  `std::nullopt` = clean end (EOF, trailer consumed, or truncation),
  a value carrying an error = hard abort (e.g. `UnknownChannelIndex`).
- **Single-pass:** the iterator is an input iterator; a second
  iteration needs a fresh reader (and stream reset).
- **Skips stay visible:** deprecated/reserved control bytes and blocks
  of `Unsupported`-declared channels come through as
  `BlockKind::Skipped` with a `SkipReason`. The payload bytes are
  discarded without allocation by default; to look inside (e.g. into
  `bcStatusEvent` or `bcTrustedTimestamp` blocks, which are still
  skipped): `reader.withCaptureSkippedPayload(true)`. `bcMessageEvent`
  is decoded rather than skipped for `string`/`binary` channels, so it
  no longer needs this opt-in.
- **OSF4 trailer:** the optional `0xFFFF` info block + 40-byte trailer
  is consumed silently; `reader.trailerSeen()` reports it.
- The `BlockReader` does **not** decompress itself — for OSFZ you put a
  `DecompressingIStream` in front of it (exactly what the `DataManager`
  does).

## Transparent OSFZ

```cpp
#include <osf/compression.h>

std::ifstream raw("measurement.osfz", std::ios::binary);
osf::CompressionFormat fmt = osf::detectCompression(raw); // None/Zlib/Gzip, non-consuming

osf::DecompressingIStream in(raw);   // istream facade; inflates on demand
// use in like any std::istream: parseMagicHeader(in), BlockReader, …
```

Detection runs on the first two bytes (gzip `1F 8B`, zlib
`78 01/5E/9C/DA`; real OSF begins with `O` = `0x4F`, so it never
collides). Decompression is constant-memory (a streaming
`std::streambuf` over `z_stream`), best-effort on truncation, and free
of zlib types in the public header (PIMPL). `DataManager` uses this
layer automatically — `loadFromFile("x.osfz")` works with no extra
effort, and `stats.compressed` / `stats.compressionFormat` document the
finding.

## `ReaderStats` — telemetry

After every load (or via `reader.stats()`):

| Field | Meaning |
|---|---|
| `fileSizeBytes` | file size (when known) |
| `headerSizeBytes` / `metablockSizeBytes` / `dataSectionSizeBytes` | sizes of the three file sections |
| `elapsed` | wall-clock time of the block iteration |
| `channelsTotal` / `channelsWithData` / `channelsUnsupported` | channel counters |
| `blocksTotal` / `blocksRead` / `blocksSkipped*` / `blocksTruncated` | block counters by reason |
| `trailerSeen` | OSF4 info block/trailer encountered |
| `compressed` / `compressionFormat` | OSFZ detection |
| `perChannel` | `ChannelStats` per channel index: name, block/sample/byte counters, segment count, time range |

`operator<<` formats both structs multi-line for CLI output;
`formatBytes` / `formatDuration` are available individually.

```cpp
std::cout << mgr.stats;                     // multi-line summary
for (auto const& [idx, cs] : mgr.stats.perChannel)
    std::cout << cs << "\n";                // one line per channel
```

## Performance notes

- Real field files in the single-digit MB range load in a few
  milliseconds in release builds.
- The `DataManager` holds all samples in memory; as a rule of thumb a
  file needs roughly its uncompressed size in RAM. For larger holdings:
  use `BlockReader` in streaming mode.
- Flat accessors copy. A single `std::get` and working directly on the
  vector is the faster form for repeated access.

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
