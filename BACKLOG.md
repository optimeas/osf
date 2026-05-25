# OSF — Backlog

Open ideas, future considerations, and items intentionally deferred.
Separate from DECISIONS.md, which records decisions that have been
made. An entry in this backlog is **not** a commitment; it is a
note that the item has been considered and parked for later
discussion.

Organised by category. New entries should land in the most fitting
section (or open a new section if none fits).

---

## Documentation Strategy

### Multi-language documentation system (after Java implementation)

Documentation strategy for the project as a whole is pending a
dedicated design session, scheduled to follow the Java
implementation reaching feature parity with C++ and Python. Topics
to be decided:

- Tooling per language (native Doxygen / rustdoc / Pydoc / PasDoc
  vs. unifying through Sphinx with breathe / sphinx-rustdocgen /
  autodoc).
- Multi-natural-language strategy (German and English minimum; how
  to keep them in sync — gettext workflow vs. parallel Markdown
  sources, manual translation vs. machine-assisted).
- Hosting (GitHub Pages, ReadTheDocs, dedicated domain).
- Example-code structure: examples kept in the repo, tested in CI,
  embedded in docs via `literalinclude` or equivalent so they cannot
  drift out of sync.
- Adoption focus: docs aimed at external readers who want to
  integrate OSF into their own projects or analyse OSF files
  produced by others — not primarily at internal contributors.

The strategic goal: make OSF easy enough to adopt that engineers
choose it over CSV or Parquet as the carrier format for measurement
data. Documentation is a first-class product, not an afterthought.

---

## Implementation Gaps and Conventions

### Java: GraalVM Native Image support

The Java implementation (DECISIONS §21) targets standard OpenJDK as
its baseline platform. GraalVM Native Image compilation is a plausible
future use case — particularly for edge gateways and container-native
deployments where startup time and footprint matter — but is not part
of the initial scope.

The two technical obstacles are well understood. First, Jackson uses
reflection internally for binding JSON to Java objects, which Native
Image requires explicit configuration for via `reflect-config.json`.
Second, our own `module-info.java` and any reflective patterns in the
public API would need a `reachability-metadata.json` audit.

A lightweight approach was considered: keep the implementation
"reflection-aware" from day one (i.e. avoid unnecessary reflection in
our own code, and prefer the Jackson `ObjectReader` API over the
annotation-driven mapper for the metablock parser). This adds no cost
today and would shrink the Native Image effort later from "rewrite
the JSON path" to "generate config files". This soft guideline is
followed during implementation; the actual Native Image work is
tracked here.

Triggers for action: a customer or internal project requests
deployment of the Java OSF library as a native binary, or a benchmark
shows the JVM startup cost is the bottleneck for a CLI-style use case
(e.g. an OSF-to-CSV converter in a CI pipeline).

### Java: embedded micro-JVM support

DECISIONS §21 explicitly targets full OpenJDK as the Java baseline,
based on the project owner's confirmation that the current embedded
recording use case runs on an industrial gateway with a complete JVM.
Two other Java-embedded scenarios were named but parked:

- Embedded micro-JVMs (Azul Zulu Embedded, Eclipse OpenJ9 in stripped
  configurations). These have reduced standard libraries and may
  exclude or restrict Jackson and other reflection-heavy libraries.
- Industrial microcontrollers running Java in AOT-compiled form.

The likely action if either becomes relevant: provide a Jackson-free
JSON parser path for OSF5 metablocks. The metablock JSON is small
and structurally regular — a hand-rolled streaming parser of a few
hundred lines is feasible and would remove the dependency on
reflection-heavy libraries entirely. The XML path (StAX, JDK-built-in)
is already micro-JVM-friendly. SLF4J also has minimal-footprint
variants for restricted JVMs.

Triggers for action: a concrete project requires deploying the OSF
library on a micro-JVM. Until then, no work needed.

### Java: memory-mapped files and direct buffers for high-throughput I/O

DECISIONS §21 chose `ByteBuffer` on `FileChannel` with heap-allocated
buffers and little-endian byte order as the I/O strategy. This is
sufficient for typical OSF workloads — SSD-bound throughput in the
500 MB/s to 1 GB/s range, which exceeds what current measurement
scenarios produce.

Two further optimisations were considered and deferred:

- **Direct buffers** (`ByteBuffer.allocateDirect`): live outside the
  Java heap, reducing copy steps between kernel and JVM. Useful when
  heap pressure from I/O buffers becomes a problem (large
  long-running services with many concurrent readers).
