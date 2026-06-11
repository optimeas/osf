# OSF — Java Implementation

![Status](https://img.shields.io/badge/status-active-brightgreen.svg)
![Java](https://img.shields.io/badge/java-21-blue.svg)

Full OSF4 + OSF5 reader and OSF5 writer for the JVM. Part of the
[Optimeas Streaming Format](https://github.com/optimeas/osf) project.

## What This Implementation Provides

- **Full OSF4 + OSF5 reader** — parses both format generations including all
  channel types (scalar float/double/int, GPS, string, binary, equidistant).
- **Two OSF5 writer modes**:
  - `BlockWriter` — accumulates all samples in memory, then writes a
    self-contained OSF5 file in one shot (batch / post-processing use).
  - `StreamingWriter` — opens a file and writes one block per
    `writeBlock()` call with a fsync after each block; safe against power
    loss (the file is always a valid prefix of the intended recording).
- **Transparent OSFZ read** — gzip/zlib-compressed `.osfz` files are
  decompressed on the fly by `DataManager`; no API change required.
- **Best-effort truncation-tolerant reader** — partial/truncated files are
  read up to the last intact block; truncated blocks are silently skipped.
- **JPMS module** `com.optimeas.osf` — `module-info.java` with explicit
  `requires` and `exports`.

## Maven Coordinates

```xml
<dependency>
  <groupId>com.optimeas.osf</groupId>
  <artifactId>osf-java</artifactId>
  <version>0.1.0-SNAPSHOT</version>
</dependency>
```

Not yet published to Maven Central (deployment deferred). Build from source
as described below.

## Dependencies

**Runtime:**

| Library | Use |
|---------|-----|
| Jackson Databind 2.18 | OSF5 JSON metablock parsing |
| SLF4J API 2.0 | Logging facade (bring your own backend) |
| StAX, `java.util.zip`, `java.nio` | OSF4 XML parsing, OSFZ decompression, NIO paths (JDK-bundled) |

**Test only:** JUnit 5, AssertJ, jqwik (property-based / fuzz).

## Build and Test

Requires **Java 21** (LTS) and Maven 3.9+.

```bash
# Run all 204 tests (unit + conformance + round-trip + fuzz)
mvn -f implementations/java/pom.xml test

# Full verify (same as CI)
mvn -f implementations/java/pom.xml verify
```

Note: the `./mvnw` Maven wrapper in `implementations/java/` works on CI and
clean networks (downloads Maven on first use). On a TLS-intercepting proxy
(enterprise environments), use the system `mvn` instead — the wrapper's
bootstrap download may fail with certificate errors.

## Quick-Start: Reading an OSF File

```java
import com.optimeas.osf.DataManager;
import com.optimeas.osf.DataChannel;
import java.nio.file.Path;

DataManager mgr = DataManager.loadFromFile(Path.of("data.osf"));

for (DataChannel ch : mgr.channels()) {
    System.out.println(ch.name() + " (" + ch.dataType() + ")");
    double[] timestamps = ch.timestamps();   // nanoseconds since Unix epoch
    double[] values     = ch.asDoubles();    // NaN where no sample
}
```

OSFZ (compressed) files are loaded identically — just pass a `.osfz` path.

## Quick-Start: Writing an OSF5 File (BlockWriter)

```java
import com.optimeas.osf.writer.BlockWriter;
import com.optimeas.osf.writer.ChannelDefinition;
import com.optimeas.osf.DataType;
import java.nio.file.Path;

try (BlockWriter writer = new BlockWriter()) {
    int chId = writer.addChannel(
        new ChannelDefinition("temperature", DataType.DOUBLE, "°C"));

    long t = System.currentTimeMillis() * 1_000_000L; // ns
    writer.writeSample(chId, t,             22.5);
    writer.writeSample(chId, t + 1_000_000, 22.6);

    writer.writeToFile(Path.of("output.osf"));
}
```

For power-loss-safe recording use `StreamingWriter` — call `openFile()` once,
then `writeBlock()` per measurement cycle, and `close()` when done.

## Design and Specification

- Architecture and format decisions: [DECISIONS.md §21](../../DECISIONS.md#21-java-implementation)
- Public documentation: [docs/en/implementations/java.md](../../docs/en/implementations/java.md)
- Format specification: [docs/en/osf_general.md](../../docs/en/osf_general.md)

## License

MIT — see [LICENSE](../../LICENSE).
