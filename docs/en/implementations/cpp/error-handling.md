---
title: Error handling
description: Result<T> and osf::Error in the exception-free core, the complete error-code catalogue and the opt-in osf::throwing layer
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - Result
  - Error
  - Exceptions
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Error handling

The core of the C++ implementation is **exception-free**: every
operation that can fail returns `osf::Result<T>` — a
`tl::expected<T, osf::Error>`. Callers who prefer exceptions put the
thin opt-in `osf::throwing` layer on top. The two styles can be mixed.

## `osf::Error` and `osf::Result<T>`

```cpp
struct Error {
    Code        code;     // stable category — branch on this
    std::string message;  // human-readable detail — for display only
};

template <typename T>
using Result = tl::expected<T, Error>;
```

Basic idiom:

```cpp
auto r = osf::DataManager::loadFromFile(path);
if (!r) {
    log("load failed [{}]: {}",
        osf::errorCategoryName(r.error().code),   // stable name, e.g. "io_error"
        r.error().message);
    return;
}
osf::DataManager const& mgr = *r;   // or r.value()
```

Rules:

- **Branch on `code`, display `message` only.** The wording of the
  message is not part of the API and may change.
- All `Result` returns are `[[nodiscard]]` — the compiler flags ignored
  errors.
- `errorCategoryName(code)` returns a stable string identifier for logs
  (static, no ownership).
- `Result<void>` signals pure success/failure operations
  (`writer.start()`, `writeToFile(...)`, …): `if (!r) …`.

## Error-code catalogue

### Input and API errors

| Code | Meaning | Typical source |
|---|---|---|
| `InvalidArgument` | API precondition violated: invalid `ChannelDef`, unknown channel index at the writer, `count == 0`, writer in the wrong lifecycle phase, non-positive sample rate | Writer |
| `IoError` | file/stream error: cannot open, read/write error, fsync failure | everywhere |
| `NotFound` | reserved for lookup APIs (channel lookups return `nullptr` instead) | — |
| `Unknown` | fallback without a more specific category; code of a default-constructed `Error` | — |
| `ParseError` | generic parse error when no more specific category fits (rare) | parsers |

### Magic header

| Code | Meaning |
|---|---|
| `InvalidMagicHeader` | the first line is not a well-formed OSF header — usually "not an OSF file" |
| `UnsupportedVersion` | header parseable, but the identifier is none of the four accepted spellings (`OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) |
| `MagicHeaderTooLong` | no line break within 128 bytes — definitely not an OSF file |

### Metablock

| Code | Meaning |
|---|---|
| `InvalidMetablock` | structural error: required field missing, number not parseable, `sizeOfLengthValue` ≠ 2/4 (would otherwise silently corrupt every block read), wrong root element |
| `JsonParseError` | OSF5: the metablock body is not valid JSON (parser diagnostic in `message`) |
| `XmlParseError` | OSF4: the metablock body is not well-formed XML (diagnostic + byte offset in `message`) |
| `RemovedInSpec` | the file uses a data type **removed** by spec rev 2026-05-04 (`pair`, `triple`, `candata`, `gpsdata`). Rejected hard — the old payload layout cannot be reproduced from a current build; the message names the replacement |

### Block stream

| Code | Meaning |
|---|---|
| `UnknownChannelIndex` | a block references a channel index without a metablock definition. Without the definition the width of the length field is unknown → corruption signal, hard abort |
| `InvalidBlock` | payload structurally broken (wrong length for the data type, equidistant block on a string channel, sample exceeds the streaming writer's block capacity, …) |
| `ChannelMixedBlockTypes` | a channel delivers both equidistant (`bcStartData`/`bcContinuedData`) and timestamped blocks — forbidden by the spec |
| `ContinuedDataWithoutStart` | `bcContinuedData` without a preceding `bcStartData` — without an open segment the continuation has no time base |
| `RelStampWithoutAnchor` | `bcContinuedRelStampData` without a prior absolute timestamp — the deltas have no anchor |
| `DataTypeMismatch` | requested type ≠ stored type (e.g. `asDoublesFlat` on an `int32` channel, `asStrings` on a binary channel) |

### What is deliberately **not** an error

| Situation | Behaviour |
|---|---|
| File ends mid-block (power loss) | best-effort: all complete blocks are delivered, `stats.blocksTruncated = 1`, iteration ends cleanly |
| Unknown future data type/channel type | the channel parses as `Unsupported`, blocks are consumed aligned as `Skipped`, the remaining channels load normally |
| Deprecated/reserved control bytes (old field files) | `BlockKind::Skipped` with a `SkipReason`, counters in `stats` |
| Deprecated channel fields (`scale`, `offset`, `physicalunit1..3`, …) | silently tolerated and ignored (real field files all carry them) |
| Channel lookup with no hit | `nullptr`, no `Result` |

## The throwing layer — `osf::throwing`

Header-only, opt-in (`#include <osf/throwing.h>`), deliberately **not**
in the umbrella header `<osf/osf.h>` and not compiled into the library.
Anyone who never includes it pulls in no exception machinery.

```cpp
#include <osf/throwing.h>

try {
    auto mgr = osf::throwing::load("measurement.osf");  // DataManager or throws
    osf::throwing::writeToFile(mgr, "copy.osf");

    osf::StreamingWriter w{path};
    auto ch = osf::throwing::unwrap(w.addChannel(def));  // Result<T> -> T or throws
    osf::throwing::unwrap(w.start());
    osf::throwing::unwrap(w.writeTimestampedSample<double>(ch, ts, value));
    osf::throwing::unwrap(w.close());
} catch (osf::Exception const& e) {
    // e.what()  — message (or category name if message empty)
    // e.code()  — Error::Code for programmatic branching
    // e.error() — the complete structured osf::Error
}
```

The layer consists of exactly three building blocks:

| Building block | Purpose |
|---|---|
| `osf::Exception : std::runtime_error` | carries the complete `osf::Error`; lives in `osf`, not `osf::throwing` |
| `osf::throwing::unwrap(Result<T>)` | universal adapter: extract the value or throw. Works with **any** `Result` of the core, including writer methods — so it needs no per-method throwing wrappers |
| `osf::throwing::load / writeToFile / writeTo` | throwing counterparts of the most common high-level operations |

## Choosing a style in practice

- **Library/embedded code, hot paths, codebases with
  `-fno-exceptions`:** stay on the `Result` core.
- **Application code with an existing exception strategy:** use
  `throwing` at the outer edge; everything internal stays `Result`.
- **Mixed:** `unwrap` selectively where an error would only propagate
  anyway — e.g. in a CLI `main` that has a top-level `try/catch`.

## Special case: writer sticky errors

The `StreamingWriter` remembers the first I/O error ("Broken" state) and
returns it on **every** subsequent call, including `close()`. In write
loops a single error check per iteration is therefore enough; the cause
is not lost even when evaluated only at the end.

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
