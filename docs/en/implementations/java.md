---
title: Java implementation
description: Planned Java implementation of the Open Streaming Format — architecture decided, no code yet
sidebar_position: 6
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - Maven
  - JPMS
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
---

🇩🇪 [German version](../../de/implementations/java.md)

# Java implementation

:::info Status: planned
The Java implementation is **architecturally decided but not yet
implemented** — no code exists yet. The points below document the decisions
taken; the authoritative source is
[DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md).
:::

## Target audiences

Two main audiences: enterprise backends (Spring, microservices, optiCloud)
as well as big-data/AI pipelines (Spark, Flink, data analysis). On top of
that there is a concrete embedded project in which a Java application records
operational data on an industrial gateway — the same "both worlds" situation
that §7 already describes for C++.

## Planned architecture

- **Java 25** (current LTS) and **Maven** as the build system. Shipped as a
  single Maven artifact (`groupId=com.optimeas.osf`,
  `artifactId=osf-java`).
- **JPMS** (Java Platform Module System): `module-info.java` exports only
  `com.optimeas.osf`; internal helpers under `com.optimeas.osf.internal`
  stay encapsulated — even against reflection.
- **Both writers**, analogous to C++: a `BlockWriter` (accumulate in memory,
  write in one pass) for batch workflows and a `StreamingWriter`
  (`FileChannel.force(true)` per block) for power-loss-safe embedded
  recording. Both produce on-disk-identical OSF5 files.
- **Dependencies:** Jackson (OSF5 JSON), SLF4J (logging facade),
  `java.util.zip` (OSFZ, in the JDK), StAX (OSF4 XML, in the JDK). Binary I/O
  via `ByteBuffer` on `FileChannel` with `LITTLE_ENDIAN`.

## Specification conformance

The Java implementation follows the same semantic rules as Rust, Python and
C++ (spec rev 2026-05-04): all current data types (unsigned types via Java
type promotion or `BigInteger` for the full range), explicit rejection of
the removed types, `bytearray` as a read alias for `binary`, the
version-deterministic null-termination rule for `string`/`binary`, and all
four magic-header identifiers.

## Source code and further reading

- Architecture decision: [DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
- Current status: [github.com/optimeas/osf](https://github.com/optimeas/osf)
- Format specification: chapter [OSF format](../osf_general.md)
