# Claude Code Session State

Last updated: 2026-05-22 (after the osftool live-progress rework).

This file is a hand-off document for the next Claude Code session. Read
[STATUS.md](STATUS.md) and [DECISIONS.md](DECISIONS.md) for the
authoritative project state; this file only captures session-local
context that would otherwise be lost between runs.

## Two active tracks

The repo currently advances on two independent tracks:

1. **C++ implementation** — a phased plan in DECISIONS.md §20. Driven by
   focused per-phase sessions. Next up: **Phase 4**.
2. **Delphi tooling** — task-driven (briefs arrive as
   `~/Downloads/task-*.md`). Recently delivered: the OSF merger and the
   `osftool` CLI. No fixed phase plan; each brief is self-contained.

A brief may also be repo-wide (the most recent one was the Apache→MIT
relicense).

## ⚠ Relicense — read before writing any new file

The whole project was relicensed **Apache 2.0 → MIT** on 2026-05-20
(commit `949f8a5`). Consequences for new work:

- New source files get a two-line header, not the old Apache block:
  ```
  // SPDX-License-Identifier: MIT
  // Copyright (c) 2026 Optimeas GmbH
  ```
  (`#` instead of `//` for Python). C++ files in `implementations/cpp/`
  keep their minimal one-line `// SPDX-License-Identifier: MIT` house
  style — no copyright line.
- `LICENSE` is the MIT text. Package manifests say `license = "MIT"`.
- Vendored third-party code under `implementations/cpp/third_party/`
  keeps its own upstream licenses (`tl::expected` CC0-1.0,
  `nlohmann/json` MIT) — never relicense those.

## Delphi track — current state

Library units live in `implementations/delphi/src/`; demos in
`implementations/delphi/demos/`; the CLI in
`implementations/delphi/tools/osftool/`. See STATUS.md "Delphi
implementation" + "Delphi CLI — osftool" for the full surface.

Recently delivered (task-driven):

- `OSF.Filer` gained a read-side `ChannelFilter`, transparent OSFZ
  (gzip) decompression, and OmniXML-based OSF4 XML parsing (no MSXML
  dependency).
- `OSF.Meta.Cache` (sidecar `.json` cache) and `OSF.Merger` (interval
  merge across many OSF/OSFZ files) — new units.
- `demos/osfmerger/` — VCL merger GUI (Win64).
- `tools/osftool/` — verb-based CLI with nine commands (merge, export,
  info, channels, stat, cache, config, convert, verify). Win64-primary;
  `.dproj` also carries OSX64/OSXARM64/Linux64 configs and the source is
  conditional-compilation clean for them (verified by inspection only).
- `OSF.Export.CSV.Unified` — single-timeline CSV exporter.
- The `osftool merge` live progress display was reworked — a single
  in-place progress-bar line replaces the old two-line ANSI block,
  driven by a new generic `StartProgress`/`DoProgress`/`EndProgress`
  triplet on `IProgressReporter` (commit `cf77461`). `osftool.md`
  (EN + DE) was brought in sync and gained the previously
  undocumented HDF5 export path (commit `6f8c7e7`).

The standalone `OsfMerge.dpr` was superseded by `osftool merge` and
removed.

Build flow (Delphi, Windows):

```powershell
# dcc32 — Win32; dcc64 — Win64. Compile from the project's own directory.
cd implementations\delphi
& "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\dcc32.exe" -B -Q OSFCompileCheck.dpr
cd tools\osftool
& "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin64\dcc64.exe" -B -Q OsfTool.dpr
```

Always remove the `*.dcu` / `*.exe` build artefacts after a verify;
they are gitignored but clutter `git status`.

## C++ track — Phase 4 is next

Phases 1, 2, 3 complete (skeleton, magic-header parser, OSF5 JSON
metablock parser). 57/57 ctest cases green as of commit `152d1ba`.

**Phase 4 — OSF4 XML metablock parser** is the immediate next step:

- Vendor `pugixml` (`pugixml.hpp` + `pugixml.cpp` + `pugiconfig.hpp`,
  MIT) under `implementations/cpp/third_party/pugixml/` following the
  Phase-1 vendoring pattern (tag-pinned URL, SHA256 verified, LICENSE
  renamed + prefixed with two provenance lines). pugixml has a `.cpp`
  file unlike the prior two header-only vendors — it compiles into
  `osf_core` directly, not via the `osf::headers` SYSTEM include.
- Implement `osf::parse_metablock_xml(uint8_t const*, size_t)` against
  the existing `MetaBlock` data model in `include/osf/metablock.hpp`
  (shared with the JSON parser). Symmetric population with the JSON
  parser is the success criterion.
- Rust reference: `implementations/rust/osf-core/src/meta_xml.rs`.
- Commit shape: vendor → API surface → implementation → unit tests →
  integration tests against `examples/generated/osf4_*.osf` plus
  `examples/motorbike.osf` / `steam_loco.osf` → cleanup-mini.

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

The same is needed for any future `FetchContent` integration (pugixml
can be vendored as plain source, so Phase 4 itself does not hit this).

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
- **Commit prefixes:** `feat(cpp|delphi):`, `test(...):`, `fix(...):`,
  `docs(status|changelog|decisions):`, `chore:` for repo-wide changes.
- **New source files use the MIT SPDX header** (see relicense section).
- **Repo CHANGELOG** is Keep-a-Changelog, currently `[0.7.0]`.
  Per-package CHANGELOGs (`implementations/<lang>/CHANGELOG.md`) are
  version-decoupled from the repo line; cpp at `[0.0.3]`. Repo headers
  use an em-dash (`## [X.Y.Z] — DATE`), per-package use a hyphen.
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
2. Read `STATUS.md`, then this file.
3. If continuing the **C++ track**: read DECISIONS.md §20 + §16, run the
   C++ build flow, then start Phase 4 (pugixml vendoring plan first).
4. If a new **Delphi brief** arrives as `~/Downloads/task-*.md`: read it,
   work it, compile-verify with dcc32/dcc64, commit + push.
5. Any new source file: MIT SPDX header, not Apache.
