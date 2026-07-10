---
title: Building & integrating
description: Maven build of the OSF Java library — prerequisites, reactor and modules, integration into your own project (Maven coordinates + JPMS requires), dependencies, tests, and publishing
sidebar_position: 6
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - Maven
  - JPMS
  - Build
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Building & integrating

The authoritative, continuously maintained build instructions live in the
`README.md` in the Java directory (`implementations/java/`). This page
summarizes the essentials and adds the integration scenarios for your own
projects. The layout of the library is described in
[Architecture](./architecture.md).

## Prerequisites

- **JDK 21 (LTS)** — the library is compiled against Java 21 as its
  baseline (`maven.compiler.release = 21`).
- **Maven 3.9+** — the reactor uses standard plugins; a `./mvnw` Maven
  wrapper is included and downloads Maven itself on first run.
- **Internet on the first build** — Maven pulls the dependencies (Jackson,
  SLF4J, test libraries) once into the local repository cache (`~/.m2`);
  after that the project builds offline.

**Java 21 is not an option** but the fixed baseline — moving to a newer
release would be a deliberate library upgrade, not a build switch.

## Quick start

```bash
git clone https://github.com/optimeas/osf.git
cd osf

# Build and run all tests (unit + conformance + round-trip + fuzz)
mvn -f implementations/java/pom.xml test

# Build the JARs (including the shaded CLI and the library)
mvn -f implementations/java/pom.xml package
```

`package` produces the library JAR for `osf-java`, an additional
executable fat JAR (`osf-cli.jar`) for `osf-cli`, and the viewer JAR for
`osf-viewer`. The viewer is typically launched directly:

```bash
mvn -f implementations/java/pom.xml -pl osf-viewer javafx:run
```

## Reactor and modules

The build is a Maven reactor. The parent POM
(`com.optimeas.osf:osf-parent:0.1.0-SNAPSHOT`, packaging `pom`) bundles
three modules and centralizes versions and plugin configuration:

| Module (`artifactId`) | Packaging | Content |
|---|---|---|
| `osf-java` | jar | The library proper — OSF4/OSF5 reader, both OSF5 writers, transparent OSFZ, the JPMS module `com.optimeas.osf` |
| `osf-cli` | jar | Command-line tool (picocli), built as an executable fat JAR via Shade; main class `com.optimeas.osf.cli.OsfCli` |
| `osf-viewer` | jar | JavaFX viewer for multi-channel display; main class `com.optimeas.osf.viewer.ViewerApp` |

Only `osf-java` is meant to be a library dependency; `osf-cli` and
`osf-viewer` are end-user applications — see [Tools](./tools.md).

## Integrating into your own project

The library is consumed as an ordinary Maven dependency:

```xml
<dependency>
  <groupId>com.optimeas.osf</groupId>
  <artifactId>osf-java</artifactId>
  <version>0.1.0-SNAPSHOT</version>
</dependency>
```

`osf-java` is a **real JPMS module**. If your own project is modular
(`module-info.java`), the OSF module must be requested explicitly:

```java
module my.app {
    requires com.optimeas.osf;
}
```

The module exports only the package `com.optimeas.osf`; internal packages
are deliberately encapsulated and not part of the public API — details in
[Internals](./internals.md). On the classic classpath (without a
`module-info.java`) the library works unchanged; the module is then loaded
as an automatic module.

## Dependencies

The runtime dependencies are deliberately lean — two external libraries
plus JDK building blocks:

| Dependency | Origin | Purpose |
|---|---|---|
| Jackson Databind 2.18.2 | external (Maven) | Parse and serialize the OSF5 metablock (JSON) |
| SLF4J API 2.0.16 | external (Maven) | Logging facade — the application brings the backend |
| StAX (`java.xml`) | JDK | Stream-parse the OSF4 metablock (XML) |
| `java.util.zip` | JDK | Transparent OSFZ decompression (gzip/zlib) on the read path |

SLF4J is only a facade: without a bound backend the library emits nothing
(no-op). `osf-cli` wires in `slf4j-simple` as its runtime backend; your own
applications pick their own (Logback, Log4j2, …). Error handling is
described in [Error handling](./error-handling.md).

The test dependencies (`test` scope only, non-transitive) are **JUnit 5**
(Jupiter), **AssertJ** (assertions), and **jqwik 1.9.1** (property-based /
fuzz testing).

## Tests

```bash
mvn -f implementations/java/pom.xml test
```

Test execution is handled by the **maven-surefire-plugin 3.5.2** with the
JUnit 5 Jupiter runner. The suite covers:

- **Unit tests** on synthetic data, covering each layer in isolation.
- **Conformance tests** against the generated reference files under
  `examples/` — the proof that all implementations read and write the same
  files bit-for-bit (see [Reading](./reading.md) and [Writing](./writing.md)).
- **Round-trip tests** that re-read written files and compare them field by
  field.
- **Property / fuzz tests** (jqwik) that generate random channel and block
  constellations.

Expectation: **all tests green**. `mvn … verify` additionally runs the
verify phase and matches the CI run.

## Publishing

The POMs are **publish-ready** — license (MIT), SCM, developer metadata,
and a `release` profile are in place. The profile (`-Prelease`) attaches
sources and Javadoc JARs, signs all artifacts with GPG
(`maven-gpg-plugin`), and stages them to Maven Central via the
`central-publishing-maven-plugin`. The **default build signs and deploys
nothing.**

Actual publication to Maven Central is currently **deferred** — until then
the library is built from source as described above. Practical recipes for
using it are in the [Cookbook](./cookbook.md); the full overview of the
Java implementation is in the [Java overview](../java.md).

## Known pitfalls

- **Corporate proxy / TLS interception:** the `./mvnw` wrapper downloads
  Maven itself on first start; behind a TLS-intercepting proxy this
  bootstrap can fail with certificate errors. Remedy: use the
  system-installed `mvn` (which uses the system trust store) or configure
  an internal mirror in `~/.m2/settings.xml`.
- **`module not found` / split package:** if this error appears when
  building a modular application, the line `requires com.optimeas.osf;` is
  usually missing from your own `module-info.java`, or you are trying to
  import a non-exported internal package.
- **JavaFX for the viewer:** `osf-viewer` needs the `javafx-controls`
  modules (21.0.5); the easiest way to launch it is via the
  `javafx-maven-plugin` (`mvn -pl osf-viewer javafx:run`), which sets the
  module path correctly.

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
