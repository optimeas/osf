# Claude Code Session State

Last updated: 2026-05-19 (after Phase 3 cleanup).

This file is a hand-off document for the next Claude Code session. Read
[DECISIONS.md](DECISIONS.md) §20 and [STATUS.md](STATUS.md) for the
authoritative project state; this file only captures session-local context
that would otherwise be lost between runs.

## Current focus

C++ implementation at `implementations/cpp/`, working through the eleven-
phase plan in DECISIONS.md §20.

### Completed

| Phase | What | Last commit |
|---|---|---|
| 1 | Skeleton (CMake, foundation Error/Result, GoogleTest) | `08d5b7e` |
| 2a | Magic-header API design (header.hpp with OsfVersion, MagicHeader, 3 overload declarations) | `d14cb54` |
| 2b | Magic-header implementation + 16 unit + 4 integration tests | `7926e9a` |
| 3 | OSF5 JSON metablock parser: nlohmann/json vendored, types.hpp + metablock.hpp + src/types.cpp + src/metablock.cpp, 9 type-parser unit + 20 metablock-parser unit + 3 metablock-integration tests | `152d1ba` |

Cleanup-mini commits:

- Phase 1: `0be729b` STATUS, `64a6a26` CHANGELOG `[0.4.0]`.
- Phase 2b: `eb889a8` STATUS, `5826b5b` CHANGELOG `[0.5.0]` + cpp `[0.0.2]`.
- Phase 3: `36b96a2` STATUS, `11210f8` CHANGELOG `[0.6.0]` + cpp `[0.0.3]`.

### Test status

57/57 ctest cases passing locally on Windows (MSVC 19.50.35717, VS 18
generator, CMake 4.2.3). 0 CMake-configure warnings, 0 compile warnings
under `/W4 /permissive-`. Last verified 2026-05-19 against commit
`152d1ba`.

### Network caveat (local environment)

CMake `FetchContent` over HTTPS fails on this Windows host with
`CRYPT_E_NO_REVOCATION_CHECK` (cannot reach the CRL endpoint).
Workaround used during Phase 3:

```powershell
# one-shot, idempotent — keep the extracted dir around between runs
Invoke-WebRequest `
  -Uri "https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz" `
  -OutFile "$env:TEMP\googletest-v1.15.2.tar.gz" -UseBasicParsing
Push-Location $env:TEMP; New-Item -ItemType Directory -Force gtest-extract |
  Out-Null; Set-Location gtest-extract
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" `
  -E tar xzf "$env:TEMP\googletest-v1.15.2.tar.gz"; Pop-Location
```

Then configure with:

```powershell
cmake -B implementations\cpp\build -S implementations\cpp `
  -D FETCHCONTENT_SOURCE_DIR_GOOGLETEST="$env:TEMP\gtest-extract\googletest-1.15.2"
```

`Invoke-WebRequest` uses the Windows certificate store and succeeds where
both `curl` and CMake's downloader fail. The same workaround will be
needed for any future `FetchContent` integration on this host.

### Pending

**Phase 4 — OSF4 XML metablock parser** is the immediate next step.

- Vendor `pugixml` (single-pair `pugixml.hpp` + `pugixml.cpp` + `pugiconfig.hpp`,
  MIT licence) under `implementations/cpp/third_party/pugixml/` following the
  established Phase-1 vendoring pattern (LICENSE prefixed with two
  provenance lines, version-pinned via tag, sources in a subdirectory
  mirroring the upstream layout). Note that pugixml has one .cpp file
  unlike the prior two header-only vendors, so it needs to compile into
  `osf_core` directly — not via `osf::headers` SYSTEM include.
- Implement `osf::parse_metablock_xml(uint8_t const*, size_t)` overload
  pair against the existing `MetaBlock` data model from Phase 3 — both
  parsers now share `include/osf/metablock.hpp`. Symmetric population
  with the JSON parser is the success criterion (every field one
  populates, the other populates).