- **Memory-mapped files** (`FileChannel.map`): the OS maps the file
  into the process address space and lazily pages it in. Can deliver
  another 30–50% throughput improvement on sequential reads of
  multi-GB files. Has platform-specific gotchas, particularly on
  Windows (file locks held until the mapping is cleaned up, which
  requires careful use of the `Cleaner` API).

Neither is justified by current measurement workloads. They are
plausible if the Java implementation becomes the I/O layer for a
high-throughput Spark or Flink connector working on multi-GB files
and the standard ByteBuffer path measurably bottlenecks. Until
benchmarks on a real workload show the issue, the added complexity
(particularly the Windows file-lock semantics of memory-mapped files)
is not worth carrying.

Triggers for action: profiling on a representative workload shows
the I/O path is the bottleneck; or a connector implementation
explicitly needs random access into multi-GB OSF files.

### Reader-comment correction: bit-7 wording in C++ and Rust readers

Both `implementations/cpp/src/reader.cpp:249` and
`implementations/rust/osf-core/src/reader.rs` (the equivalent
location around line 708, in the variable-length-payload parser)
carry a comment to the effect of *"per spec, bit 7 should be set
for variable-length blocks; we tolerate clear bit as implicit
N=1, matching the Rust reference"*.

That wording is factually wrong. The spec at
`docs/en/osf_general.md` §"bcAbsTimeStampData" explicitly
documents **both** forms as valid: bit-7 = 0 with implicit N=1
(single-sample, the canonical compact form, four bytes shorter)
and bit-7 = 1 with an explicit `uint32 N` prefix. The C++
Phase 7a encoder produces the spec-canonical bit-7 = 0 form for
string/binary; the Rust writer produces bit-7 = 1 with N=1
(see the separate entry below). Both readers accept either form
on input, which is correct — but the in-source comments make
the wrong form sound canonical.

Action: rewrite both comments to reflect the actual spec rule
(*"the spec allows both forms; the C++ encoder emits the
canonical bit-7 = 0 single-sample form; the Rust writer emits
the explicit-N variant pending the conformance fix in the
sibling backlog entry"*). Mini-session, two-file diff.

Discovered during the Phase 7a final code review on 2026-05-26
(commits `e839c66..7cdb561`, reviewer report archived in the
session transcript).

### Rust-writer spec conformance for single-sample variable-length blocks

The Rust writer in `implementations/rust/osf-core/src/writer.rs`
emits **bit-7 = 1** with an explicit `uint32 N=1` prefix for
single-sample string and binary `bcAbsTimeStampData` blocks. The
spec-canonical form is **bit-7 = 0** with no N-prefix (four bytes
saved per block). The C++ Phase 7a encoder is spec-conformant
(emits 0x08 / no prefix). Both readers tolerate either form, so
the divergence is not a correctness defect today — it only means
files written by the Rust writer are 4 bytes longer per
variable-length block than files written by the future C++
Phase 7b writer for the same content.

**Priority: HIGH** — fix before C++ Phase 7b lands, or document
the divergence as accepted. The risk if neither happens: when
Phase 7b's cross-implementation roundtrip tests start comparing
Rust-writer output to C++-writer output for the same input, the
4-byte-per-block delta will surface as bytewise mismatch and
will be initially confusing.

Two acceptable resolutions:

1. **Fix Rust to be spec-canonical.** A short patch to
   `binary_write.rs` / `writer.rs` to emit the bit-7 = 0
   compact form for single-sample variable-length blocks.
   Existing Rust roundtrip tests should still pass (reader
   tolerates both forms). Net repo size reduction across the
   shipped reference files in `examples/generated/`. Recommended.
2. **Accept the divergence and document it.** Add a DECISIONS
   entry explaining why the Rust writer uses the expanded form
   (e.g., for hypothetical future compatibility with multi-sample
   variable-length writers — though the spec rev 2026-05-24
   already abandoned that path). The C++ encoder stays
   spec-canonical; cross-impl wire-format-comparison tests would
   need per-implementation expected-file fixtures rather than
   shared ones.

Discovered during the Phase 7a final code review on 2026-05-26.

---

## How to add an entry

- Place under the most fitting section, or add a new section if
  needed.
- Each entry: a short `###` title summarising the topic, then a
  paragraph or two of context.
- State explicitly what was considered, what was decided (parked
  vs. ready to act), and what would unlock further action.
- Cross-link to relevant `DECISIONS.md` sections or
  `implementations/*/CHANGELOG.md` entries where applicable.
- Backlog entries are not commitments. They can be removed when a
  decision is taken (with the decision moving to `DECISIONS.md`)
  or when the item becomes irrelevant.
