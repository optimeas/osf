# Claude Code Session State

Last updated: 2026-05-25 (after the spec rev 2026-05-24 cleanup and
the Delphi `TOSFLog` listener-pattern refactor).

This file is a hand-off document for the next Claude Code session. Read
[STATUS.md](STATUS.md) and [DECISIONS.md](DECISIONS.md) for the
authoritative project state; this file only captures session-local
context that would otherwise be lost between runs.

## Two active tracks

The repo currently advances on two independent tracks:

1. **C++ implementation** — a phased plan in DECISIONS.md §20. Driven by
   focused per-phase sessions. Phases 1–7c done; next up: **Phase 7d**
   (`StaleValueGuard`, optional) or **Phase 8** (OSFZ read).
2. **Delphi tooling** — task-driven (briefs arrive as
   `~/Downloads/task-*.md`). Recently delivered: cross-impl null-terminator
   cleanup, then the `TOSFLog` listener-pattern refactor that replaced
   `OSF.Progress.*`. No fixed phase plan; each brief is self-contained.

A brief may also be repo-wide.

## Recent sessions (since 2026-05-22)

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

## C++ track — Phase 7d / Phase 8 are next

Phases 1–6 complete (skeleton, magic-header parser, OSF5 JSON +
OSF4 XML metablock parsers, block-stream reader, typed
`DataManager`). Phase 7a (private block-encoder), 7b
(`StreamingWriter`), and **7c (`BlockWriter`, completed 2026-06-02)**
all done. Both OSF5 writer classes are in place — the two-writer
write surface of DECISIONS §7 is fully realised. **271/271 ctest
green** (~11.6 s, 0 warnings under `/W4 /permissive-`).

Naming note: `osf::DataChannel` (the assembled-samples variant) is
distinct from `osf::Channel` (the metablock-level channel
*definition*, sitting in `metablock.hpp`). The Rust reference has
them in separate modules; in C++ they share `namespace osf` so the
names differ.

The two writers share `src/writer_common.{hpp,cpp}` (chunking
helpers, sizing constants, `build_metablock`). `BlockWriter`
accumulates in memory and emits at `write_to_file` / `write_to`,
auto-bumping variable `sizeoflengthvalue` 2 → 4; `StreamingWriter`
fsyncs per block and cannot auto-bump. Both emit `channeltype:
scalar` for non-equidistant channels (Delphi reference convention).
Rust reference: `implementations/rust/osf-core/src/writer.rs`.

**Next up:**

- **Phase 7d (optional):** `osf::StaleValueGuard` — a
  100-second-repeat-of-last-value layer over `StreamingWriter` for
  timestamped channels, decoupled from the writer itself.
- **Phase 8:** transparent OSFZ (gzip/zlib) decompression on read —
  removes the current `DataManager` OSFZ-rejection stub. Then
  Phase 9 (throwing convenience layer), Phase 10 (CI), Phase 11
  (C ABI wrapper).

A few cosmetic StreamingWriter test-coverage residuals are parked
in BACKLOG (`### C++ StreamingWriter polish — RESOLVED in Phase 7c`).

### C++ network caveat (local environment)

CMake `FetchContent` over HTTPS fails on this Windows host with
`CRYPT_E_NO_REVOCATION_CHECK`. Workaround — download with PowerShell
`Invoke-WebRequest` (uses the Windows cert store), extract once, and
point CMake at the local copy:

```powershell
Invoke-WebRequest -Uri "https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz" `
  -OutFile "$env:TEMP\googletest-v1.15.2.tar.gz" -UseBasicParsing
Push-Location $env:TEMP; New-Item -ItemType Directory -Force gtest-extract | Out-Null
Set-Location gtest-extract
& "<cmake.exe>" -E tar xzf "$env:TEMP\googletest-v1.15.2.tar.gz"; Pop-Location

cmake -B implementations\cpp\build -S implementations\cpp `
  -D FETCHCONTENT_SOURCE_DIR_GOOGLETEST="$env:TEMP\gtest-extract\googletest-1.15.2"
```

The cached extract is reused across runs and survives reboots — no
need to redownload unless `googletest` itself is bumped.

### C++ build flow (Windows)

`cmake.exe` / `ctest.exe` are not on PATH. Full path:
`C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\`.

```powershell
cmake -B implementations\cpp\build -S implementations\cpp [-D FETCHCONTENT_SOURCE_DIR_GOOGLETEST=...]
cmake --build implementations\cpp\build --config Debug
ctest --test-dir implementations\cpp\build -C Debug --output-on-failure
```

Always remove `implementations\cpp\build` after a successful verify.

## Active conventions

- **Push after every commit** — no batching.
- **Verify before push** for code-touching commits (compile / build /
  ctest must be green locally first).
- **Co-Authored-By trailer** on every commit:
  `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>`
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
- **CI** (`.github/workflows/ci.yml`) path filter does not cover
  `implementations/cpp/**` or the Delphi tree — those pushes trigger no
  CI run; verify locally.
- **Don't modify OSF library units from a demo / tool** unless a brief
  explicitly says so; demos and `osftool` depend on `src/` via
  `..\..\src\`.
- **Docs → PDF:** `python docs/scripts/docs-to-pdf.py` renders the
  Docusaurus docs tree to one combined PDF per language under
  `docs/pdf-out/` (gitignored build artifact — the `.md` files stay
  the single source of truth). Auto-discovers languages and new files.

## Pickup checklist for the next session

1. `git pull`.
2. Read `STATUS.md`, then this file, then `DECISIONS.md` §22 if you
   are about to add or modify Delphi logging code.
3. If continuing the **C++ track**: read DECISIONS §20 + §6 + §7, run
   the C++ build flow (271/271 baseline). Phases 1–7c are done (both
   OSF5 writers); next is Phase 7d (`StaleValueGuard`, optional) or
   Phase 8 (transparent OSFZ decompression on read).
4. If a new **Delphi brief** arrives as `~/Downloads/task-*.md`: read
   it, work it, compile-verify with dcc32/dcc64, commit + push.
5. Any new source file: MIT SPDX header, not Apache.
6. Any new Delphi log call: `Logger.Write(Msg, Level, 'TClassName')`
   from `OSF.Log` — do not reintroduce per-class `OnLog` properties.