- Rust reference: `implementations/rust/osf-core/src/meta_xml.rs`.
- Likely commit shape (similar to Phase 2 / Phase 3): vendor → API
  surface (declaration only, adds one overload to the existing header)
  → implementation → unit tests → integration tests against
  `examples/generated/osf4_*.osf` plus `examples/motorbike.osf` and
  `examples/steam_loco.osf` → cleanup-mini.

## Toolchain notes (Windows)

- `cmake.exe` is **not** on PATH. Full path:
  `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
  (same for `ctest.exe`).
- Generator: `Visual Studio 18 2026` (used by default when invoking via the
  VS-bundled CMake).
- Local C++ build flow:
  ```powershell
  cmake -B implementations\cpp\build -S implementations\cpp
  cmake --build implementations\cpp\build --config Debug
  ctest --test-dir implementations\cpp\build -C Debug --output-on-failure
  ```
  Always remove `implementations\cpp\build` after a successful verify.

## Active conventions to remember

- **Push after every commit** — no batching, no holding back.
- **Verify before push** for code-touching commits (build + ctest must be
  green locally; Phase 1 and 2 disciplined this for the C++ tree).
- **Commit prefixes:** `feat(cpp):` for code, `test(cpp):` for tests,
  `docs(cpp):` for cpp-docs, `docs(status):`, `docs(changelog):`,
  `docs(decisions):` for the repo-wide docs, `chore(cpp):` for
  meta-files like .gitignore.
- **Co-Authored-By trailer** on every commit:
  `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>`
- **Path filter in `.github/workflows/ci.yml`** does **not** cover
  `implementations/cpp/**` yet — that wiring belongs to Phase 10 of §20.
  C++ pushes do not trigger any CI run; verify locally instead.
- **Phase numbers** refer to DECISIONS.md §20 Implementation Order.
- **Repo CHANGELOG** is versioned Keep-a-Changelog (currently `[0.5.0]`),
  per-package CHANGELOGs live under `implementations/<lang>/CHANGELOG.md`
  (cpp at `[0.0.2]`). Repo and per-package version lines are intentionally
  decoupled. Repo CHANGELOG uses em-dash in version headers
  (`## [X.Y.Z] — DATE`); per-package CHANGELOGs use hyphen
  (`## [X.Y.Z] - DATE`). Inconsistency is known; not worth a lone
  style-only commit, would land alongside the next per-package CHANGELOG
  edit if it ever comes up.
- **Vendoring pattern (from Phase 1 tl::expected and reused for Phase 3):**
  tag-pinned URL, SHA256 verified, byte-identical drop except for the
  LICENSE which is renamed and prefixed with two provenance lines:

  ```text
  # Vendored from <upstream-URL-with-tag>
  # Renamed from <upstream-name> to LICENSE for tooling compatibility; content unmodified.
  ```

## Reference files for the next session

- `DECISIONS.md` §20 — C++ architecture (read first thing).
- `DECISIONS.md` §16 — Spec revision 2026-05-04 data-type set
  (relevant for Phase 3).
- `STATUS.md` "C++ implementation — current state" block — repo-wide
  status with module list, currently up-to-date through Phase 2b.
- `implementations/cpp/CHANGELOG.md` — per-package version history.
- `implementations/rust/osf-core/src/` — Rust reference implementation.
  When a phase has a counterpart in Rust (header, metablock, block reader,
  manager, writer, compression), translate the form, not the code,
  idiomatically into C++.
- `implementations/rust/osf-core/src/meta_json.rs` — source of truth
  for Phase 3.
- `docs/de/references/osf5.md` and `docs/en/references/osf5.md` — OSF5
  metablock JSON schema documentation.

## Pickup checklist for the next session

1. `git pull` (in case of any out-of-band edits).
2. Read this file plus DECISIONS.md §20 and §16.
3. Run the C++ build flow above to confirm 25/25 still green locally
   before starting any new code.
4. Start Phase 3 (OSF5 JSON metablock parser) per the Pending section
   above. Likely first stop-point: vendoring plan for `nlohmann/json`.
