#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH
"""Sync the OSF docs from this repo's docs/{de,en,it,fr,es} trees into the
Bitbucket Docusaurus site's native-i18n layout.

  DE  docs/de/X  ->  <target>/docs/software/data_formats/osf/X
  EN  docs/en/X  ->  <target>/i18n/en/docusaurus-plugin-content-docs/current/software/data_formats/osf/X
  IT  docs/it/X  ->  <target>/i18n/it/docusaurus-plugin-content-docs/current/software/data_formats/osf/X
  FR  docs/fr/X  ->  <target>/i18n/fr/docusaurus-plugin-content-docs/current/software/data_formats/osf/X
  ES  docs/es/X  ->  <target>/i18n/es/docusaurus-plugin-content-docs/current/software/data_formats/osf/X

German is the site's default locale; every other locale lives under i18n/.
This repo is the source for all of them: the target's pages are overwritten.

Markdown files have the GitHub-only flag cross-link line stripped. .md files
are clean-replaced; media/ is additive (overwrite same-named assets, never
delete target-only assets).
"""
from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

DEFAULT_LOCALE = "de"
# Locales besides German. A target site that does not have one of the
# OPTIONAL_LOCALES at all (no i18n/<locale> directory) is synced without it.
I18N_LOCALES = ("en", "it", "fr", "es")
OPTIONAL_LOCALES = ("it", "fr", "es")

SUBTREE = Path("software/data_formats/osf")
DST_DE = Path("docs") / SUBTREE


def src_dir(locale: str) -> Path:
    """Source tree of a locale, relative to the repo root."""
    return Path("docs") / locale


def dst_dir(locale: str) -> Path:
    """Target subtree of a locale, relative to the Docusaurus checkout."""
    if locale == DEFAULT_LOCALE:
        return DST_DE
    return Path("i18n") / locale / "docusaurus-plugin-content-docs/current" / SUBTREE


# A line that is (optional ws) two regional-indicator glyphs (a flag) followed
# by a markdown link into a sibling locale tree. The ../ depth varies with the
# page's directory depth (root pages use ../, deeper pages ../../), so match
# one or more ../ segments.
FLAG_LINK_RE = re.compile(
    r"^\s*[\U0001F1E6-\U0001F1FF]{2}\s*\[[^\]]*\]\((?:\.\./)+(?:en|de|it|fr|es)/[^)]*\)\s*$"
)


def strip_flag_link(text: str) -> str:
    """Remove the flag cross-link line plus one immediately following blank line."""
    lines = text.split("\n")
    out: list[str] = []
    i = 0
    while i < len(lines):
        if FLAG_LINK_RE.match(lines[i]):
            i += 1
            if i < len(lines) and lines[i].strip() == "":
                i += 1
            continue
        out.append(lines[i])
        i += 1
    return "\n".join(out)


def iter_files(root: Path):
    """Yield every file under ``root`` as a path relative to ``root``."""
    for p in sorted(root.rglob("*")):
        if p.is_file():
            yield p.relative_to(root)


def sync_locale(src_dir: Path, dst_dir: Path, *, dry_run: bool) -> dict:
    """Sync one locale tree. Clean-replace .md, additive media. Returns a tally."""
    if not dst_dir.is_dir():
        raise SystemExit(
            f"target subtree does not exist: {dst_dir}\n"
            "Refusing to create a new category tree (see AGENTS.md)."
        )
    tally = {"md": 0, "media": 0, "removed_md": 0}

    # 1. Clean-replace: drop existing .md under the target subtree.
    for p in sorted(dst_dir.rglob("*.md")):
        if not dry_run:
            p.unlink()
        tally["removed_md"] += 1

    # 2. Copy source files. .md -> flag-stripped; everything else -> additive.
    for rel in iter_files(src_dir):
        src = src_dir / rel
        dst = dst_dir / rel
        if src.suffix == ".md":
            content = strip_flag_link(src.read_text(encoding="utf-8"))
            if not dry_run:
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_text(content, encoding="utf-8")
            tally["md"] += 1
        else:
            if not dry_run:
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
            tally["media"] += 1
    return tally


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Sync OSF docs into the Docusaurus site.")
    ap.add_argument("target", type=Path, help="root of the optimeas-documentation checkout")
    ap.add_argument("--dry-run", action="store_true", help="print actions, write nothing")
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="root of the osf GitHub repo (default: inferred from this script)",
    )
    args = ap.parse_args(argv)

    if not args.target.is_dir():
        raise SystemExit(f"target is not a directory: {args.target}")

    mode = "DRY-RUN" if args.dry_run else "SYNCED"
    for locale in (DEFAULT_LOCALE, *I18N_LOCALES):
        if locale in OPTIONAL_LOCALES and not (args.target / "i18n" / locale).is_dir():
            print(f"[SKIPPED] {locale.upper()}: the target site has no i18n/{locale}")
            continue
        t = sync_locale(
            args.repo_root / src_dir(locale), args.target / dst_dir(locale), dry_run=args.dry_run
        )
        print(
            f"[{mode}] {locale.upper()}: {t['md']} md ({t['removed_md']} removed), "
            f"{t['media']} media"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
