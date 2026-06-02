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

### C++ DurableFile hardening (post-Phase-7b)

Phase-7b Task 1 (`implementations/cpp/src/durable_file.{hpp,cpp}`,
commits `566709c`+`612d943`) shipped with three review observations
deferred to keep Task 1 within scope. Code-quality review verdict
was approved-with-nits; none are blockers for current call sites.

1. **`std::strerror` on POSIX is not guaranteed thread-safe.** The
   POSIX spec permits implementations to back it with a per-process
   static buffer; concurrent calls from multiple threads can corrupt
   each other's message. The current code path is exercised only on
   error from a single-threaded writer, but the gap is real. Fix:
   replace `last_errno_message` body with `strerror_r` (with
   `__GLIBC__`-conditional handling because the GNU variant returns
   `char*` and the XSI/POSIX variant returns `int`). Defer trigger:
   a multi-threaded POSIX consumer enters the picture, or hardening
   the writer for general-purpose POSIX use is prioritised.

2. **`DurableFile::write(data, 0)` semantics are implicit.** The
   `while (written < size)` loop makes a zero-length write a silent
   no-op success. This is the right semantics — a no-op write should
   not be an error — but it is undocumented and untested. Fix: add
   a one-line clarification to the hpp Doxygen ("`size == 0` is a
   no-op success") and a test in `test_durable_file.cpp`. The OSF
   block encoder never produces zero-byte payloads (a block always
   carries at least the control byte + length prefix), so a
   regression here cannot reach `StreamingWriter` from inside the
   library. Defer trigger: an external direct consumer of
   `DurableFile` appears, or the contract becomes load-bearing.

3. **Coverage gaps in `test_durable_file.cpp`.** Move-assignment
   path (the `(void) close()` of the prior handle) is not exercised
   by any test. `write` on a moved-from source object is not
   asserted (the `is_open()` guard handles it correctly today, but
   the contract is implicit). The `force_commits_buffered_writes`
   test's name overstates what the body can verify under exclusive-
   lock semantics — rename to `force_then_close_succeeds` or
   similar. Defer trigger: any future change to the move semantics
   or to the `close()` idempotency contract; both should land with
   matching test additions then.

### C++ serialize_metablock_json policy harmonisation (post-Phase-7b)

Phase-7b Task 2 (`implementations/cpp/src/metablock.cpp`, commit
`e75cce2`) shipped with two code-quality observations parked to
keep Task 2 verbatim with the plan.

1. **`"file"` always emitted as `{}` even when every `FileInfo`
   optional is unset, but `"infos"` is omitted when empty.** The
   parser tolerates both forms (round-trip is correct), but the
   asymmetry violates principle of least surprise. Pick a single
   policy before Phase 7c `BlockWriter` reuses this helper: either
   omit `"file"` when all `FileInfo` optionals are unset, or always
   emit both (including `"infos": []`). A one-line comment near the
   `if (!meta.infos.empty())` guard documenting the chosen rule
   would also suffice.

2. **`Info` with `DataType::Unsupported` silently round-trips to
   `"double"`.** `Info` has no `data_type_raw` field unlike
   `Channel`, so `info_to_json` calls `data_type_to_wire(...,
   /*raw_fallback=*/"")` and lands on the `"double"` fallback. This
   is a true round-trip loss for any `Info` whose datatype was
   originally unknown. Document at the call site, or add a
   `data_type_raw` to `Info` symmetric to `Channel` if round-trip
   fidelity is required.

Plus minor test-coverage gaps: no test sets `Channel::spectrum_type`
to verify the helper's branch (the spectrum-type helper is
exercised only by parser tests today), no `Info::physical_unit`
populated in the infos round-trip test, no test for
`serialize_metablock_json` on a `MetaBlock` with empty channels
list. Defer trigger: Phase 7c `BlockWriter` brings up the helper's
second consumer, OR a real-world OSF5 round-trip surfaces a missing
field.

### C++ StreamingWriter polish — RESOLVED in Phase 7c (2026-06-02)

The five `### C++ StreamingWriter … polish (post-Phase-7b)` batches
(Tasks 3–7 code-quality reviews, 18 minor items) were folded in during
the Phase-7c `BlockWriter` work:

- `MAX_PAYLOAD_FOR_SOV` → `max_payload_for_sov` and the `GPS_WIRE_SIZE`
  / `VARIABLE_BLOCK_OVERHEAD_BYTES` constants promoted to the shared
  `src/writer_common.hpp` (commit `1e37499`).
- `channeltype` normalised to the Delphi `scalar`-for-non-equidistant
  convention in the shared `build_metablock` (`336103f`).
- `close()` doxygen accuracy, `start()` Broken-state error surfacing,
  `sov_for` assert, `make_double_channel` split into scalar/equidistant
  helpers, test rename, tightened long-run chunking assertions
  (derived bounds 13/19/25), stale-comment + truncation-math cleanups,
  and three StreamingWriter lifecycle tests (double-close,
  move-construct, self-move) (`77a5dc2`).
- The shared `tests/integration/roundtrip_helper.hpp` now compares
  first/last sample values, closing the cross-impl value-comparison gap
  (`8691163`).

The `streaming_writer.cpp` file-split question was re-evaluated and
**declined** — the new `block_writer.cpp` TU plus the `writer_common`
extraction relieved the size pressure that motivated the watch.

**Residual minor items (low-risk, parked):**

1. StreamingWriter `int8_t` / `uint8_t` byte-exact tests (Task-5 #3)
   and the `oversized_binary_at_sov2` boundary-success case (Task-6 #4)
   were added for `BlockWriter` (`test_block_writer.cpp`) but not
   mirrored into `test_streaming_writer.cpp`. The Phase-7a encoder
   per-type tests already cover the wire format, so the gap is cosmetic.
2. The StreamingWriter cross-impl roundtrip `ChannelDef` construction
   in `test_streaming_writer_examples.cpp` still copies only
   `physical_unit` + `display_name` from the source ChannelMeta,
   dropping `physical_dimension` / `reference` / `comment` /
   `mime_type` / `time_increment_ns` (Task-7 #4). Invisible to current
   assertions. (BlockWriter's `from_manager` copies these correctly.)
3. Neither C++ writer emits the `created_utc` file-info field
   (pre-existing since Phase 7b; the serialiser omits null optionals,
   which is valid OSF5). Add if a future spec rev requires it.

Defer trigger: a strict downstream OSF reader that validates these
metadata fields, or a future StreamingWriter-focused session.

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
