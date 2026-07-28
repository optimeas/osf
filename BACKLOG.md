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

### C++ C ABI (osf-c) — deferred surface (post-Phase-11)

Phase 11 (DECISIONS §23) shipped the `osf-c` C ABI shared library with a
deliberately bounded scope: full read path + a single round-trip
`osf_write_to_file`. The following were considered and parked:

- **Full sample-by-sample C builder.** A `osf_writer_*` family
  (`osf_writer_new`, `add_channel`, `add_equidistant_segment`,
  `add_timestamped_*` over all types, `write_to_file`) so C consumers can
  produce OSF from scratch rather than only re-export a loaded manager.
  Large surface (~30+ functions); wraps `BlockWriter` / `StreamingWriter`.
  Trigger: a concrete cross-language *writer* use case (the current
  consumers read + convert).
- **Per-exact-type numeric getters.** The current readers are
  convenience-typed (`read_f64` / `read_i64` + the exact `osf_channel_data_type`
  tag); add bit-exact `read_i8/u8/.../f32` if a consumer needs lossless
  access to `uint64` / `float` without going through `double`/`int64`.
- **`osf_load_buffer`** — load from an in-memory buffer / stream rather
  than only a file path (mirrors `DataManager::load_from_stream`). Needed
  for consumers that already hold the bytes (network, embedded flash).
- **Packaging.** Install rules for `osf-c` (the shared lib + the import
  library on Windows + `c_api.h`), a CMake package config / pkg-config so
  external projects can `find_package(osf-c)`. Pairs with a future
  release of the C++ library.

None block current consumers; `OSF_BUILD_C_API` stays OFF by default.

### CI: AddressSanitizer leg for C++ / osf-c

During the crc integrity work a **pre-existing use-after-free** in the pure-C
smoke test (`test_capi.c` read a borrowed channel name after
`osf_manager_free`) surfaced only via **AddressSanitizer** — on the normal CI
legs it showed up as an intermittent Windows segfault, not a clear failure. Add
an opt-in ASan (`-fsanitize=address`) CI leg for the C++ tree and the `osf-c` C
ABI so use-after-free / leaks are caught deterministically in CI rather than by
chance. Low effort; pairs with the existing warnings-as-errors legs.

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

### Public-release prep — residual optional polish

The `PUBLIC-PREP.md` tracker was closed and removed on 2026-06-07. Phase 1
(cleanup), Phase 3 (DE + EN developer docs) and Phase 4 (history purge) are
done; Phase 2 (Docusaurus integration) shipped as
`docs/scripts/sync-to-docusaurus.py` with the OSF docs synced to the Bitbucket
site (PR branch `osf-docs-sync-phase3`). The repo has been public since
2026-05-03, so the remaining items are optional polish, not blockers:

- CHANGELOG version bump + date for a public release line; cut a tag
  (e.g. `v0.11.0-public`) if desired.
- GitHub repo description + topics.
- Branch-protection rules review (they apply to public PRs too).
- Issue templates; re-read `CONTRIBUTING.md` for public-facing tone.

Deliver future doc changes to the public site via a PR to the Bitbucket repo
(`docs/scripts/sync-to-docusaurus.py`) — never push to its `main`, which
auto-deploys.

---

### Docs: refresh stale `last_update` dates + verify anchor links

The doc-currency audit (2026-07-10, `DOC_CURRENCY_AUDIT.md`) found ~18 docs pages
whose `last_update.date` predates their last content commit (touched by the CC-BY
and channeltype passes without a date bump) — refresh each alongside its next
content edit, not in bulk. Separately, a simple link scanner flagged 22 anchor
links in `osf_general.md` / `osf4.md` / `osf5.md`; these use explicit `{#id}`
headings and pass the authoritative Docusaurus build — spot-check the `{#id}`
anchors when those pages are next edited. Both are cosmetic (P3).

### Zero-length data blocks — find the producing writer (OSF-UP3)

