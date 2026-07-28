# Claude Code Session State

Last updated: 2026-07-29 (**OSF-UP4 — `bcMessageEvent` is read-mandatory**: a
normative spec rule + DECISIONS §26, control byte 4 decoded in all five
implementations instead of skipped, a dedicated `bcStatusEvent` counter
everywhere, the **Delphi meta cache and `osftool verify` fixed**, and a corpus
pair under the shared manifest contract. Spec revision in effect stays
**2026-07-28** — the OSF-UP4 spec text carries that date too. Suite totals
measured 2026-07-29: Rust 189/2 ignored, Java 265, C++ ctest 354, Delphi DUnitX
37, Python 23/1 skipped. The round before it, **OSF-UP3 — zero-length data
blocks**: a normative spec rule + DECISIONS §25, a dedicated skip reason and
counter in all five implementations, the **Delphi reader fixed** (it used to
abort the whole file), a malformed corpus file behind a new optional `anomalies`
manifest field, and `osftool verify` reporting the count. Earlier July work
already on `main`: the
documentation currency pass (2026-07-10), the OSF5 **integrity profile level
`crc`** across all five implementations, the `channeltype`-as-data-shape fix,
the shared `reference_manifest.json` conformance retrofit, and a Docusaurus docs
sync. cpp package **0.2.0**. Older session notes below may cite earlier states —
STATUS.md and the headers under `implementations/cpp/include/osf/` are ground
truth.)

This file is a hand-off document for the next Claude Code session. Read
[STATUS.md](STATUS.md) and [DECISIONS.md](DECISIONS.md) for the
authoritative project state; this file only captures session-local
context that would otherwise be lost between runs.

## Two active tracks

The repo currently advances on two independent tracks:

1. **C++ implementation** — a phased plan in DECISIONS.md §20, **now
   complete (phases 1–11)**: both OSF5 writers, the optional
   `StaleValueGuard`, transparent OSFZ read, the opt-in throwing layer, CI
   on Linux/macOS/Windows, and the `osf-c` C ABI shared library (§23). No
   next numbered phase — remaining C++ work is incremental/BACKLOG.
2. **Delphi tooling** — task-driven (briefs arrive as
   `~/Downloads/task-*.md`). Recently delivered: cross-impl null-terminator
   cleanup, then the `TOSFLog` listener-pattern refactor that replaced
   `OSF.Progress.*`. No fixed phase plan; each brief is self-contained.

A brief may also be repo-wide. The current repo-wide line is the **OSF5
integrity profile** (DECISIONS §24): level `crc` has shipped across all five
implementations (Rust/Python/C++/Java/Delphi) with a shared
`reference_manifest.json` conformance contract; level `signed` (Ed25519) is the
next step. Two repo-wide upstream briefs have since landed: **OSF-UP3**
(zero-length data blocks, DECISIONS §25), closed on the reader side — only the
hunt for the producing writer remains, and that lives outside this repository —
and **OSF-UP4** (`bcMessageEvent` read-mandatory, DECISIONS §26), closed
outright, since the firmware that produces the encoding is *conforming*. Java is
complete (§21); only a native **C** implementation remains unstarted.

## Recent sessions (since 2026-05-22)

### OSF-UP4 — `bcMessageEvent` (2026-07-28)

**The defect.** Deployed device firmware writes OSF4 `string` channels as
`bcMessageEvent` (control byte 4). All five reference readers skipped it as
deprecated, so those channels arrived **empty and silent** — no error, no
warning, and no statistic in three of the five (Rust and C++ counted a generic
deprecated-skip; Java's `ReaderStats` had no skip counters at all; Delphi
dropped the block through a dispatch default arm). In the field files this was
found in, that is fourteen channels of device metadata per file — exactly what
one opens such a file for. It was **our** gap, not the firmware's: the spec said
only that the type is no longer *produced* from OSF5 onwards, so emitting it in
OSF4 is conforming. The deeper cause sat in the spec table itself — row 4's
payload column had omitted the `uint32` length prefix the bytes actually carry,
so a reader built strictly from that row decodes the wrong layout even if it
does not skip the block.

