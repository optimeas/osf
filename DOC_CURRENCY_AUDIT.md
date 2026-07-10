# Repo-wide documentation currency audit

**Date:** 2026-07-10 · **Branch:** `docs-currency-audit` · **Scope:** Phase 1 (read-only).
This report is the deliverable of Phase 1 of `task-doc-currency.md`. **No documentation
was changed** — fixes are Phase 2, gated on an explicit "go".

## Truth sources used (in priority order)
1. Merged code + tests under `implementations/` on `main`.
2. `DECISIONS.md` (§21 in its Java-21 form; the integrity section = `none ⊂ crc ⊂ signed` ladder).
3. Spec docs `docs/{de,en}/references/` (revision 2026-07-07).
4. Zenodo concept paper (DOI 10.5281/zenodo.21227942) for publication/licence context.

**Method note.** The four area subagents (A public docs, B repo-root, C handoff, D automated
checks) were dispatched in parallel but all terminated on a shared API session limit before
returning; the audit below was therefore performed inline against the same sources. Area D was
run as a link/date scanner script. The audit branch sits on top of the open **PR #11** (STATUS
meta-block consolidation), so the three STATUS meta blocks it already fixed are marked
"addressed in PR #11" rather than re-reported.

---

## Summary counts

| Severity | Count | Meaning |
|---|---:|---|
| **P1** public-wrong | 9 | Contradicts reality on a public page (docs.optimeas.com / GitHub landing) |
| **P2** internally-misleading | 5 | Handoff/meta docs that would misdirect a new session/contributor |
| **P3** cosmetic | 4 groups | Stale dates, scanner-flagged anchors, model-version string, missing feature mentions |

Biggest theme: **Java and C++ are shown as "planned / in progress" on every public status
surface, though both are complete** (Java fully merged incl. crc; C++ §20 complete incl. CI +
C ABI + crc). The known `java.md` P1 is confirmed and is the top Phase-2 item.

---

## Area A — Public documentation `docs/{de,en}/`

| File | Line(s) | Ist (current claim) | Soll (per truth source) | Sev |
|---|---|---|---|---|
| `docs/de/implementations/java.md` | 3, 22–24, 37, 39 | "Geplante Java-Implementierung … noch kein Code"; "Status: geplant"; "Geplante Architektur"; "**Java 25**" | Java is **complete + merged**: Java **21**, JPMS module, OSF4+OSF5 read, **both** OSF5 writers, `osf-cli` + `osf-viewer`, integrity level `crc`. (impl/java, DECISIONS §21) | **P1** |
| `docs/en/implementations/java.md` | 3, 22–24, 37, 39 | "Planned Java implementation … no code yet"; "Status: planned"; "Java 25" | same as above | **P1** |
| `docs/de/implementations/index.md` | 44 | Java `📋` — "Architektur entschieden (Java **25**…); noch kein Code" | Java `✅`, Java 21, complete | **P1** |
| `docs/en/implementations/index.md` | 44 | Java `📋` — "Java **25** … no code yet" | Java `✅`, Java 21, complete | **P1** |

**DE↔EN drift:** none found in the Java pages — both are wrong in the same way (consistent). CC-BY
front-matter/footer coverage is **complete (58/58 md files carry a CC BY 4.0 marker)** — no gap.

