# Public Release Preparation

Tracking the steps to make the OSF repo public on GitHub. Branch:
`prep-public-release`. Owner: Burkhard. Initiated 2026-06-03.

This file is intentionally lightweight — it lives only on this branch
and is removed at merge. The repo-going-public itself is the last,
manual step (GitHub Settings → Visibility → Public).

Parallel tracks that **must not be disturbed** while this work runs:

- `phase-7d-stale-value-guard` (main checkout at
  `C:\Users\Public\Documents\Develop\github\osf`) — active C++ work.
- Java track — just started elsewhere via `session_snapshot`.

This branch's worktree is `C:\Users\Public\Documents\Develop\github\osf-docs`
to keep all three isolated.

---

## Phase 1 — Repo cleanup (do first, mergeable independently)

- [x] **Remove the MicroPython implementation.**
  - [x] `implementations/micropython/` (placeholder dir, no code yet)
  - [x] Row in `README.md` "Implementations" table
  - [x] Any reference in `DECISIONS.md` priority order (also §6 impl
        table + §7 streaming/block platform table; items renumbered)
  - [x] Any mention in `STATUS.md` / `BACKLOG.md` / `CHANGELOG.md`
        (STATUS list + new CHANGELOG `Removed` entry; BACKLOG had none;
        also `implementations/swift/README.md` cross-reference)
  - [x] Spec / docs sweeps (`docs/{en,de}/...`) — no matches
  - [x] Commit: `chore: drop MicroPython implementation placeholder`

- [x] **README.md refresh.**
  - [x] Implementation status table reflects reality (C++ now 🚧 in
        active development; Java 📋 planned/architecture-decided; added
        a ✅/🚧/📋 legend)
  - [x] "Why OSF?" intact, but tone-check for public-facing audience
        (reads fine for public; left as-is)
  - [x] Add quickstart pointers (which implementation to try first —
        new "Which one should I try first?" note + anchor to Quick Start)
  - [x] Badges still valid post-public-flip (License + osfdata are
        static shields.io badges; CI badge renders once public —
        `github.com/optimeas/osf` is the canonical URL)
  - Note: the **per-implementation** C++ README badge still says
    "phase 1: skeleton" — stale, but per-impl READMEs are Phase 3.
    Should be fixed before the public flip regardless (flagged).

- [ ] **Pre-public audit.**
  - [ ] `git log --all` scan for customer names, internal hostnames,
        Optimeas-internal paths in commit messages
  - [ ] `git grep` across HEAD for the same in source/docs
  - [ ] `.claude/`, `.vscode/`, `.idea/` — gitignored already? double-check
  - [ ] Field-data samples under `examples/` — confirm Optimeas owns
        the data and it can go public (`examples/Testdata Train OSFZ/`
        is 346 files of real recordings — explicit OK needed)
  - [ ] No `TODO: remove before publishing` markers left
  - [ ] License headers on every source file (MIT SPDX per relicense
        2026-05-20); third-party vendored code keeps upstream license
  - [ ] `docs/superpowers/plans/`, `docs/superpowers/specs/` — these
        are internal planning artifacts. Decide: keep, move out of
        `docs/`, or remove from history.

## Phase 2 — Docusaurus integration prep

Target: external Bitbucket-hosted Docusaurus repo. The Markdown sources
here must be **drop-in copyable** into that site's `docs/` tree.

- [ ] **Layout audit.** Current `docs/{de,en}/` structure vs.
      Docusaurus conventions:
  - sidebar item ordering (Docusaurus uses `sidebar_position` or
    `_category_.json` files — pick one)
  - frontmatter (`id`, `title`, `slug`, `sidebar_label`,
    `sidebar_position`) — currently missing from most files
  - internal links — relative `[x](osf4.md)` vs. Docusaurus
    `[x](./osf4)` or `[x](/docs/references/osf4)`
  - asset paths — `media/` references survive the copy?
  - i18n strategy — Docusaurus has native i18n (`i18n/de/`,
    `i18n/en/`); current layout puts language in path
    (`docs/de/`, `docs/en/`). Decide: keep current layout (treat as
    two sibling doc trees in Docusaurus) vs. restructure to
    Docusaurus i18n convention. **Decision pending.**
- [ ] **Cross-repo sync mechanism.** Manual copy on each release vs.
      Bitbucket pipeline pulls from GitHub vs. submodule. Out of
      scope for the source layout, but the layout choice should not
      block any of these.
- [ ] **Docusaurus-readiness PoC.** Drop the current `docs/` into a
      local minimal Docusaurus install, see what breaks, fix at the
      source.

## Phase 3 — Per-implementation + examples documentation

- [ ] **Per-implementation pages.** For each language with code:
      install / quickstart / API tour / link to source. Skeleton
      pages only for planned languages.
  - [ ] Delphi
  - [ ] Rust (`osf-core`)
  - [ ] Python (`osfdata`)
  - [ ] C++ (current state; honest about Phase 7c done / 7d–11 pending)
  - [ ] Java (status: just started)
- [ ] **Examples documentation.** The 17 generated reference files
      under `examples/generated/` deserve a structured doc page —
      what each file demonstrates, how to read it, what code produced
      it. Plus the field samples (`motorbike.osf`, `steam_loco.osf`,
      `weather_station.osfz`).
- [ ] **Integrations docs** (Arrow, PyTorch, TensorFlow, MCP,
      LangChain) — even placeholder pages signal intent.

## Phase 4 — Flip to public

Manual on GitHub. Pre-checks:

- [ ] All previous phases merged to `main`
- [ ] CI green on `main`
- [ ] CHANGELOG version-bumped + dated
- [ ] Tag cut (e.g. `v0.11.0-public`)
- [ ] Repo description + topics set on GitHub
- [ ] Branch protection rules reviewed (will apply to public PRs too)
- [ ] Issue templates? Contributor guidelines (`CONTRIBUTING.md` already
      exists — re-read for public-facing tone)

---

## Session hand-off notes

When pausing mid-phase: leave a `wip(...)` commit on this branch or
note the in-progress checklist item in this file. The next session
picks up by reading this file from the worktree's `PUBLIC-PREP.md`.