**What changed.** Delivered on branch `worktree-osf-up4-bcmessageevent`
(15 implementation commits + this bookkeeping pass). The rule is normative in
`docs/{en,de}/osf_general.md` (block-type table rows 3 + 4, a dedicated
`bcMessageEvent (deprecated, read-mandatory)` subsection, a row in the
block-type restriction table, a corrected `datatype` table) and recorded as
**DECISIONS §26**; both it and OSF-UP3 carry `2026-07-28`, so the *spec revision
in effect* does not move. All five implementations decode the block as one
time-stamped sample of the channel's declared `datatype` into their **existing**
time-stamped representation — no new block kind. Bit 7 and any `datatype`
outside `string` / `binary` are skipped-and-counted, never guessed; `N = 0` is a
legal empty value and is *not* the OSF-UP3 anomaly. `bcStatusEvent` (byte 3)
stays skipped but gained a counter everywhere. Delphi additionally: the codec
unwraps the frame (`DecodeMessageEventPayload`, keeping `BlockType =
bcMessageEvent` — the tag is load-bearing in two dispatches, do not relabel it),
the manager feeds the sample to the channel, the **meta cache was fixed** (it
disagreed with the manager, so `osftool info` reported zero where `export`
decoded five), and `osftool verify` surfaces the new counter. The corpus pair
`examples/generated/osf4_message_event_string{,_equivalent}.osf` is a manifest
key all four conformance suites assert, proven non-vacuous by sabotage. The
writer audit (evidence in `examples/README.md`) shows nothing here can emit
byte 4 and that the round trip re-emits `bcAbsTimeStampData` with data intact.

**What is left.** No writer hunt — unlike OSF-UP3, the producing firmware is
conforming. The open items are in `BACKLOG.md`, and the first one matters:
**invalid UTF-8 in a `string` payload splits the implementations three ways**
(Rust/Python/Delphi fail the whole load, C++ keeps raw bytes, Java substitutes
`U+FFFD`), with Delphi's `EEncodingError` escaping `TOSFDataManager.LoadFromStream`
— an API documented as best-effort — and discarding every already-decoded
channel. OSF-UP4 exists to rescue legacy-firmware string channels, so CP1252 /
Latin-1 firmware is the plausible next field case. Also parked: the shared
string/binary builder falling through to `Binary` in C++/Java where Rust errors;
Delphi's misleading `BlocksUnknownTypeSkipped` name and its two uncounted
deprecated block types; Java's still-missing `unsupported` bucket; and the
evidence gaps (no executable round-trip test for C++/Delphi, no `binary` corpus
file for this block type, Rust missing a synthetic `N = 0` case). Suite totals
measured this round: Rust 189 passed / 2 ignored, Java **265** (`mvn -f
implementations/java/pom.xml -pl :osf-java test` — group-qualified selector),
C++ ctest **354/354**, Delphi DUnitX **37** under dcc32 *and* dcc64, Python
pytest 23 passed / 1 skipped.

### OSF-UP3 — zero-length data blocks (2026-07-28)

A data block whose per-channel length field reads `0` is a non-conforming writer
artefact — a conforming block always carries at least its control byte. Before
this round Rust, C++, Java and Python skipped such a frame and kept scanning
while **Delphi raised `EOSFFormatError` and aborted the whole file**, so a real
field recording was unopenable in Delphi and fine everywhere else. Nothing
tested the case and the corpus had no such file. Delivered on branch
`worktree-osf-up3-zero-length-blocks` (23 implementation commits + this
bookkeeping pass): the rule is normative in `docs/{en,de}/osf_general.md`
(*Zero-length data blocks*, DE+EN, identical anchors) and recorded as
**DECISIONS §25** — so the *spec revision in effect* moves to **2026-07-28**.
A pre-existing spec bug went with it: the "Basic structure" list said the length
field spans "the following data area" when it actually spans control byte +
payload (+ frame CRC at integrity level `crc`).

All five implementations now classify the case under a dedicated reason
(`ZeroLengthBlock` / `ZERO_LENGTH_BLOCK`) with its own counter rather than
misfiling it as a reserved-control-byte skip — no control byte is ever read on
such a frame, so the old label asserted something untrue. Rust and C++ both had
a `blocks_total` aggregation that silently undercounted until the new counter
joined it; Rust's `SkipReason` became `#[non_exhaustive]` (API-visible). Delphi
logs, counts (`BlocksZeroLengthSkipped` — local naming, so a grep for the shared
`blocksSkippedZeroLength` finds nothing there) and skips, on the normal read
path and the channel-filter path, with the unrecognised length-field-width
fall-through split into its own guard. The contract is
`examples/generated/malformed/osf5_zero_length_block.osf`, hand-assembled from
two writer outputs and registered in `examples/reference_manifest.json` under a
new optional `"anomalies": {"zeroLengthBlocks": N}` field that all four
manifest-driven conformance suites assert. `osftool verify` prints
`Zero-length skips:` (`zero_length_skipped_count` in `--json`) and warns naming
OSF-UP3; exit stays 0, `--strict` gives 4.

**What is left is the producer.** All seven writer classes here were audited and
cleared — the evidence table is in `examples/generated/malformed/README.md` — so
the suspect list is the om kernel, smartCORE `osfwriter`, or device firmware.
That hunt needs a field file plus its device and firmware version; `BACKLOG.md`
carries the entry conditions and two gotchas worth knowing before starting:
`osftool verify --json` emits **concatenated JSON values** (neither one document
nor NDJSON — `json.load()` and `ConvertFrom-Json` both fail; use `raw_decode` in
a loop), and it omits `creator` / `created_utc`, so a hunt must also run
`osftool info` and join on filename. Suite totals measured this round: Rust 178
passed / 2 ignored, Java **244** (`mvn -f implementations/java/pom.xml -pl
:osf-java test` — the selector must be group-qualified), C++ ctest 346, Delphi
DUnitX 29, Python 19 passed / 1 skipped.