**Checked and OK / deliberate (no finding):**
- C#/P-Invoke references in `docs/*/implementations/cpp/c-abi.md`, `cpp.md`, `cpp/architecture.md`
  are the **C-ABI consumer** docs (calling `osf-c` from C#/OCX) — deliberately kept; not "planned C#".
- `index.md` "Other languages / Weitere Sprachen — C — planned" is correct (only native C remains planned).
- Feature lists on `index.md`/impl pages do **not** mention the `crc` integrity profile — an
  omission, not a contradiction → see P3.

---

## Area B — Repo-root documents

| File | Line(s) | Ist | Soll | Sev |
|---|---|---|---|---|
| `README.md` | 37 | "the C++ implementation is in active development" | C++ is **§20 complete** (both writers, OSFZ read, CI, `osf-c` C ABI, crc) | **P1** |
| `README.md` | 44 | C++ `🚧` — "Cross-platform CI and a C ABI wrapper are the **remaining milestones**" | Both **shipped**; nothing remaining | **P1** |
| `README.md` | 46 | Java `📋` "Planned; architecture decided" | Java **complete** | **P1** |
| `implementations/README.md` | 9 | C++ "**Planned** … Qt integration" | C++ complete; "Qt integration" is not part of the impl | **P1** |
| `implementations/README.md` | 11 | Java "**Planned**" | Java complete | **P1** |
| `BACKLOG.md` | — | No **ASan CI-leg** note for the pre-existing C-smoke-test use-after-free found during the crc work | Add a short note (missing entry) | **P2** |

**Checked and OK (no finding):**
- `README.md:5,43` osfdata "TestPyPI v0.1.0" badge/row — correct (Python is on TestPyPI, pre-release).
- `README.md` license split (MIT code / CC BY docs), `CONTRIBUTING.md:64–65` CC-BY clause — present + correct.
- `CONTRIBUTORS.md` — **4 entries** (Schranz, Kessel, Langfeld, Klein) as expected.
- `integrations/README.md` + `README.md` integrations table — all "Planned" (correct; none built).
- `BACKLOG.md` — the "C++ C ABI (osf-c) deferred surface" entry is a **legitimate open** item (C
  builder + packaging), not a zombie. No stale "HIGH Rust-writer" entry found in BACKLOG itself
  (the stale pointer to it lives in STATUS — see Area C).
- `CHANGELOG.md [Unreleased]` — carries the manifest + crc entries; broadly current.
- `DECISIONS.md` — no unmarked superseded passages spotted in the areas checked (integrity §, §21).

---

## Area C — Handoff documents

| File | Line(s) | Ist | Soll | Sev |
|---|---|---|---|---|
| `STATUS.md` | 998–999 | "Resuming" block: "the **HIGH-priority Rust-writer item** lives [in BACKLOG]" | The Rust OSF5 writer is **done**; no such HIGH BACKLOG item exists → stale pointer (zombie) | **P2** |
| `CLAUDE.md` | 3 | "Last updated: **2026-06-12**" | Stale — misses the July crc wave, manifest retrofit, channeltype fix, docs sync | **P2** |
| `CLAUDE.md` | 41–43 | "Recent sessions" newest entry = "C++ smartCORE … 2026-06-12" | Missing the whole July arc: integrity `crc` across all 5 impls, channeltype-as-data-shape fix, reference-manifest retrofit, docs-currency/Docusaurus sync | **P2** |
| `CLAUDE.md` | header / "Two active tracks" / Pickup checklist | Frame the C++ track as "§20 complete" with no integrity ladder; no crc/signed mention | Should reflect the integrity profile (crc done, signed next) — **verify + refresh in Phase 2** | **P2** |

**STATUS.md meta blocks (header table, "Next session priorities", follow-up list): already
addressed in open PR #11** — not re-reported here. The top "Public-release prep" note (L15–24)
is historically accurate (Phases 1–4, 2026-06-07) and not misleading.

---

## Area D — Automated checks

### D1 — Internal links / anchors (`docs/`)
Scanner reported **22** unresolved relative/anchor links. **The authoritative check is the
Docusaurus build**, which ran **green** at the last sync (2026-07-10) except the single
`papers/` link that was already fixed (PR #10). So the 22 are almost entirely **scanner
artifacts**, to verify — not fix blindly:
- `osf_general.md` self-anchors `#datentypen`/`#datatypes`, `#kanaltypen`/`#channeltypes`,
  `#sizeoflengthvalue` and the `osf4.md`/`osf5.md` → `osf_general.md#…null-termination…` links:
  these headings use **explicit `{#id}` anchors** (added during an earlier sync) that the simple
  slug scanner does not read → **likely false positives** (the Docusaurus build did not flag them).
- `cpp/cookbook.md`, `cpp/reading.md` "→ `auto` (file missing)": C++ lambda syntax `[](auto)` in
  prose/code matched the markdown-link regex → **false positive**.
- **Recommendation:** trust the Docusaurus `onBrokenLinks/onBrokenAnchors` build as the gate; spot-
  check the `{#id}` anchors in `osf_general.md` during Phase 2. `[dead_links(raw)=22, likely_real≈0]`

### D2 — Stale `last_update.date` vs last content commit
**~20+ pages** declare a `last_update.date` older than their last content commit — a systematic
P3 (the field is not maintained on edit). Representative candidates (not auto-fixed):
`docs/de/osf_general.md` (07-07 < 07-09), `examples/index.md` (05-04 < 07-07),
`implementations/{index,java,cpp,delphi,python,rust,planned}.md`, `implementations/cpp/*.md`
(06-12 < 07-07). Same set applies to the EN tree. **Candidate list only.** `[stale_dates≈20+]`

---

## P3 (cosmetic) — consolidated

1. **Stale `last_update` dates** across ~20+ docs pages (D2) — refresh alongside their Phase-2 content edits.
2. **Scanner-flagged anchors (22)** — verify against explicit `{#id}` headings; almost certainly
   already valid per the green Docusaurus build.
3. `CLAUDE.md:928` Co-Authored-By trailer cites "**Claude Opus 4.7**"; current model is Opus 4.8.
4. Public status/feature surfaces (`README.md`, `docs/*/implementations/index.md`) **omit the
   `crc` integrity profile** from feature lists — completeness gap, not a contradiction.

---

## Phase-2 preview (do NOT start until "go")
1. **All P1** — rewrite `java.md` (DE+EN) from "planned" to the shipped state (Java 21, both
   writers, JPMS, crc, links to STATUS/DECISIONS §21), using the other implementation pages as the
   template; fix the C++/Java rows in `index.md` (DE+EN), `README.md`, `implementations/README.md`.
2. **All P2** — STATUS Resuming-block zombie; refresh CLAUDE.md (Last updated, Recent sessions,
   tracks, pickup checklist); add the two new standing CLAUDE.md conventions from the brief; add the
   BACKLOG ASan-leg note.
3. **P3** — trivial ones in a sweep (model-version string; last_update dates touched during content
   edits); the rest as a BACKLOG note.
4. **Docusaurus sync** of the changed pages (via `docs/scripts/sync-to-docusaurus.py` + the
   sync-PR path; deployment is Matthias-merged).