**The reader side is closed.** A data block whose per-channel length field
reads `0` is a non-conforming writer artefact, not a truncation: the rule is
normative in `docs/{en,de}/osf_general.md` (*Zero-length data blocks*) and
recorded as [DECISIONS §25](DECISIONS.md#25-zero-length-data-blocks). All five
implementations now skip the frame, count it under a dedicated reason
(`ZeroLengthBlock` / `ZERO_LENGTH_BLOCK`) and keep scanning. Delphi, which used
to raise `EOSFFormatError` and fail the whole file, logs, counts
(`BlocksZeroLengthSkipped`) and continues — on the normal read path and on the
channel-filter path alike. The behaviour is held by the shared conformance
contract, not by convention: `examples/generated/malformed/osf5_zero_length_block.osf`
is a manifest key carrying `"anomalies": {"zeroLengthBlocks": 1}`, and all four
manifest-driven suites (Rust, C++, Java, Delphi) assert the count with no
per-suite registration step.

**What remains is the producer.** All seven writer classes in this repository
were audited on 2026-07-28 and cleared — none of them can emit a zero-length
frame. The per-writer evidence table, the two mechanisms the guarantee rests on
(buffer-derived length in Rust/Python/Java/Delphi, arithmetic length in C++),
the three risk shapes checked, and the two coverage gaps the audit itself leaves
open (the GPS encoder path is exercised by no test in either new audit suite;
Python's pass-through claim rests on reading `python/src/writer.rs`, with
nothing executable behind it) are all in
[`examples/generated/malformed/README.md`](examples/generated/malformed/README.md).
That narrows the suspect list to code *outside* this repository: the om kernel,
smartCORE and its `osfwriter` plugin, or device firmware. The blocks were
observed in real field recordings in July 2026; those recordings are full-size
and not redistributable, which is why the corpus carries a handcrafted minimum
instead.

**The instrument is `osftool verify`.** It reports a `Zero-length skips:` count
in the plain output and `zero_length_skipped_count` under `--json`, and raises a
warning naming OSF-UP3. Plain `verify` keeps exit code 0; `--strict` escalates
to 4. Two gotchas a corpus hunt will hit, both verified during this work:

- `verify --json` writes a **stream of concatenated JSON values** (log events,
  then a multi-line pretty-printed report) — not one document, and not NDJSON.
  `json.load()` and `ConvertFrom-Json` both fail on the whole stream; use a
  concatenated-value-aware parser (Python's `json.JSONDecoder().raw_decode` in a
  loop over the remaining text).
- `verify --json` does **not** emit `creator` / `created_utc` — exactly the two
  fields that correlate a file with a recording device and a firmware version.
  `osftool info` does emit `creator`, so a hunt has to run both commands and
  join the results on filename.

**Trigger to act:** a field file carrying the anomaly, together with the device
that recorded it and its firmware version. With those, record the provenance in
the corpus README and take the fix to the producing project. Without them there
is nothing further to do in this repository.

**Consumers that vendor the Delphi sources** should re-copy and drop whatever
local workaround they carry for the old abort behaviour. That side is tracked by
the consuming projects, not here.

### Conformance manifest — the anomaly contract is stricter in some suites than in others

The optional `anomalies` field added for OSF-UP3 is asserted by all four
manifest-driven conformance suites, but they do not agree on how strictly they
police the manifest itself. Three gaps, all surfaced while building the
contract:

- **An unknown extra key inside `anomalies` is silently ignored by Rust and
  C++**; Java and Delphi reject it. A typo'd key sitting next to a correct one
  (`{"zeroLengthBlocks": 1, "zeroLenghtBlocks": 3}`) therefore passes in two of
  four suites, and the misspelled expectation is checked by nobody.
- **A kind becomes legal the moment it joins the known-kinds set** (Delphi,
  Java) — nothing forces a matching assertion, so a kind could be declared in
  the manifest and compared by no suite. This is the other half of the rule the
  corpus README already carries (every kind needs at least one corpus file with
  a *nonzero* count, or its assertion is vacuous).
- **Delphi does not assert the manifest `version` field**; Rust, C++ and Java
  all check it against the `osf4_` / `osf5_` filename prefix. Given that a
  Delphi reader divergence in equidistance detection is already known (see the
  Java integrity section in `STATUS.md`), this is the asymmetry most likely to
  be hiding something.

None of these is a defect in a shipped reader — they are holes in the test
contract that would let the *next* anomaly kind land half-checked. Cheap to
close; best done the next time the manifest schema is touched.

### Writer divergence: an empty equidistant segment emits a block in Rust/Python/Java

Found by the OSF-UP3 writer audit. Rust (`writer.rs:1356`), Python (the same
call through the binding), and both Java writers (`BlockWriter.java:352`,
`StreamingWriter.java:487`) accept a start-of-segment call carrying no samples
and then emit the `bcStartData` opener unconditionally — a well-formed 21-byte
block carrying zero samples, the only place in the codebase where a block is
emitted for no data. C++ rejects the call at the writer entry point
(`streamingwriter.cpp:584`, `blockwriter.cpp:276`) and Delphi exits early
(`OSF.Filer.pas:2006`), so neither can produce it. The Rust manager-copy path
additionally does not skip zero-sample segments (`writer.rs:1048`) where C++
(`blockwriter.cpp:763`) and Java (`BlockWriter.java:529`) do, so a Rust round
trip of a manager holding such a segment reproduces it.

All readers handle it cleanly: the surrounding sample data survives and the
zero-length counter stays 0 — the block is 21 bytes, not zero-length, so it is
unrelated to OSF-UP3 beyond having been found by the same audit. This is a
conformance divergence rather than a defect, but it means the same
`DataManager` written by Rust and by C++ is not byte-identical, and it will
surface the moment anyone byte-compares the two writers' output. Resolving it
means picking one behaviour (reject, or emit) and writing it into the spec;
until then it is an undocumented per-implementation choice.

### Reader counters are not reachable from every high-level API

The per-reason block counters are the diagnostic surface for anomalies such as
OSF-UP3, but three surfaces do not expose them where callers actually work:

- **`osf-c` (the C ABI)** — `include/osf/capi.h` has
  `osf_manager_blocks_crc_failed` and `osf_manager_blocks_signature_skipped`
  but **no zero-length getter**. This is the one that matters most for the
  OSF-UP3 hunt: smartCORE and the om kernel integrate through C/C++, not
  through the Delphi manager or the Python bindings, so a C consumer currently
  cannot see the anomaly at all. A one-function, purely additive follow-up
  (`osf_manager_blocks_skipped_zero_length`, mirroring the two that exist).
- **Delphi** — `TOSFDataManager` exposes *no* reader counters at all.
  `BlocksCRCFailed`, `BlocksUnknownTypeSkipped`, `BlocksSignatureSkipped` and
  `BlocksZeroLengthSkipped` are reachable only from the low-level `TOSFFile`,
  so an application built on the manager API cannot tell that blocks were
  dropped. C++ and Java read the count off their high-level manager in their own
  conformance tests; Delphi's has to go through the filer to do the same.
- **Python (`osfdata`)** — the binding surfaces only a subset of `osf-core`'s
  `ReaderStats`: `blocks_skipped_unsupported`, `blocks_skipped_deprecated_type`
  and `blocks_skipped_reserved_type` are absent (`blocks_skipped_zero_length`
  was added for OSF-UP3).

Both are additive, mechanical changes.

### `osftool verify` — cap the mirrored per-block warnings

`verify` mirrors *every* per-block `llWarning` from the filer into its warnings
list, so a file with N zero-length blocks yields N+1 warnings: the summary line
plus N per-block lines, repeated across the headline count, the `[W]` list and
the JSON `warnings` array. The mechanism predates OSF-UP3 and is shared with the
CRC-failure and unknown-type warnings; a zero-length block is simply the first
anomaly likely to occur hundreds of times in a single file, which is what made
it visible. Suggested during review: cap the mirrored lines (the first N, e.g.
10) and append a `… N further warnings suppressed` line. Presentation only —
the counters and the exit code are already summary-based.

---

## Streaming Transport

### OSF5 Streaming Transport layer + UDP reference receivers (epic)

optiMEAS smartCore devices can stream live channel data over the network via
the `osfudpstreamer` module, which today serialises selected channels as an
**OSF4** byte stream chopped into UDP datagrams (broadcast by default, port
12345). The on-wire framing — the `OSF40` (header-length), `OSF41`
(header-chunk) and `OSF42` (data-chunk) packet types — is documented on
docs.optimeas.com; a minimal receiver reassembles the datagram payloads back
into the embedded OSF4 file and can then read it with a normal OSF4 reader.

This epic raises streaming to a first-class, **specified** capability of OSF,
built on **OSF5**. Two boundaries are deliberate. We do **not** bend the
OSF4/OSF5 core file-format spec to fit the transport — the transport is an
additive layer on top. And we make a **clean cut to OSF5**: the legacy
OSF4-UDP protocol (`OSF40/41/42`) is not adopted, extended, or given a
reference receiver here; OSF5 forward only.

Why a new transport rather than reusing the existing one — the current
framing has structural weaknesses a fresh OSF5 transport should fix:

- **No sequence number on data packets.** A lost datagram silently breaks
  the embedded block-stream framing, with resync only at the next periodic
  header re-broadcast. This is the most serious gap.
- The framing reuses the literal `OSF4` label and carries **no independent
  transport version**, so it cannot evolve separately from the payload.
- **Mixed endianness** — big-endian framing wrapping the little-endian OSF
  payload.
- **No per-packet / per-generation integrity** (e.g. CRC).
- **Broadcast-only** by default (layer-2); multicast would route across
  subnets.
- A large default max packet size forces IP fragmentation; an MTU-friendly
  default is preferable.

Scope, as separable tracks (this repo owns the spec and the reference
receivers):

- **A — Spec.** An "OSF5 Streaming Transport" section added to the OSF
  documentation: a datagram framing that carries (a) the OSF5 metablock as a
  repeated keyframe for late-join / resync and (b) the OSF5 block stream as
  sequenced data packets, addressing the design goals above. Lands as a docs
  section plus a `DECISIONS.md` entry. **This unlocks everything else.**
- **B — Reference receivers** in each implementation language, each a thin
  UDP de-framing layer in front of the existing OSF5 block reader:
  - **Delphi** — a live receiver that visualises channels in a chart (the
    `osfviewer` demo is the natural starting point).
  - **Python** — receive and re-emit / persist in another format (e.g. JSON,
    or a pandas / CSV / message-bus gateway).
  - **Rust** — a reusable de-framing crate (the foundation) plus a CLI that
    reconstructs `.osf` files or prints live statistics.
  - **C++** — the canonical reference on top of the `osf` library.
- **B′ — Reference sender / test harness** (e.g. in Rust) that emits the
  transport without a device, so receivers can be developed and CI-tested
  standalone.
- **C — Device-side producer.** A new OSF5-based streaming module on the
  smartCore side. This lives in the smartCore project and is tracked there,
  not in this repo; it is listed here only as the upstream counterpart so the
  transport spec is co-designed against a real producer.

Dependencies: B, B′ and C all depend on A. The detailed transport design
(packet layout, sequence / generation-id semantics, keyframe cadence,
multicast, integrity) is deferred to a dedicated design / brainstorming
session; until then this entry is a parked direction, not a commitment.

Trigger to act: that design session is scheduled, or a concrete need for a
live OSF5 network consumer arises.

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
