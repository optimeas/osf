# Claude Code Session State

Last updated: 2026-05-10 (end of day, see below for hand-off notes).

This file is a hand-off document for the next Claude Code session. Read
[DECISIONS.md](DECISIONS.md) §20 and [STATUS.md](STATUS.md) for the
authoritative project state; this file only captures session-local context
that would otherwise be lost overnight.

## Current focus

C++ implementation at `implementations/cpp/`, working through the eleven-
phase plan in DECISIONS.md §20.

### Completed

| Phase | What | Last commit |
|---|---|---|
| 1 | Skeleton (CMake, foundation Error/Result, GoogleTest) | `08d5b7e` |
| 2a | Magic-header API design (header.hpp with OsfVersion, MagicHeader, 3 overload declarations) | `d14cb54` |
| 2b | Magic-header implementation + 16 unit + 4 integration tests | `7926e9a` |

Plus the cleanup commits for Phase 1 (`0be729b` STATUS, `64a6a26` CHANGELOG 0.4.0).

### Test status

25/25 ctest cases passing locally on Windows (MSVC 19.50, VS 18 generator,
CMake 4.2.3). 0 CMake-configure warnings, 0 compile warnings under `/W4
/permissive-`.

### Pending

Two next-step options — user picks at session start:

1. **Phase 2b cleanup mini-session.** Extend STATUS.md C++ block with the
   magic-header parser stage; add a repo-CHANGELOG entry (likely
   `[0.5.0] — 2026-05-10` or `2026-05-09` covering Phase 2a+2b
   together). No code changes.
2. **Phase 3 — OSF5 JSON metablock parser.** Vendor `nlohmann/json`
   (single-header), implement metablock structs and the parser per spec
   rev 2026-05-04 (DECISIONS §16 set: `pair`/`triple`/`candata` removed,
   `gpsdata` → `gpslocation`, unsigned-int datatypes added).

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
  green locally; Phase 1 disciplined this for the C++ tree).
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
- **Repo CHANGELOG** is versioned Keep-a-Changelog (currently `0.4.0`),
  per-package CHANGELOGs live under `implementations/<lang>/CHANGELOG.md`
  (cpp at `0.0.1`). Decision documented in §20 commit history; see
  also CHANGELOG entry for 0.3.0/0.4.0.

## Reference files for the next session

- `DECISIONS.md` §20 — C++ architecture (read first thing).
- `STATUS.md` "C++ implementation — current state" block — repo-wide
  status with module list.
- `implementations/cpp/CHANGELOG.md` — per-package version history.
- `implementations/rust/osf-core/src/` — Rust reference implementation.
  When a phase has a counterpart in Rust (header, metablock, block reader,
  manager, writer, compression), translate the form, not the code,
  idiomatically into C++.
- `implementations/rust/osf-core/src/header.rs` — was the source of truth
  for Phase 2.

## Pickup checklist for tomorrow

1. `git pull` (in case of any out-of-band edits).
2. Read this file plus DECISIONS.md §20.
3. Run the C++ build flow above to confirm 25/25 still green locally
   before starting any new code.
4. User decides: cleanup-mini or Phase 3.
