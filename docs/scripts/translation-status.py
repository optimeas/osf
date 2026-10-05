#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH
"""Tell which translated doc pages need work.

docs/it and docs/fr are translations of docs/de. A translation cannot be
diffed against its source, so docs/translation-state.json records, per
translated page, the hash of the German page it was made from. This tool
compares that record with the trees on disk:

  stale       the German page changed since the translation was made
  missing     German page without a translation
  orphaned    translation without a German page
  structure   headings, code blocks or front matter no longer line up
  media       a non-markdown file of docs/de is absent

  translation-status.py                 report; exit 1 if anything is open
  translation-status.py --record        after translating: record the current
                                        German hashes (all pages, or the given
                                        ones, relative to docs/de)

The workflow and the translation rules are in docs/TRANSLATING.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

SOURCE = "de"
LOCALES = ("it", "fr")
STATE_FILE = Path("docs/translation-state.json")

_FENCE_RE = re.compile(r"^\s*(```+|~~~+)(.*)$")
_HEADING_RE = re.compile(r"^(#{1,6})\s")
_FRONT_MATTER_RE = re.compile(r"\A---\n(.*?)\n---\n", re.S)


def page_hash(path: Path) -> str:
    """Hash of a page's text, independent of the checkout's line endings."""
    return hashlib.sha256(path.read_text(encoding="utf-8").encode("utf-8")).hexdigest()


def structure(text: str) -> dict:
    """The parts of a page a translation must leave exactly as they are."""
    keys: list[str] = []
    fm = _FRONT_MATTER_RE.match(text)
    if fm:
        keys = [ln.split(":")[0] for ln in fm.group(1).split("\n") if re.match(r"[A-Za-z_]", ln)]
        text = text[fm.end():]
    headings: list[int] = []
    blocks: list[tuple[str, str]] = []
    fence = None  # (char, length, info)
    body: list[str] = []
    for line in text.split("\n"):
        m = _FENCE_RE.match(line)
        if fence:
            if m and m.group(1)[0] == fence[0] and len(m.group(1)) >= fence[1] and not m.group(2).strip():
                # Diagram labels are translated, so only their presence counts.
                content = "" if fence[2].startswith("mermaid") else "\n".join(body)
                blocks.append((fence[2], content))
                fence, body = None, []
            else:
                body.append(line)
            continue
        if m:
            fence = (m.group(1)[0], len(m.group(1)), m.group(2).strip())
            continue
        h = _HEADING_RE.match(line)
        if h:
            headings.append(len(h.group(1)))
    return {"front_matter": keys, "headings": headings, "code": blocks}


def structure_issues(source_text: str, translated_text: str) -> list[str]:
    s, t = structure(source_text), structure(translated_text)
    issues = []
    if s["front_matter"] != t["front_matter"]:
        issues.append("front matter keys differ")
    if s["headings"] != t["headings"]:
        issues.append(f"headings: {len(s['headings'])} in German, {len(t['headings'])} here, or levels differ")
    if len(s["code"]) != len(t["code"]):
        issues.append(f"code blocks: {len(s['code'])} in German, {len(t['code'])} here")
    else:
        for i, (a, b) in enumerate(zip(s["code"], t["code"]), 1):
            if a != b:
                issues.append(f"code block {i} differs from the German page")
    return issues


def _pages(root: Path) -> set[str]:
    return {p.relative_to(root).as_posix() for p in root.rglob("*.md")}


def _other_files(root: Path) -> set[str]:
    return {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file() and p.suffix != ".md"}


def load_state(repo_root: Path) -> dict:
    path = repo_root / STATE_FILE
    if not path.is_file():
        return {"source": SOURCE, "pages": {}}
    return json.loads(path.read_text(encoding="utf-8"))


def save_state(repo_root: Path, state: dict) -> None:
    text = json.dumps(state, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    (repo_root / STATE_FILE).write_text(text, encoding="utf-8", newline="\n")


def status(repo_root: Path, locales=LOCALES) -> dict:
    """Open work per locale: {locale: {kind: [page or 'page: detail', ...]}}."""
    src_root = repo_root / "docs" / SOURCE
    recorded = load_state(repo_root)["pages"]
    source_pages = _pages(src_root)
    hashes = {rel: page_hash(src_root / rel) for rel in source_pages}
    result = {}
    for locale in locales:
        root = repo_root / "docs" / locale
        pages = _pages(root) if root.is_dir() else set()
        known = recorded.get(locale, {})
        open_work = {
            "missing": sorted(source_pages - pages),
            "orphaned": sorted(pages - source_pages),
            "stale": sorted(rel for rel in source_pages & pages if known.get(rel) != hashes[rel]),
            "structure": [],
            "media": sorted(_other_files(src_root) - (_other_files(root) if root.is_dir() else set())),
        }
        for rel in sorted(source_pages & pages):
            for issue in structure_issues(
                (src_root / rel).read_text(encoding="utf-8"), (root / rel).read_text(encoding="utf-8")
            ):
                open_work["structure"].append(f"{rel}: {issue}")
        result[locale] = open_work
    return result


def record(repo_root: Path, locales=LOCALES, only: list[str] | None = None) -> int:
    """Record the current German hash for every translated page (or ``only``)."""
    src_root = repo_root / "docs" / SOURCE
    state = load_state(repo_root)
    source_pages = _pages(src_root)
    count = 0
    for locale in locales:
        root = repo_root / "docs" / locale
        known = state["pages"].setdefault(locale, {})
        for rel in list(known):
            if rel not in source_pages:
                del known[rel]
        for rel in sorted(only if only else source_pages):
            if rel not in source_pages:
                raise SystemExit(f"not a page of docs/{SOURCE}: {rel}")
            if (root / rel).is_file():
                known[rel] = page_hash(src_root / rel)
                count += 1
    save_state(repo_root, state)
    return count


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Report translated doc pages that need work.")
    ap.add_argument("pages", nargs="*", help="with --record: pages relative to docs/de (default: all)")
    ap.add_argument("--record", action="store_true", help="record the current German hashes")
    ap.add_argument("--locale", action="append", choices=LOCALES, help="limit to a locale (repeatable)")
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="root of the osf repo (default: inferred from this script)",
    )
    args = ap.parse_args(argv)
    locales = tuple(args.locale) if args.locale else LOCALES

    if args.record:
        count = record(args.repo_root, locales, args.pages)
        print(f"recorded {count} page(s) in {STATE_FILE.as_posix()}")
        return 0
    if args.pages:
        ap.error("pages are only accepted together with --record")

    total = 0
    for locale, open_work in status(args.repo_root, locales).items():
        n = sum(len(v) for v in open_work.values())
        total += n
        print(f"{locale}: {'up to date' if n == 0 else f'{n} open'}")
        for kind, items in open_work.items():
            for item in items:
                print(f"  {kind:<10}{item}")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
