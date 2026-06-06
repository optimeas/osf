# Public Release Preparation

Tracking the steps to make the OSF repo public on GitHub. Owner:
Burkhard. Initiated 2026-06-03.

**Consolidated onto `main` 2026-06-04.** The `prep-public-release`
branch + the separate `osf-docs` worktree existed only to isolate this
work from the parallel C++/Java tracks. Those tracks have settled (C++
§20 complete on `main`; no Java track active), so the branch was
fast-forwarded into `main` and removed. This file is **kept on `main`**
(not removed at merge, as originally planned) — it is the living tracker
for the public-release prep. Phases 1, 3 and 4 (incl. the history purge)
are done; Phase 2 is synced to a Docusaurus PR branch (owner merge
pending). Remaining work continues directly on `main`.

> **⚠ CORRECTION 2026-06-04:** the repo is ALREADY public — and has been
> since creation (2026-05-03). The "the repo-going-public is the last,
> manual step" framing this section originally carried was mistaken.
> Consequence: the unreleased Train field data (in history since
> 2026-05-19) and the `docs/superpowers/` artefacts were publicly
> reachable via `git clone` history the whole time. The history purge
> (Phase 4) was therefore **executed immediately** rather than deferred
> behind Phases 2–3. **Done 2026-06-04:** `git-filter-repo` on a fresh
> mirror (`--path "examples/Testdata Train OSFZ" --path docs/superpowers
> --invert-paths`) + `git push --force --mirror`; new tip `1cd30c4`, tags
> `v0.1.0`/`v0.2.0`/`v0.10.0` rewritten; a fresh clone now pulls 0 train +
> 0 superpowers objects, tip tree byte-identical, authorized data intact.
>
> **No GitHub Support follow-up needed.** The data owner (Burkhard)
> reassessed the Train data as low-sensitivity: a third party can do
> nothing with it and incidental exposure is uncritical, including towards
> the customer — the only requirement is that it not stay **published
> long-term**, which the purge already satisfies (no fresh clone carries
> it). The remaining unreachable objects are left to GitHub's periodic
> `gc`; with 0 forks there is no fork-network retention. The earlier
> "ask Support to drop cached views" step is dropped.
>
> **No second worktree to re-sync on this machine:** `V:\` is a `subst`
> alias for `C:\Users\Public\Documents\Develop`, so `v:\github\osf` and
> the `C:\Users\Public\...\osf` "C++ worktree" are the same directory
> (canonical path `C:/Users/Public/Documents/Develop/github/osf`), already
> at the rewritten tip. Only a clone on *another* machine would need
> `git fetch --force --tags && git reset --hard origin/main` before its
> next push.

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
  - [x] Fixed two stale READMEs ahead of the flip (would otherwise be
        a poor public first impression): `implementations/cpp/README.md`
        (badge "phase 1: skeleton" → status/tests + accurate
        done/pending list) and `examples/README.md` (claimed "files not
        added yet" + a nonexistent `field/` dir → real generated/ +
        field-sample inventory; verified each sample's actual format via
        magic header — all the bundled `.osf`/`.osfz` are OSF4).

- [x] **Pre-public audit.**
  - [x] `git log --all` scan for customer names, internal hostnames,
        Optimeas-internal paths in commit messages — clean (only
        Burkhard + GitHub as authors; "internal" only as a tech term;
        one config-path doc with a `<u>` placeholder)
  - [x] `git grep` across HEAD for the same in source/docs — clean
        (only external emails are Claude's Co-Author trailer + the
        vendored third-party copyright holders, which stay)
  - [x] `.claude/`, `.vscode/`, `.idea/` — gitignored (`.claude/` +
        `.idea/` already; `.vscode/` added this pass; none tracked)
  - [x] Field-data samples under `examples/` — **Decision:** Testdata
        Train OSFZ (348 files) is NOT cleared for publication (foreign
        data). Removed from the working tree this pass; **MUST also be
        purged from git history before the flip** (see Phase 4). The
        rest stays: Optimeas owns motorbike.osf / steam_loco.osf /
        weather_station.osfz / `Testdata Motorbike/` / `generated/`.
  - [x] No `TODO: remove before publishing` markers left (FIXME/HACK
        hits were prose / binary false-positives)
  - [x] License headers on every source file (MIT SPDX per relicense
        2026-05-20) — added to the 12 files that lacked them (6 .dpr +
        6 .py); third-party vendored code keeps upstream license
  - [x] `docs/superpowers/plans/`, `docs/superpowers/specs/` — internal
        planning artifacts. **Decision:** `.gitignore` + `git rm
        --cached` (files stay on disk for ongoing C++/Java superpowers
        work, untracked so they never reach the public repo).
        **Updated 2026-06-04:** the dir is now backed by a SEPARATE
        PRIVATE repo (intentionally unnamed/unlinked here — see below),
        cloned into the gitignored `docs/superpowers/` path as an
        independent clone (NOT a submodule — no `.gitmodules` pointer in
        the public repo), so plans/specs sync across machines + colleagues
        with full history while staying invisible publicly. The
        previously-committed copies ARE purged from history in the
        `git-filter-repo` run (same pass as the Train data — see Phase 4),
        superseding the earlier "leave in history, low-sensitivity" call.
        Dangling refs in CHANGELOG.md + DECISIONS.md reworded. Docusaurus
        copy step must exclude `superpowers/` (noted in `.gitignore`).
        **Discovery is deliberately out-of-band:** per the 2026-06-04
        decision the public repo carries NO name, URL, or bootstrap
        pointer to the private planning repo — colleagues learn the clone
        step from team onboarding + the private repo's own README, not
        from anything in this public tree.

## Phase 2 — Docusaurus integration prep

Target: external Bitbucket-hosted Docusaurus repo `optimeas-documentation`
(deploys to docs.optimeas.com), locally at `V:\bitbucket\optimeas-documentation`.

**DONE 2026-06-06 — synced to a PR branch; owner review/merge pending.**
Design + plan: `docs/superpowers/{specs,plans}/2026-06-06-osf-docs-docusaurus-sync*`.

- [x] **Layout audit.** Audited `docs/{de,en}/` against the live target:
  - sidebar ordering: the OSF subtree uses `index.md` + `sidebar_position`
    (no `_category_.json` inside `osf/`; only `data_formats/_category_.json`
    above it) — our pages already match.
  - frontmatter: matches the target OSF-subtree convention
    (`title`/`description`/`sidebar_position`/`image: "/img/om_social_card.png"`/
    `keywords`/`last_update.author: Optimeas GmbH`).
  - internal links: already relative `.md` — Docusaurus-native and portable;
    resolve within each locale tree after the move.
  - asset paths: `media/` references survive (media copied alongside).
  - **i18n strategy — DECIDED (dictated by the target):** keep the target's
    **native Docusaurus i18n** with **German as default** —
    `defaultLocale: "de"`, `locales: ["de","en"]`. DE → `docs/…/osf`,
    EN → `i18n/en/docusaurus-plugin-content-docs/current/…/osf`. The GitHub
    `docs/{de,en}` sibling layout stays the authoring source; the sync maps it
    in. Manual `🇩🇪`/`🇬🇧` flag cross-links are GitHub-only and stripped on
    sync (the locale switcher replaces them).
- [x] **Cross-repo sync mechanism.** Reusable script
      `docs/scripts/sync-to-docusaurus.py` (+ 9 unittest cases): path mapping,
      flag-link strip at any `../` depth, clean-replace `.md`, additive
      `media/`. `--dry-run` + tally. Delivery is a feature branch + PR (never
      direct to `main`, which auto-deploys). Submodule/pipeline-pull rejected
      as fragile (cross-host auth + the path/i18n/flag transform).
- [x] **Docusaurus-readiness check.** The local PoC build was blocked by this
      host's Windows TLS issue (`UNABLE_TO_VERIFY_LEAF_SIGNATURE` on npm
      fetches — same cert problem as the C++ FetchContent caveat), so readiness
      was verified offline instead (62 internal links resolve, 0 broken;
      40/40 frontmatter blocks valid; 0 flag remnants) and the **authoritative
      build is the Bitbucket PR pipeline** (`node:20`). Synced to branch
      `osf-docs-sync-phase3` (target commit `a79a9a1`, 44 files); **owner
      opens the PR → pipeline builds → review + merge → SFTP deploy.**

## Phase 3 — Per-implementation + examples documentation

**Decisions taken 2026-06-04 (next session implements these, DE first):**

- **New `docs/{de,en}/implementations/` section** for the per-language
  pages (NOT folded into the existing `integrations/` section — keep
  `integrations/` for the ecosystem/binding angle). Follow the existing
  frontmatter convention (`title`/`description`/`sidebar_position`/
  `image`/`keywords`/`last_update`) and the bilingual cross-link line.
- **Language order, DE first then EN** (owner is German-speaking): write
  each German page first, then mirror to English.
- **Lean placeholders:** Java gets its own skeleton page; C / C# /
  MATLAB / JavaScript / Swift share **one** combined "planned
  implementations" page; the ecosystem integrations (Arrow / PyTorch /
  TensorFlow / MCP / LangChain) get **one** short "planned" page — no
  per-item empty stubs.

**DONE 2026-06-06 — Phase 3 complete (DE + EN).** DE pages were reviewed and
approved by Burkhard; the EN mirror landed (commit `fd130d5`) with the same
structure, frontmatter and section ordering (implementations `sidebar_position:
3`; `integrations`→4, `tools`→5). The DE review also caught one fix, folded
into the same commit: `implementations/rust.md` `sidebar_position` 4 → 3 (was
tied with `python.md`, breaking the Rust-before-Python order). The optional
follow-up (3) — refreshing the stale `implementations/cpp/README.md` — was
done separately (commit `4d7cddc`): badge in-development → complete, "CI in
progress / C-ABI pending" replaced by "all phases complete + CI on
Linux/macOS/Windows" and the shipped `osf-c` C ABI.

- [x] **Per-implementation pages** (`implementations/`) — **DE + EN done.**
      Install / quickstart / API tour / link to source.
  - [x] `index.md` — status table (available vs. planned) + legend
  - [x] Delphi — full (library + demos + osftool CLI)
  - [x] Rust (`osf-core`) — full
  - [x] Python (`osfdata`) — full (cross-link the existing
        `integrations/python.md`, don't duplicate it)
  - [x] C++ — full (§20 complete: reader/DataManager/writers/throwing/
        C-ABI; honest "all phases done")
  - [x] Java — skeleton (planned; architecture per DECISIONS §21; no
        code yet)
  - [x] `planned.md` — combined C / C# / MATLAB / JavaScript / Swift
- [x] **Examples documentation** (**DE + EN done**) — expanded the stub
      `docs/{de,en}/examples/osf_file_examples.md` (was a
      "working on it" placeholder). The 17 generated reference files
      under `examples/generated/` (8× OSF4, 9× OSF5) as a structured
      table — what each demonstrates, how to read it, what code produced
      it (`OSFGeneratorCLI`). Plus the field samples (`motorbike.osf`,
      `steam_loco.osf`, `weather_station.osfz` — all OSF4 per the audit).
- [x] **Ecosystem integrations** (Arrow, PyTorch, TensorFlow, MCP,
      LangChain) — one combined "planned" page signalling intent.
      (**DE + EN done** as `integrations/planned.md`.)

## Phase 4 — Flip to public

Manual on GitHub. Pre-checks:

- [x] **DONE 2026-06-04 — purged unreleased field data from git history.**
      (Outcome summary in the ⚠ CORRECTION note at the top of this file.)
      `examples/Testdata Train OSFZ/` (348 files) had been removed from the
      working tree but was still retrievable from history until rewritten. **The blobs are
      reachable from BOTH `main` and `prep-public-release` (357 objects
      each)** — `main` never removed them from its tree at all (only
      `prep` did, in `41ac7da`), so the rewrite must cover both branches.
      The procedure below was **dry-run-validated 2026-06-04** (see the
      hand-off note) — it removes exactly the 348 train files and nothing
      else, shrinking the pack 157 → 95 MiB and leaving the `prep` tip
      tree byte-identical. **The live run additionally removes
      `docs/superpowers/` (35 objects) per the 2026-06-04 decision —
      beyond the train-only dry-run scope, but the same mechanism;
      `docs/superpowers/` is already absent from the tip tree (`git rm
      --cached`), so the deliverable tree stays unchanged.**

      Tooling: `git-filter-repo` (the doc-recommended tool). No system
      Python on this host — use the standalone single-file script with a
      venv interpreter (no install, no pollution):

      ```powershell
      # one-time: fetch the standalone script (Windows cert store works)
      $py = "C:\Users\bus\Documents\Claude\dev-setup\test-python\.venv\Scripts\python.exe"
      $fr = "$env:TEMP\git-filter-repo.py"
      Invoke-WebRequest -UseBasicParsing `
        -Uri "https://raw.githubusercontent.com/newren/git-filter-repo/v2.47.0/git-filter-repo" `
        -OutFile $fr

      # run on a FRESH mirror clone, never the live working repo
      git clone --mirror <repo> "$env:TEMP\osf-flip.git"
      Push-Location "$env:TEMP\osf-flip.git"
      & $py $fr --path "examples/Testdata Train OSFZ" --path "docs/superpowers" --invert-paths
      # verify (must print 0 for BOTH):
      (git rev-list --all --objects | Select-String "Testdata Train OSFZ").Count
      (git rev-list --all --objects | Select-String "docs/superpowers").Count
      # then push the rewritten refs back:
      git push --force --mirror https://github.com/optimeas/osf.git
      Pop-Location
      ```

      **Sequencing — superseded.** This was originally planned as one of
      the LAST steps before the flip. Once the repo turned out to be
      *already public* (see the ⚠ CORRECTION note), the purge was no longer
      something to defer behind Phases 2–3 — it was run immediately on
      2026-06-04. The "re-sync the parallel C++ worktree afterwards"
      concern was moot on this host: `V:\` is a `subst` alias for
      `C:\Users\Public\Documents\Develop`, so that worktree path and
      `v:\github\osf` are the same directory, already at the rewritten tip.
      Only clones on *other* machines need a `git reset --hard origin/main`
      before their next push.

      **Data-sensitivity reassessment (owner, 2026-06-04):** the Train data
      is low-sensitivity — a third party can do nothing with it, incidental
      exposure is uncritical (incl. towards the customer); only long-term
      publication had to be avoided, which the purge satisfies. → No GitHub
      Support cache-purge requested; GitHub's periodic `gc` handles the
      remaining unreachable objects (0 forks, so no fork-network retention).
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

### 2026-06-03 — Phase 1 complete (pause)

**Status: Phase 1 (Repo cleanup) is done.** Working tree clean,
everything pushed to `origin/prep-public-release`. Six commits this
session (`408ce5c`..`cdd6978`):

1. `408ce5c` — dropped MicroPython placeholder + swept all refs
2. `e096aa1` — root README implementation table refresh (status legend,
   quickstart pointer)
3. `38de872` — untracked `docs/superpowers/` (`.gitignore` +
   `git rm --cached`; files stay on disk; also gitignored `.vscode/`)
4. `202eb6d` — MIT SPDX headers on the 12 files that lacked them
   (6 `.dpr` + 6 `.py`)
5. `41ac7da` — removed `examples/Testdata Train OSFZ/` (348 files) from
   the working tree
6. `cdd6978` — refreshed stale C++ + examples READMEs (+ root README
   C++ row)

All Phase-1 checkboxes above are ticked. Audit came back clean (no
customer names / IPs / hostnames in history or source).

**Two hard blockers before the public flip (Phase 4) — NOT yet done:**

- **⚠ Train-data history purge.** `Testdata Train OSFZ` is foreign data
  NOT cleared for publication. It is gone from the working tree but
  still in git history → would be retrievable in the public repo. Must
  run `git filter-repo --path "examples/Testdata Train OSFZ"
  --invert-paths` + force-push **after** the parallel C++/Java tracks
  settle and merge (the rewrite changes every later SHA). This is the
  gating item — *do not flip to public before it is done.*
- **Sequencing.** Because of the above, "publish right after the audit"
  is not possible; the purge (and the C++/Java track merges) come first.

**Parallel-track note (discovered this session):** the C++ track has
advanced past the CLAUDE.md snapshot — `phase-10-ci` branch has Phases
8 (OSFZ read) + 9 (throwing layer) merged and Phase 10 (CI) in
progress. The refreshed C++ README describes that public end-state.
Their `cpp/README.md` on `phase-10-ci` is still the stale "phase 1
skeleton" — when their work merges, prefer this branch's version (no
conflict expected; they didn't touch it).

**Next session — pick up with EITHER:** *(superseded by the 2026-06-04
note below — the C++ track has now fully settled on `main`.)*

- **Phase 2** (Docusaurus integration prep — layout/frontmatter audit,
  i18n decision), or
- **Phase 3** (per-implementation + examples documentation — user's
  stated next priority: document the existing implementations with
  links to the runnable examples, then cross-link docs ↔ repo), or
- wait for the C++/Java tracks to settle, then do the Train history
  purge + flip prep.

Decision pending from the user on which of these to start with.

### 2026-06-04 — main synced in; Train-purge dry-run validated

**C++ track fully settled.** All of C++ §20 (phases 1–11) is merged to
`main` (`0f51096`, 305/305 ctest, CI-green); no Java track is active.
`main` was merged into `prep-public-release` this session (merge commit,
one docs conflict in STATUS.md resolved toward main's "§20 complete"
state, plus two MicroPython residuals from main swept). The branch now
carries the whole finished C++ surface.

**Train-data history purge — tooling chosen + dry-run-validated (NOT yet
executed live).** The exact, reproducible procedure is now in the Phase 4
checklist above. Key findings from the dry run (on a throwaway mirror
clone, `git-filter-repo v2.47.0`):

- removes exactly the **348** `Testdata Train OSFZ` files across all
  history; **0** non-train paths touched, **0** train objects left;
- `prep` tip tree byte-identical before/after (`9d9d95f…`) — zero impact
  on the deliverable;
- pack shrinks **157 → 95 MiB** (the remaining 95 MiB is the *authorized*
  Motorbike / steam_loco / weather_station / generated data);
- 2 train-only commits become empty and are pruned on each branch.
- **Both `main` and `prep` carry the train blobs** (357 objects each) —
  the live rewrite must cover both. `main` still has the files *in its
  tip tree* (only `prep` removed them); the merge of `prep`→`main` will
  carry that removal, and the purge scrubs the history regardless.

**Still NOT done (the live purge is deliberately deferred):** the
irreversible `--force --mirror` push is one of the *last* steps before
the flip. It must wait until Phases 2–3 are done and `prep` is merged to
`main`, and the C++ worktree at `C:\Users\Public\Documents\Develop\github\osf`
(has `main` checked out) is re-synced afterwards. Phases 2 and 3 remain
the next substantive work.

### 2026-06-06 — main re-synced after the purge; Phase 3 closed (DE + EN)

Picked up on a *different* machine whose local `main` still held the
pre-purge history (293 local-only commits, 0 merge-base with the rewritten
`origin/main`). Re-synced as the PUBLIC-PREP note above anticipated for
"other machines": backed the old tip up to `backup/pre-purge-main-2470bf5`,
then `git reset --hard origin/main`. The untracked `examples/Testdata Train
OSFZ/` still sits on that disk (left as-is, not re-added).

**Phase 3 finished and pushed.** The EN mirror of the DE Phase-3 docs landed
(`fd130d5`) — `docs/en/implementations/{index,delphi,rust,python,cpp,java,
planned}.md`, the expanded `examples/osf_file_examples.md`, the new
`integrations/planned.md`, the reworked `integrations/index.md`, and the
section re-ordering (integrations 3→4, tools 4→5 on the EN side). One DE
review fix folded in (`rust.md` sidebar_position 4→3). The stale
`implementations/cpp/README.md` was refreshed to the §20-complete state
(`4d7cddc`). DE↔EN parity, internal links and cross-links all verified.

**Next substantive work is Phase 2** (Docusaurus integration prep): the
layout/frontmatter audit and the still-open **i18n decision** (keep the
current `docs/de` + `docs/en` sibling-tree layout vs. restructure to the
Docusaurus native `i18n/` convention), plus the cross-repo sync mechanism to
the Bitbucket-hosted Docusaurus site.
