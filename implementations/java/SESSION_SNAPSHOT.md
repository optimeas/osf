# Java Implementation — Session Snapshot

Saved state from the Java implementation architecture discussion,
paused 2026-05-24 during the cross-implementation spec-tightening
cleanup (Bit 7 / null-terminator clarification originating from the
C++ chat).

This file is the resume point. When work on the Java implementation
resumes (after the spec-tightening session completes), read this file
plus DECISIONS §7, §20, §21, BACKLOG.md, the latest spec docs in
`docs/de/` and `docs/en/`, and the listed open questions below, then
continue from Step 4 (scaffolding the initial directory structure).

## Status overview

| Step | Status |
|---|---|
| 1. Architecture discussion (7 questions) | ✅ Decided |
| 2. DECISIONS.md §21 added + adjusted | ✅ Committed (`1deaeb0`, `a3d6015`) |
| 3. BACKLOG.md extended with Java items | ✅ Committed (`f847b20`) |
| 4. Scaffolding initial directory structure | ⏸ Paused — prompt ready, not yet executed |
| 5. Phase 1 (Skeleton: data types, error classes, logging) | ⏸ Waiting |
| 6. Phases 2-9 (Magic Header → OSFZ) | ⏸ Waiting |

## Architecture decisions (from §21)

| Topic | Decision |
|---|---|
| Java baseline | Java 25 (newest LTS, September 2025) |
| Build system | Maven |
| Module structure | Single Maven module, `com.optimeas.osf` package root |
| Maven coordinates | `groupId=com.optimeas.osf`, `artifactId=osf-java`, version `0.1.0-SNAPSHOT` |
| Maven Central | Publication-ready POM, onboarding deferred |
| Dependencies | Jackson (JSON), SLF4J (logging), JUnit 5 + AssertJ (test); built-in StAX (XML), java.util.zip (OSFZ), java.nio (binary I/O) |
| Write strategy | Both BlockWriter (in-memory accumulate) and StreamingWriter (sample-by-sample with `FileChannel.force(true)` per block) |
| JPMS | Enabled, `module-info.java` exporting only `com.optimeas.osf` |
| Binary I/O | `ByteBuffer` on `FileChannel`, little-endian, heap-allocated buffers |
| Memory-mapped files | Out of initial scope, tracked in BACKLOG.md |
| Embedded micro-JVMs | Out of initial scope, tracked in BACKLOG.md |
| GraalVM Native Image | Out of initial scope, tracked in BACKLOG.md |

## Target audience (confirmed by project owner)

- Enterprise backends (Spring, microservices, optiCloud integration)
- Big-data and AI pipelines (Spark, Flink, Kafka-Connect, data analysts)
- Industrial gateway recording (concrete embedded project — Java on
  full OpenJDK, with power-loss safety as a hard requirement)

## Pending external dependency: spec-tightening session

A separate Claude Code mini-session is updating the OSF specification
documents to clarify two points that originated in the C++ Phase 7a
architecture discussion. These updates affect the Java writer and
reader logic, so the Java implementation work waits for them.

### Change 1 — Bit 7 is a write-time optimization, not a type-specific requirement

The previous spec wording "Bit 7 must be set" for `string`/`binary` in
`bcAbsTimeStampData` is being lifted. New uniform rule for all data
types:

- **Writer:** at N=1, the writer chooses — bit 7 = 0 (no uint32 N
  prefix, 4 bytes saved) or bit 7 = 1 with uint32 N=1. Both
  spec-conformant. At N>1: bit 7 = 1, uint32 N prefix mandatory.
- **Reader:** must handle both variants. Bit 7 = 0 → N=1 implicit;
  bit 7 = 1 → read uint32 N.
- **String/binary specifically:** writer always emits N=1
  (single-sample per block). Multi-sample string/binary in writers is
  abandoned across all implementations.

**Java writer plan:** at N=1 use bit 7 = 0 (saves 4 bytes), at N>1
use bit 7 = 1 with N prefix. Consistent with Rust and C++ planned
behaviour.

### Change 2 — Null-terminator for string/binary is version-dependent

The trailing `0x00` byte on `string` and `binary` payloads in
`bcAbsTimeStampData` was historically a Qt `QString` serialization
artefact. New spec rule:

- **OSF4:** writer **may** write with or without null byte. Reader
  **must** handle both (strip if last byte is `0x00`).
