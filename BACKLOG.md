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

### C++ StreamingWriter polish (post-Phase-7b)

Phase-7b Task 3 (`implementations/cpp/{include/osf/streaming_writer.hpp,src/streaming_writer.cpp}`,
commit `db273d9`) shipped with five observations from the
code-quality review parked for post-Phase-7b attention.

1. **`std::optional<DurableFile>` → `std::unique_ptr<DurableFile>`
   deviation needs spec ratification.** The spec/plan called for
   `std::optional<detail::DurableFile> durable_file_` with only a
   forward declaration of `DurableFile` in the header. MSVC's
   `std::optional<T>` eagerly evaluates `__is_trivially_destructible`
   and rejects forward-declared `T`. The implementer correctly
   switched to `unique_ptr` (which defers dtor instantiation to the
   TU that sees the complete type). Add a footnote to spec §2 Q10
   ratifying `unique_ptr<DurableFile>` as canonical for any public
   header that forward-declares the private type. Phase 7c
   (`BlockWriter`) and Phase 7d (`StaleValueGuard`) should adopt
   the same pattern when they reach this design point.

2. **`close()` Doxygen overstates the contract.** Header line for
   `close()` says "Final flush + fsync + file-close" but the
   implementation does NOT call `durable_file_->force()` before
   `durable_file_->close()` in the Streaming branch. Practical
   impact: zero — every preceding `do_write_block` already
   `force()`'d its data, and `start()` `force()`'d the magic
   header + metablock. So there is no unsynced data at close
   time. Fix options: either add a defensive `force()` before
   `close()` in the Streaming branch (cheap insurance, matches
   the documented contract), or trim the Doxygen to "file-close
   only — all data already durable from per-block fsync." Pick
   one before Phase 7c writes documentation that propagates the
   current ambiguity.

3. **`start()` error message misleading for Broken state.** The
   current message `"start: writer is past the Configure phase"`
   is correct for a writer in Streaming or Closed state, but
   confusing if the writer somehow reached Broken. Refine the
   check to surface the sticky error when `state_ == Broken` —
   matching the pattern used by `require_streaming_state()`.

4. **`sov_for(channel)` silently returns 2 for out-of-range
   channels.** The function is private and every caller goes
   through a `require_*` helper that already validates channel
   bounds. The fallback path is therefore dead code in a correct
   caller sequence. Replace with `assert(channel < channels_.size())`
   or convert to an internal-API contract (no fallback) for
   clarity.

5. **`OSF_STUB_NOT_IMPLEMENTED` macro uses `InvalidArgument`.** All
   stub returns share the InvalidArgument code, which is
   semantically "caller made a bad call." A stub that has not been
   implemented is better served by a distinct code. Tasks 4-6
   will replace the stubs with real implementations, so the
   concern is short-lived. If a test accidentally exercises a
   stub before its real body lands, the InvalidArgument error
   could mask a genuine arg-validation bug. Trigger to act:
   never — Tasks 4-6 close this naturally.

Plus three test-coverage gaps for the lifecycle: no test for
double-close from Configure (the dtor calls close() unconditionally
which silently consumes the second-close's InvalidArgument), no
test for move-ctor on a Streaming-state writer (the 14-member
move is complex enough to deserve a dedicated test), and no
self-move-assignment safety test. Defer trigger: Tasks 4-7 land
write methods that exercise the same move/close paths through
roundtrip tests; if no regression surfaces by Phase 7b close, the
gaps can be filled with one focused commit.

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
