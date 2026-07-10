---
title: Error handling
description: The OsfException hierarchy, the full error catalogue, the best-effort reader model and the integrity status in ReaderStats
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - OsfException
  - best-effort
  - integrity
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Error handling

The Java implementation reports failures through **exceptions**. Every
library error is an instance of `OsfException`, which extends
`RuntimeException` — so they are **unchecked**: no `throws` clause is
forced on you, no mandatory `try/catch`. Catch `OsfException` (or one of
its subclasses) where you want to handle a failure; otherwise let it
propagate to a central handler.

Separate from that stands the **best-effort reader**: a single broken or
truncated data block does *not* end the read with an exception — it is
skipped and counted in `ReaderStats`. Only structural failures *before*
the block stream (header, metablock) and hard precondition violations
throw. This split is the heart of the model: fail hard while the file is
fundamentally uninterpretable — keep going once only trailing blocks are
lost.

## The `OsfException` hierarchy

```java
package com.optimeas.osf;

public class OsfException extends RuntimeException {
    // message (and optional cause) — the message is for display only
    public static final class UnsupportedType       extends OsfException { }
    public static final class MalformedFile         extends OsfException { }
    public static final class UnknownHeaderToken    extends OsfException { }
    public static final class MetablockCrcMismatch  extends OsfException { }
}
```

Rules:

- **Branch on the type, display the message only.** `getMessage()` is
  human-readable detail and not part of the API — the wording may change.
  To react programmatically, test the class (`instanceof` / `catch`
  order).
- A plain `OsfException` **without** a more specific subclass is the
  "valid API, but misused, or I/O failed" case — chiefly from the
  writers.
- Where an underlying `IOException` is the root, it is carried as the
  `cause` (`getCause()`) so the stack trace stays complete.

## Error catalogue

### On open and parse (hard failures)

These exceptions occur before any payload block is read — the file is not
interpretable as OSF and is rejected wholesale.

| Exception | Meaning | Typical source |
|---|---|---|
| `MalformedFile` | Structural defect: no well-formed magic header, unknown version token, a required metablock field missing, an unparsable number, an invalid `sizeoflengthvalue` (≠ 2/4), unexpected end of input, no newline within the header window, invalid JSON (OSF5) or XML (OSF4). Carries the `IOException` as `cause` on an I/O root. | header, metablock, block parsers |
| `UnknownHeaderToken` | A magic-header token whose key the library does not understand (must-understand rule). Deliberately distinct from `MalformedFile` so an unknown integrity/extension token never surfaces as a misleading number-format error. | magic header |
| `MetablockCrcMismatch` | The CRC32C declared by the `crc32c` header token does not match the raw metablock bytes. Under an active integrity profile the file is rejected fail-closed — the metadata is treated as compromised. | metablock verification |
| `UnsupportedType` | The file uses a datatype the specification **removed** (`pair`, `triple`, `candata`, `gpsdata`). Hard rejection — the old payload layout is not reproducible from a current build. Also the **access-time** error: a type-wrong getter on `DataChannel` (e.g. doubles from a string channel). | metablock parser, `DataChannel` |

### On write and API misuse

The writers (`StreamingWriter`, `BlockWriter`) throw a plain
`OsfException` on any violated precondition — they signal a programming
error, not a data defect:

| Trigger | Example meaning |
|---|---|
| Unknown channel index | Sample written to a channel that was never declared |
| Type mismatch | Write type does not match the channel's declared datatype |
| Mixed block types | A channel emits both equidistant *and* timestamped blocks — spec-forbidden |
| Wrong lifecycle phase | Sample written while the writer is still configuring or already closed |
| No channel / length mismatch | `begin`/`writeTo` with no channels declared; `timestamps.length ≠ values.length` |
| Signed profile requested | The `ed25519` (signed) profile is rejected on write by this crc-level library |
| I/O failure | File not openable, write/`force` error — `IOException` as `cause` |

## Best-effort reader: what is deliberately **not** an error

The block-stream reader stops where a file is *still* fundamentally
readable instead of throwing. The outcome is recorded in `ReaderStats`,
reachable via `manager.stats()`:

| Situation | Behaviour |
|---|---|
| File ends mid-block (power loss, truncation) | Every complete block is delivered, `stats.truncationSeen()` becomes `true`, iteration ends cleanly |
| Broken/garbled block body | Best-effort stop at exactly that point: `truncationSeen()` = `true`, blocks already read stay valid |
| Unknown channel index in the block stream | Without a definition the length-field width is unknown → stop (`truncationSeen()`) rather than guess |
| Unknown future datatype | The channel is `UNSUPPORTED`; its blocks are skipped by length, all other channels load normally |
| A block's frame CRC does not match | The block is dropped, `stats.blocksCrcFailed()` increments, reading continues |
| Signature block (reserved channel `0xFFFE`) | Not verifiable by this crc-level library → skipped, `stats.blocksSignatureSkipped()` increments |
| Reserved/empty block types | Consumed as skipped; not an error |

Only failures *before* the block stream (header, metablock CRC, metablock
parse) throw — there, no partial interpretation is defensible.

## Integrity status — `ReaderStats.verificationStatus()`

After a load, `stats.verificationStatus()` condenses the integrity
outcome into a stable string (vocabulary from the specification):

| Value | Meaning | Recommended action |
|---|---|---|
| `"none"` | File carries no integrity profile | None — process normally |
| `"crc_valid"` | Profile `crc`, **every** block CRC verified | Treat the data as intact |
| `"invalid"` | Profile `crc`, at least one block CRC failed (`blocksCrcFailed() > 0`) | Warn/reject; the affected blocks are missing from the data |
| `"signature_unverifiable"` | A signed file (`ed25519`) that this crc-level library **cannot** verify | Do not present as "trusted-signed"; the payload is still readable |

`verificationStatus()` is derived purely from the declared profile and
the counters — an `"invalid"` specifically means the CRC-failing blocks
were skipped (not delivered).

## `try/catch` in practice

```java
import com.optimeas.osf.*;

try {
    DataManager mgr = DataManager.loadFromFile(Path.of("measurement.osf"));

    ReaderStats st = mgr.stats();
    if (st.truncationSeen()) {
        log.warn("File truncated at the end — {} blocks read", st.blocksRead());
    }
    switch (st.verificationStatus()) {
        case "invalid" -> log.error("CRC failure: {} blocks dropped", st.blocksCrcFailed());
        case "signature_unverifiable" -> log.warn("Signature not verifiable");
        default -> { /* none / crc_valid — ok */ }
    }

    double[] values = mgr.channelByName("temperature")
                         .orElseThrow()
                         .asDoubles();          // throws UnsupportedType on a type mismatch
} catch (OsfException.MetablockCrcMismatch e) {
    // metadata compromised — reject the file
} catch (OsfException e) {
    // MalformedFile, UnknownHeaderToken, UnsupportedType, I/O …
    log.error("Failed to load OSF: {}", e.getMessage(), e);
}
```

Practical rules:

- **Catch specific before general.** Specific subclasses
  (`MetablockCrcMismatch`, `UnknownHeaderToken`) first, then `OsfException`
  as the safety net.
- **Check `stats()` after loading** — a successful `load` does not mean
  every block arrived. Truncation and CRC failures are silent and live
  only in the counters.
- **Getter calls on `DataChannel` can throw `UnsupportedType`** when the
  requested type does not match the channel — test `dataType()` first or
  catch narrowly.

## Writer lifecycle

The `StreamingWriter` enforces a `CONFIGURE → STREAMING → CLOSED` state
machine. A call in the wrong phase — a sample before `begin()`, a write
after `close()` — throws `OsfException`. `close()` is idempotent (a second
call is a no-op) and moves reliably to `CLOSED` even if the final flush
hits an I/O error, so `try-with-resources` (the writer is `AutoCloseable`)
never leaves the file open.

For more detail see [Reading](./reading.md), [Writing](./writing.md),
[Architecture](./architecture.md) and the
[OSF format handbook](../../osf_general.md). Back to the
[Java overview](../java.md).

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