- **OSF5:** writer **must not** append a null byte (spec-conformant
  OSF5 writes the payload without sentinel). Reader **must** still
  be tolerant — strip the trailing `0x00` if present, to migrate
  pre-spec-update files.

**Java reader stripping strategy — open question (see below).**

## Open questions waiting on spec-tightening resolution

### Q1 — Trailing 0x00 stripping strategy (cross-implementation)

The spec-update mini-session must decide whether the OSF5 reader's
trailing-`0x00` toleration is:

- **Datatype-agnostic** (always strip last byte if 0x00) — simpler,
  but can incorrectly shorten binary payloads that legitimately end
  in 0x00.
- **Datatype-aware** (strip for `string` because UTF-8 cannot
  legitimately end in 0x00; do not strip for `binary` because
  arbitrary binary payloads can end in 0x00) — safer for binary
  data, slightly more code.

The Java owner's default (in the discussion) was datatype-aware. The
C++ sync-block proposed datatype-agnostic. **This is a
cross-implementation consistency question, not a Java-only decision.**
The spec-update session should set the project-wide policy. Java
follows whatever the spec says.

### Q2 — Delphi multi-sample string/binary backward compatibility

The Delphi writer emits multi-sample string/binary blocks with
per-sample `uint32` length prefixes — incompatible with the
equal-length spec reading. Java reader (like Rust and C++) will NOT
implement a Delphi-specific compatibility path. Project-owner default
confirmed: no workaround needed.

### Q3 — DECISIONS §21 patch after spec-tightening

§21 currently contains a "Spec revision 2026-05-04 compliance"
subsection stating:

> `string` and `binary` payloads in `bcAbsTimeStampData` end with a
> trailing `0x00` byte. Writer appends, reader strips. Uniform for
> OSF4 and OSF5.

This statement is being invalidated by the spec-tightening session.
§21 needs a patch reflecting the new rules. Project owner confirmed
this patch should land in the same cleanup pass as the Java MIT
license fix.

### Q4 — License fix (MIT, not Apache 2.0)

The repo was relicensed to MIT on 2026-05-20. The Java scaffolding
prompt prepared during the discussion (`java-scaffold-skeleton.md`)
still references Apache 2.0 in `pom.xml` `<licenses>` and `README.md`
"License" section. A mini-patch corrects this; also a repo-wide
audit should check DECISIONS §2, CONTRIBUTING.md, and other
sprach-specific READMEs for stale Apache 2.0 references.

## Resume sequence (when work continues)

1. Read this file, DECISIONS §7, §20, §21, BACKLOG.md, the updated
   spec documents in `docs/de/` and `docs/en/`.
2. Verify the spec-tightening commit(s) in the repo history — should
   touch `docs/de/osf_general.md`, `docs/en/osf_general.md`, and
   possibly the OSF4/OSF5-specific reference docs.
3. Check Q1 resolution (datatype-agnostic vs. datatype-aware
   stripping) in the spec.
4. Return to the Claude.ai web chat for Java to continue. The owner
   will provide:
   - Updated scaffolding prompt (`java-scaffold-skeleton.md` with
     MIT license and any spec-update-related notes).
   - Repo-doc-audit prompt for Apache 2.0 leftovers.
   - DECISIONS §21 patch prompt reflecting the new spec rules.
5. After cleanup commits land, execute the scaffolding prompt
   (Step 4) and continue with Phase 1.

## References

- DECISIONS §7 (Streaming vs. Block Mode) — revised by C++ chat
  (`6f386a9`)
- DECISIONS §20 (C++ Implementation Architecture)
- DECISIONS §21 (Java Implementation Architecture) — `1deaeb0`,
  `a3d6015`
- BACKLOG.md (Implementation Gaps and Conventions) — `f847b20`
- C++ Phase 7a snapshot (sibling document):
  `implementations/cpp/PHASE7_SNAPSHOT.md`

## Reusable prompts (drafted but not yet executed)

These prompts were prepared during the discussion but parked pending
the spec-tightening resolution:

- `java-scaffold-skeleton.md` — initial Maven project scaffolding
  (needs MIT license fix before use)
- (To be drafted post-spec-update) — DECISIONS §21 patch
- (To be drafted post-spec-update) — repo-wide Apache 2.0 audit
- (To be drafted post-spec-update) — combined scaffolding + cleanup
  prompt
