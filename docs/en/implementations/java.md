---
title: Java implementation
description: The osf-java library — a complete Java 21 implementation of the Open Streaming Format with both OSF5 writers, JPMS encapsulation, transparent OSFZ, and the crc integrity profile; plus the osf-cli and osf-viewer tools
sidebar_position: 6
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - Maven
  - JPMS
  - JavaFX
  - crc32c
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

🇩🇪 [German version](../../de/implementations/java.md)

# Java implementation

:::info Status: available
The Java implementation is **complete and tested**. It reads OSF4 and OSF5,
writes OSF5 with both writer models, reads OSFZ transparently, and supports the
crc integrity profile. The authoritative decision sources are
[DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
(architecture) and [§24](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
(integrity profile); the current state lives in
[STATUS.md](https://github.com/optimeas/osf/blob/main/STATUS.md).
:::

:::tip Developer handbook
This page is the overview. The in-depth developer documentation lives in the
**Java in detail** sub-chapter:

| Page | Content |
|---|---|
| [Architecture](java/architecture.md) | Layer model, modules, JPMS encapsulation, data models, conventions |
| [Reading](java/reading.md) | `DataManager`, `DataChannel`, `BlockReader`, `ReaderStats`, transparent OSFZ |
| [Writing](java/writing.md) | `StreamingWriter`, `BlockWriter`, `ChannelDef`, metadata defaults, integrity profile |
| [Error handling](java/error-handling.md) | `OsfException` hierarchy, `verificationStatus()`, best-effort reader |
| [Tools](java/tools.md) | `osf-cli` (picocli verbs) and `osf-viewer` (JavaFX) |
| [Building & integrating](java/building.md) | Maven, Java 21, JPMS, dependencies, tests |
| [Cookbook](java/cookbook.md) | copy-paste recipes from inspection to embedded loop |
| [Internals](java/internals.md) | Encoder, chunking, integrity helper, parsers — for contributors |
:::

## Target audiences

Two main audiences: enterprise backends (Spring, microservices, optiCloud) and
big-data / AI pipelines (Spark, Flink, data analysis). Plus embedded Java that
records operating data on an industrial gateway — the same "both worlds"
situation §7 already describes for C++.

## Feature set

- **Reading:** OSF4 (XML metablock) and OSF5 (JSON metablock), a typed
  `DataManager` with a channel/segment model; a robust best-effort reader.
- **Writing (OSF5):** **both writers** — a `BlockWriter` (accumulate in memory,
  write in one pass) for batch workflows and a `StreamingWriter`
  (`FileChannel.force(true)` per block) for crash-safe embedded recording. Both
  produce **on-disk-identical** OSF5 files.
- **Transparent OSFZ:** gzip-wrapped files are decompressed automatically on
  read (`java.util.zip`, in the JDK).
- **Integrity profile `crc`:** optionally enabled on either writer; the reader
  verifies the metablock and frame CRCs (see below).

## Building and using

**Java 21** and **Maven**. Shipped as a Maven artifact
(`groupId=com.optimeas.osf`, `artifactId=osf-java`); the POM is
publish-ready (deployment to a public repository is still deferred).

```bash
# From the repository root: build and test the Java reactor
mvn -f implementations/java/pom.xml test
```

**JPMS** (Java Platform Module System): `module-info.java` exports only
`com.optimeas.osf`; internal helpers under `com.optimeas.osf.internal` stay
encapsulated — even against reflection.

**Dependencies:** Jackson (OSF5 JSON), StAX (OSF4 XML, in the JDK),
`java.util.zip` (OSFZ + CRC32C, in the JDK), SLF4J (logging facade). Binary I/O
via `ByteBuffer` over `FileChannel` with `LITTLE_ENDIAN`.

## Modules

Beyond the core library, the Java reactor ships two tools:

| Module | Purpose |
|---|---|
| **`osf-java`** | Core library — reading, both writers, OSFZ, integrity profile. |
| **`osf-cli`** | Command-line tool (picocli): `info`, `channels`, `dump`, `convert`; built as a runnable jar. |
| **`osf-viewer`** | JavaFX multi-channel viewer (min/max per pixel). Run: `mvn -pl osf-viewer javafx:run`. |

## Integrity profile (`crc`)

Optional OSF5 integrity profile at level `crc` (CRC32C, `java.util.zip.CRC32C`,
JDK-native; check value `0xE3069283`, byte-identical to Rust/C++/Delphi).

- **Reader:** `MagicHeaderParser` recognizes the `crc32c` token; `DataManager`
  verifies the metablock CRC before parsing, `BlockReader` verifies and strips
  the 4-byte frame CRC before the typed parse (fail-closed). Signature blocks
  (channel `0xFFFE`) are skipped and counted, so signed files stay readable.
  `ReaderStats` exposes `integrity` + `verificationStatus()`
  (`none`/`crc_valid`/`invalid`/`signature_unverifiable`).
- **Writer:** `setIntegrity(IntegrityProfile.CRC32C)` on **both** writers
  (default off) emits the token, the metablock CRC, and a per-block frame CRC.

The signing level (`signed`, Ed25519) is not implemented yet.
Foundations: [DECISIONS §24](https://github.com/optimeas/osf/blob/main/DECISIONS.md).

## Specification conformance

The Java implementation follows the same semantic rules as Rust, Python, C++ and
Delphi: all current data types (unsigned types via Java type promotion or
`BigInteger` for the full range), explicit rejection of the removed types,
`bytearray` as a read-side alias for `binary`, `channeltype` as the **data shape**
(scalar/vector/matrix/binary), the version-deterministic null-termination rule
for `string`/`binary`, and all four magic-header identifiers. Conformance is
checked against the shared reference-manifest contract
(`examples/reference_manifest.json`).

## Source code and further information

- Architecture decision: [DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
- Integrity profile: [DECISIONS §24](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
- Current status: [STATUS.md](https://github.com/optimeas/osf/blob/main/STATUS.md) · [github.com/optimeas/osf](https://github.com/optimeas/osf)
- Format specification: the [OSF format](../osf_general.md) chapter

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.en). Attribution: optiMEAS GmbH und optiMEAS Switzerland GmbH.