### Documentation currency pass (2026-07-10)

Repo-wide doc audit (`DOC_CURRENCY_AUDIT.md`, PR #12) plus fixes: `java.md`
(DE+EN) rewritten from "planned" to the shipped state; C++/Java status corrected
on the implementation index + `README.md` + `implementations/README.md`; STATUS
meta blocks consolidated (PR #11); this file refreshed. Docusaurus sync of the
changed public pages followed.

### OSF5 integrity profile + reference-manifest wave (2026-07-08 … 07-10)

Level `crc` (crc32c) landed across all five implementations — Rust/Python
(PR #3), Delphi (PR #4), C++ `osf-cpp`+`osf-c` (PR #5), Java (PR #6): metablock
CRC + per-block frame CRC + signature-block skip, byte-identical check value
`0xE3069283`. The shared `examples/reference_manifest.json` gained the four
integrity files (sub-path keys) and all four conformance tests are now
manifest-driven (PR #9). DECISIONS §24 is the ladder (`none ⊂ crc ⊂ signed`);
level `signed` is the next step.

### channeltype = data shape (2026-07-09, PR #8)

Reconciled every implementation against the DE spec: `channeltype` is the
channel's **data shape** (scalar/vector/matrix/binary), NOT a storage mode.
Rust, Python, C++, and Java had invented `Equidistant`/`Timestamped`
channeltypes and silently dropped vector/matrix/binary channels (data loss);
fixed. Delphi was already correct (the reference). The planned C#/JS/MATLAB/Swift
implementations were also dropped (PR #7).

### C++ smartCORE coding-style refactoring (2026-06-12)

Burkhard supplied the smartCORE coding style sheet
(`V:\bitbucket\smartcore\coding-style-sheet-design.md`) and chose **full
alignment including the API break** (4 scope decisions: full API; full
file renames `.hpp`→`.h` lowercase no-separator; C ABI exempt; public
struct fields camelCase WITHOUT `m_`). Spec + plan live in
osf-superpowers (`specs/2026-06-12-cpp-smartcore-style-design.md`,
`plans/2026-06-12-cpp-smartcore-style.md`). Executed via
subagent-driven-development in an EnterWorktree worktree, 4 mechanical
layers, each verified at **321/321 ctest**:

1. `5fbbf04` file renames (45 files; `_p.h` internal headers; `capi.h`)
2. `d9ac260` public API → camelCase (~160-entry word-boundary table;
   wire-literal restore pass; flat-accessor macros now paste
   `as##SUFFIX##Flat`) + `aaddce7` comment/assert fixups
3. `72c8850` private members → `m_` + camelCase (47 names)
4. `6a5355e`/`e3dc3c9` src/include internal helpers + locals;
   `6ce7026`/`87ceb11` tests/examples locals + `osftest` namespace
   (GoogleTest test NAMES and the pure-C `test_capi.c` deliberately
   unchanged)

Docs: `71250f9` (handbook DE 8 pages + `cpp.md` + EN page + README +
cpp CHANGELOG **0.1.0 BREAKING** + CMake version) and `2367003`
(DECISIONS §20 naming revision + STATUS C++ section + root CHANGELOG
Unreleased) + `17a8c0d` review fixups (standalone-rule scrub: no Rust
comparison, no "smartCORE" wording in consumer-visible cpp docs).
Historical records (old CHANGELOG entries, dated decision narratives)
deliberately keep pre-rename spellings.

### C++ human-review round 1 response (2026-06-10)

Delivered as **PR #1** (`worktree-cpp-review-round1` → `main`, 8 commits,
CI green), via subagent-driven-development in an `EnterWorktree` worktree.
Addresses the round-1 reviewer items:

- **Docs/spec:** DECISIONS §4 table fixed (dropped "C++ builds on C"); **§12
  rewritten to the finalized post-close OSFZ writer design** — writers MAY
  produce OSFZ = **gzip**; `StreamingWriter` compresses only AFTER file
  finalization, **`BlockWriter` may inline**; source OSF deleted after the
  OSFZ is written + `fsync`'d (no read-back verify); downstream-trigger hook.
  `osf_general.md` EN+DE updated to match. C++17 "hard-pinned/fest verdrahtet"
  softened. PascalCase-types / snake_case-methods + `Kind` convention
  documented (kept, not renamed). `PHASE7_SNAPSHOT.md` removed.
- **"Phase N" cleanup:** internal phase nomenclature stripped from all cpp
  public headers/src/tests/per-package CHANGELOG (DECISIONS §20/STATUS
  phase-plan + root CHANGELOG kept by design).
- **Deps:** zlib 1.3.1→**1.3.2** (SHA `bb329a0a…d16`) + nlohmann/json
  3.11.3→**3.12.0** (SHA `aaf127c0…de63`).
- **Examples:** new `implementations/cpp/examples/` (inspect/dump/write/copy);
  `OSF_BUILD_EXAMPLES` now actually builds them (was a no-op).
- **Doxygen:** opt-in `OSF_BUILD_DOCS` → `doxygen_add_docs(osf-docs …)`,
  graceful-skip when Doxygen absent (verified generating with Doxygen 1.17.0,
  136 pages, 0 warnings).

**321/321 ctest green** with `OSF_BUILD_C_API=ON` (319 before the two
§13-defaults tests added 2026-06-12). Out-of-scope follow-ups
parked: vcpkg/conan packaging + CMake `install()`; OSFZ post-close tooling
(CLI `compress` verb + background-thread helper); class diagrams + "which
class for what" guide + Docusaurus expansion; `reference_manifest.json`
retrofit to C++. Env notes this session: the **Bash tool is broken** on this
host (use PowerShell); scoped subagent build/commit allow-rules were added to
`.claude/settings.local.json`.

### C++ Phase 11 — C ABI wrapper (2026-06-04)

Added the `osf-c` C ABI shared library — the final §20 phase, so **the
C++ Implementation Order is now complete (1–11)**. DECISIONS §23 is the
contract. New `include/osf/capi.h` (pure-C99 `extern "C"`) +
`src/capi.cpp` wrap `DataManager` + the round-trip write behind opaque
handles, `osf_status` codes, a thread-local last-error, and
caller-buffer copy-out readers; built only when `OSF_BUILD_C_API=ON`. A
standalone C99 test (`tests/capi/test_capi.c`) proves C-compat + DLL
linkage; CI builds + runs it on Linux/macOS/Windows. Cross-compiler fixes
on the branch: `enable_language(C)`, `CMAKE_POSITION_INDEPENDENT_CODE`.
ctest 304 → **305/305 green** with the C API on. Branch `phase-11-c-api`.

### C++ Phase 10 — CI integration (2026-06-03)

Wired the C++ build into GitHub Actions (`.github/workflows/ci.yml`):
added `implementations/cpp/**` to the push + pull_request path filters
and a `test-cpp` matrix job (ubuntu-latest / macos-14 / windows-latest)
that configures + builds + runs ctest with warnings-as-errors, gating
`summary`. New opt-in CMake option `OSF_WARNINGS_AS_ERRORS` (default OFF;
CI ON) drives `/WX` / `-Werror`. First-ever GCC/AppleClang build; two
hits fixed — a C4127 dead Float/Double branch in `block_writer.cpp`
(→ static_assert) and an unused `putBytes` test helper. Verified by
dispatching CI on the branch (`gh workflow run ci.yml --ref phase-10-ci`)
until all three OS legs + the full run were green (304/304 ctest each).
Branch `phase-10-ci`. Next: Phase 11 (C ABI wrapper, the last phase).

### C++ Phase 9 — throwing convenience layer (2026-06-03)

Added the opt-in, header-only throwing layer `include/osf/throwing.h`
(DECISIONS §20). `osf::Exception : std::runtime_error` wraps an
`osf::Error`; `osf::throwing::unwrap(Result<T>)` returns the value or
throws (works on any core `Result`, incl. writer methods — confirmed
scope: no per-method writer wrappers); free `throwing::load` /
`writeToFile` / `writeTo`. NOT in the `osf.h` umbrella and NOT
compiled into the library, so consumers who never include it pull in
nothing extra (verified by grep). New `test_throwing.cpp` (10 cases);
ctest 294 → **304/304 green**. Branch `phase-9-throwing`. Next: Phase 10
(CI integration).

### C++ Phase 8 — transparent OSFZ read (2026-06-03)

Added transparent gzip/zlib decompression on the read path, removing the
`DataManager` OSFZ-rejection stub. New `osf::DecompressingIStream`
(`include/osf/compression.h` / `src/compression.cpp`) classifies a
stream by its leading two bytes and inflates on demand via a custom
`std::streambuf` (constant-memory; `inflateInit2(MAX_WBITS|32)`
auto-detect; `z_stream` behind a PIMPL). `DataManager` wraps its input
and sets `stats.compressed` / `compression_format`. zlib is a PRIVATE
`osf_core` dependency via `OSF_USE_SYSTEM_ZLIB` (default FetchContent
zlib 1.3.2). New `test_compression.cpp` + `test_compression_examples.cpp`;
the former OSFZ-rejection stub tests were flipped/merged. ctest 283 →
**294/294 green**. `weather_station.osfz` now loads. Branch
`phase-8-osfz-read`. Next: Phase 9 (throwing convenience layer).

### C++ Phase 7d — `StaleValueGuard` (2026-06-03)

Added `osf::StaleValueGuard`, the optional freshness layer over
`StreamingWriter` that completes Phase 7. From-scratch C++ design (no
Rust/Delphi reference): a write-through wrapper caching each timestamped
channel's last `(timestamp, value)`; `poll(now_ns)` re-emits the cached
value of any channel idle `>= repeat_interval_ns` (default 100 s),
once per poll, for numeric + GPS channels (string/binary excluded; no
backfill, no internal clock/thread). New `include/osf/stalevalueguard.h`
+ `src/stalevalueguard.cpp` + 12 unit tests; ctest 271 → **283/283
green**. Branch `phase-7d-stale-value-guard`. Next: Phase 8 (OSFZ read).

### Cross-implementation null-terminator cleanup (2026-05-24, 2026-05-25)

Brief: `~/Downloads/cross-impl-null-terminator-cleanup-prompt.md`. The
spec was tightened to a **version-deterministic** null-terminator rule
for `string` / `binary` payloads in `bcAbsTimeStampData`:

- **OSF4** writers MUST append the trailing `0x00`; OSF4 readers MUST
  strip the last byte unconditionally.
- **OSF5** writers MUST NOT append; OSF5 readers MUST NOT strip.

Replaces the soft "strip-if-present" heuristic that had been the
2026-05-04 wording. No detection, no fallback. The whole chain (specs
EN+DE, DECISIONS §16+§21, Rust, Delphi, C++ readers, 17 reference files
under `examples/generated/`) was updated in one series. The Delphi
multi-sample variable-length writer layout (non-spec per-sample
`uint32` length prefix, never parsed by Rust / C++ readers) was also
removed entirely; auto-split into N single-sample blocks.

### Delphi logging + progress consolidation (2026-05-25)

The `OSF.Progress.*` family (8 units, ~1400 LOC, merge-specific
`IProgressReporter` interface plus six reporter implementations) plus
the `TOSFLoggable` mixin plus the per-class `OnLog` events were all
replaced by a single process-wide `TOSFLog` instance, exposed as the
global variable `Logger`. Caller-owned `TLoggerListener` instances
register with it; per-listener `MinLevel` filtering. Documented in
[DECISIONS.md §22](DECISIONS.md#22-delphi-logging--progress-architecture).

The CLI in-place progress bar moved to its own OSF-agnostic unit
`implementations/delphi/src/console/Console.ProgressBar.pas`. osftool
wires it up through `Cmd.Base`'s default listener callbacks.

## ⚠ Relicense — read before writing any new file

The whole project was relicensed **Apache 2.0 → MIT** on 2026-05-20
(commit `949f8a5`). Consequences for new work:

- New source files get a two-line header, not the old Apache block:
  ```
  // SPDX-License-Identifier: MIT
  // Copyright (c) 2026 Optimeas GmbH
  ```
  (`#` instead of `//` for Python). This applies uniformly across
  Delphi, Rust, Python (PyO3 bindings), and C++. The historical
  C++ "SPDX-only" exception was abandoned on 2026-05-25 when the
  surface inconsistency became visible during Phase 7a (cf. commit
  introducing the copyright line into all `implementations/cpp/`
  source files); file-level attribution now matches the rest of
  the implementations.
- `LICENSE` is the MIT text. Package manifests say `license = "MIT"`.
- Vendored third-party code under `implementations/cpp/third_party/`
  keeps its own upstream licenses (`tl::expected` CC0-1.0,
  `nlohmann/json` MIT, `pugixml` MIT) — never relicense those.

## Delphi track — current state

Library units live in `implementations/delphi/src/`; demos in
`implementations/delphi/demos/`; the CLI in
`implementations/delphi/tools/osftool/`. See STATUS.md "Delphi
implementation" + "Delphi CLI — osftool" for the full surface.

**New logging architecture (2026-05-25):** every OSF library unit
writes log + progress events to the global `Logger: TOSFLog`. Hosting
applications (osftool, demos, future GUIs) create and register a
`TLoggerListener`, set its `MinLevel` and event callbacks, and let
the singleton do the fan-out. See DECISIONS §22 + `OSF.Log.pas`.
**Do not reintroduce per-class `OnLog` events** — that was the pattern
this session retired.

Build flow (Delphi, Windows):

```powershell
# dcc32 — Win32; dcc64 — Win64. Compile from the project's own directory.
cd implementations\delphi
& "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\dcc32.exe" -B -Q OSFCompileCheck.dpr
cd tools\osftool
& "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin64\dcc64.exe" -B -Q OsfTool.dpr
```

DUnitX suite (29 tests as of 2026-07-28) — the form that actually works:

```powershell
cd implementations\delphi\tests
$DX = "C:\Program Files (x86)\Embarcadero\Studio\23.0\source\DUnitX"
New-Item -ItemType Directory -Force dcu32 | Out-Null       # -NU does not create it
& "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\dcc32.exe" `
    -B -Q -U"..\src;$DX" -I"$DX" -NU"dcu32" OSFTests.dpr
.\OSFTests.exe   # exit 0 = all pass
```

`-U` **replaces** the unit search path, so `..\src` is mandatory — without it the
build dies with `F2613 Unit 'OSF.Data.Channels' nicht gefunden`, because
`Test.OSF.Filer.Integrity.pas` uses `OSF.Data.Channels` / `OSF.Data.Manager`
without them being listed in the `.dpr`.

Always remove the `*.dcu` / `*.exe` / `Win64\` build artefacts after
a verify; they are gitignored but clutter `git status`.

The headless `OSFGeneratorCLI` at
`implementations/delphi/demos/osfgenerator/OSFGeneratorCLI.dpr` is
the non-interactive way to regenerate the 17-file reference set under
`examples/generated/` — useful after spec or writer changes:

```powershell
cd implementations\delphi\demos\osfgenerator
& "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\dcc32.exe" -B -Q OSFGeneratorCLI.dpr
.\OSFGeneratorCLI.exe   # writes into ..\..\..\..\examples\generated by default
```

## C++ track — §20 complete (phases 1–11)

Phases 1–6 complete (skeleton, magic-header parser, OSF5 JSON +
OSF4 XML metablock parsers, block-stream reader, typed
`DataManager`). Phase 7a (private block-encoder), 7b
(`StreamingWriter`), 7c (`BlockWriter`, 2026-06-02), 7d
(`StaleValueGuard`, 2026-06-03), 8 (transparent OSFZ read,
2026-06-03), 9 (throwing convenience layer, 2026-06-03), 10
(CI integration, 2026-06-03), and **11 (C ABI wrapper `osf-c`,
completed 2026-06-04)** all done — **the §20 Implementation Order is
complete**. Both OSF5 writer classes, the optional freshness layer,
transparent gzip/zlib decompression on read, the opt-in throwing layer,
and the `osf-c` C ABI shared library are in place, and CI builds + tests
them on Linux/macOS/Windows with warnings-as-errors. **321/321 ctest
green** on every leg with `OSF_BUILD_C_API=ON` (0 warnings under
`/W4 /permissive-` locally; `/WX` / `-Werror` in CI).

Naming note: `osf::DataChannel` (the assembled-samples variant) is
distinct from `osf::Channel` (the metablock-level channel
*definition*, sitting in `metablock.h`). The Rust reference has
them in separate modules; in C++ they share `namespace osf` so the
names differ.

The two writers share `src/writercommon_p.h + writercommon.cpp` (chunking
helpers, sizing constants, `build_metablock`). `BlockWriter`
accumulates in memory and emits at `writeToFile` / `writeTo`,
auto-bumping variable `sizeoflengthvalue` 2 → 4; `StreamingWriter`
fsyncs per block and cannot auto-bump. Both emit `channeltype:
scalar` for non-equidistant channels (Delphi reference convention).
Rust reference: `implementations/rust/osf-core/src/writer.rs`.

`StaleValueGuard` (Phase 7d) is a write-through wrapper over
`StreamingWriter` at `include/osf/stalevalueguard.h` /
`src/stalevalueguard.cpp`: it caches each timestamped channel's last
`(timestamp, value)` and `poll(now_ns)` re-emits the cached value of any
channel idle `>= repeat_interval_ns` (default 100 s), once per poll, for
numeric + GPS channels (no backfill, no internal clock/thread). No
Rust/Delphi reference — from-scratch C++ design.

Phase 8 (transparent OSFZ read) added `osf::DecompressingIStream`
(`include/osf/compression.h` / `src/compression.cpp`): a `std::istream`
that classifies a stream by its leading two bytes (gzip `0x1F 0x8B`,
zlib `0x78 {01,5E,9C,DA}`, else plain) and inflates on demand via a
custom `std::streambuf` (constant-memory; `inflateInit2(MAX_WBITS|32)`
auto-detect; `z_stream` behind a PIMPL so the header is zlib-free).
`DataManager` wraps its input before the magic-header parse and sets
`stats.compressed` / `compression_format`; `parseMagicHeader` stays
non-decompressing. zlib is a PRIVATE `osf_core` dependency via
`OSF_USE_SYSTEM_ZLIB` (default FetchContent zlib 1.3.2, pinned tarball
+ SHA256; `ON` → `find_package(ZLIB)`). Mirrors the Rust `compression`
module.

Phase 9 (throwing convenience layer) added the opt-in, header-only
`include/osf/throwing.h`: `osf::Exception : std::runtime_error` wraps
an `osf::Error` (`what()` = message or category name; `code()` /
`error()` for structured detail); `osf::throwing::unwrap(Result<T>)`
returns the value or throws — works on any core `Result` incl. the
writer methods, so no per-method wrappers; free `throwing::load(path)` /
`load(istream&)` and `writeToFile(mgr,path)` / `writeTo(mgr,ostream&)`.
Header-only, NOT in the `osf.h` umbrella and NOT compiled into the
library — opt-in by design.

Phase 10 (CI integration) wired the C++ build into `.github/workflows/ci.yml`:
`implementations/cpp/**` joined the push + pull_request path filters, and
a `test-cpp` matrix job (ubuntu-latest / macos-14 / windows-latest)
configures + builds + runs ctest with `-D OSF_WARNINGS_AS_ERRORS=ON`
(`/WX` / `-Werror`), gating `summary`. The new opt-in CMake option
`OSF_WARNINGS_AS_ERRORS` (default OFF) keeps local builds lenient. First
GCC/AppleClang build; two hits fixed (C4127 dead branch → static_assert,
unused test helper). Note: the CI Windows MSVC is *older* than the local
19.50, so a local `/WX` build is not a complete Windows-leg proxy —
verify on CI (`gh workflow run ci.yml --ref <branch>`).

Phase 11 (C ABI wrapper, the final §20 phase) added the `osf-c` shared
library (DECISIONS §23): `include/osf/capi.h` (pure-C99 `extern "C"`) +
`src/capi.cpp`, built only when `OSF_BUILD_C_API=ON`. Opaque
`osf_manager` (owns a `DataManager`) + borrowed `osf_channel` handles;
`osf_status` codes mirroring `Error::Code`; thread-local last-error;
caller-buffer copy-out readers (timestamps/f64/i64/gps) + borrowed
string/binary accessors; round-trip `osf_write_to_file` (OSF5). A
standalone C99 test (`tests/capi/test_capi.c`) proves C-compat + DLL
linkage; CI builds it on all three OSes. Two cross-compiler CMake fixes:
`enable_language(C)` and `CMAKE_POSITION_INDEPENDENT_CODE` (fold the
static core into the shared lib). Scope was read + round-trip write; a
full C builder is BACKLOG.

**Next up:** no numbered C++ phase remains — the §20 order is complete.
Incremental C++ options (all BACKLOG): a full sample-by-sample C builder
API, per-exact-type numeric C getters, an `osf_load_buffer` memory load,
and packaging the `osf-c` DLL. The bigger roadmap is the **other-language
tracks** (Java §21 has no code yet; C is README-only).

A few cosmetic StreamingWriter test-coverage residuals are parked
in BACKLOG (`### C++ StreamingWriter polish — RESOLVED in Phase 7c`).

### C++ network caveat (local environment)

CMake `FetchContent` over HTTPS fails on this Windows host with
`CRYPT_E_NO_REVOCATION_CHECK`. Workaround — download with PowerShell
`Invoke-WebRequest` (uses the Windows cert store), extract once into a
**persistent** directory, and point CMake at the local copy:

```powershell
$deps = "V:\external\osf-cpp-deps"
New-Item -ItemType Directory -Force $deps | Out-Null
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

Invoke-WebRequest -Uri "https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz" `
  -OutFile "$env:TEMP\googletest-v1.15.2.tar.gz" -UseBasicParsing
Push-Location $deps; & $cmake -E tar xzf "$env:TEMP\googletest-v1.15.2.tar.gz"; Pop-Location

# Transparent OSFZ read uses zlib. FetchContent dep: zlib 1.3.2.
Invoke-WebRequest -Uri "https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz" `
  -OutFile "$env:TEMP\zlib-1.3.2.tar.gz" -UseBasicParsing
Push-Location $deps; & $cmake -E tar xzf "$env:TEMP\zlib-1.3.2.tar.gz"; Pop-Location

cmake -B implementations\cpp\build -S implementations\cpp `
  -D FETCHCONTENT_SOURCE_DIR_GOOGLETEST="$deps\googletest-1.15.2" `
  -D FETCHCONTENT_SOURCE_DIR_ZLIB="$deps\zlib-1.3.2"
```

**Do not keep the extracts under `%TEMP%`.** They used to live there, and
Windows disk cleanup / Storage Sense empties that tree — it deletes the
*files* but leaves the *directory skeletons* behind. A hollow cache then
configures without complaint and fails at generate time with
`Target "test_error" links to: GTest::gtest_main but the target was not
found`, which points nowhere near the real cause. `V:\external\` is not
swept, so the extracts survive there.

**Check usability, not existence.** `Test-Path` on the directory returns
`True` for a hollowed cache. Verify the root `CMakeLists.txt` instead:

```powershell
Test-Path "$deps\googletest-1.15.2\CMakeLists.txt"   # must be True
Test-Path "$deps\zlib-1.3.2\CMakeLists.txt"          # must be True
```

Once extracted the copies are reused indefinitely — no need to redownload
unless `googletest` / `zlib` is bumped. (zlib's published SHA256
`bb329a0a…d16` is pinned in CMake; or build with
`-D OSF_USE_SYSTEM_ZLIB=ON` to use a system zlib instead.)

### C++ build flow (Windows)

`cmake.exe` / `ctest.exe` are not on PATH. Full path:
`C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\`.

```powershell
cmake -B implementations\cpp\build -S implementations\cpp `
  -D FETCHCONTENT_SOURCE_DIR_GOOGLETEST="V:\external\osf-cpp-deps\googletest-1.15.2" `
  -D FETCHCONTENT_SOURCE_DIR_ZLIB="V:\external\osf-cpp-deps\zlib-1.3.2"
cmake --build implementations\cpp\build --config Debug --parallel 1
ctest --test-dir implementations\cpp\build -C Debug --output-on-failure
```

Use `--parallel 1`: at default parallelism MSBuild intermittently fails with
`C1041` ("multiple CL.EXE write to the same .PDB file"), unrelated to the code.

Always remove `implementations\cpp\build` after a successful verify.

## Active conventions

- **Push after every commit** — no batching.
- **Verify before push** for code-touching commits (compile / build /
  ctest must be green locally first).
- **Co-Authored-By trailer** on every commit:
  `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`
- **Commit prefixes:** `feat(cpp|delphi|rust|python|delphi-demo):`,
  `test(...):`, `fix(...):`, `refactor(...):`,
  `docs(status|changelog|decisions|backlog|spec):`, `chore:` for
  repo-wide changes.
- **New source files use the MIT SPDX header** (see relicense section).
- **Repo CHANGELOG** is Keep-a-Changelog, currently `[0.10.0]`.
  Per-package CHANGELOGs (`implementations/<lang>/CHANGELOG.md`) are
  version-decoupled from the repo line. Repo headers use an em-dash
  (`## [X.Y.Z] — DATE`), per-package use a hyphen.
- **Vendoring pattern:** tag-pinned URL, SHA256 verified, byte-identical
  drop except the LICENSE, which is renamed and prefixed with two
  provenance lines (`# Vendored from <url>` / `# Renamed from <name>…`).
- **CI** (`.github/workflows/ci.yml`) covers the Rust, Python, **and C++**
  trees (`implementations/{rust,python,cpp}/**` + `examples/**` +
  `.github/workflows/**`); the C++ `test-cpp` job builds + tests on
  Linux/macOS/Windows with warnings-as-errors. The **Delphi** tree is
  still uncovered (no hosted Delphi toolchain) — Delphi pushes trigger no
  CI run; verify those locally with dcc32/dcc64.
- **Don't modify OSF library units from a demo / tool** unless a brief
  explicitly says so; demos and `osftool` depend on `src/` via
  `..\..\src\`.
- **Docs → PDF:** `python docs/scripts/docs-to-pdf.py` renders the
  Docusaurus docs tree to one combined PDF per language under
  `docs/pdf-out/` (gitignored build artifact — the `.md` files stay
  the single source of truth). Auto-discovers languages and new files.
- **STATUS meta stays in sync.** When you add or change a per-implementation
  section in `STATUS.md`, in the same pass re-check the header table (tag /
  spec revision), the "Next session priorities" block, and the "Open / known
  follow-ups" list for contradictions and fix them — the meta blocks must
  never lag the section you just edited.
- **Log every finished task here.** On completing a task, add a ≥3-line entry
  to "Recent sessions" above and update the "Last updated" line at the top of
  this file, so this hand-off never goes stale.

## Pickup checklist for the next session

1. `git pull`.
2. Read `STATUS.md`, then this file, then `DECISIONS.md` §22 if you
   are about to add or modify Delphi logging code.
3. If continuing the **C++ track**: read DECISIONS §20 + §6 + §7 (+ §23
   for the C ABI), run the C++ build flow (**321/321** with
   `-D OSF_BUILD_C_API=ON`; configure with the local
   `FETCHCONTENT_SOURCE_DIR_ZLIB` + `FETCHCONTENT_SOURCE_DIR_GOOGLETEST`
   overrides — see the network caveat below; note the zlib extract dir is
   now `zlib-1.3.2`). **All eleven §20 phases are
   done** (writers, `StaleValueGuard`, OSFZ read, throwing layer, CI, and
   the `osf-c` C ABI) — there is no next numbered phase; remaining C++
   work is incremental/BACKLOG. CI runs on C++ pushes — verify
   cross-compiler legs via `gh workflow run ci.yml --ref <branch>`.
4. If a new **Delphi brief** arrives as `~/Downloads/task-*.md`: read
   it, work it, compile-verify with dcc32/dcc64, commit + push.
5. Any new source file: MIT SPDX header, not Apache.
6. Any new Delphi log call: `Logger.Write(Msg, Level, 'TClassName')`
   from `OSF.Log` — do not reintroduce per-class `OnLog` properties.
