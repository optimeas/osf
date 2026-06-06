#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH
"""Sync the OSF docs from this repo's docs/{de,en} tree into the Bitbucket
Docusaurus site's native-i18n layout.

  DE  docs/de/X  ->  <target>/docs/software/data_formats/osf/X
  EN  docs/en/X  ->  <target>/i18n/en/docusaurus-plugin-content-docs/current/software/data_formats/osf/X

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

SRC_DE = Path("docs/de")
SRC_EN = Path("docs/en")
DST_DE = Path("docs/software/data_formats/osf")
DST_EN = Path(
    "i18n/en/docusaurus-plugin-content-docs/current/software/data_formats/osf"
)

# A line that is (optional ws) two regional-indicator glyphs (a flag) followed
# by a markdown link into the sibling ../../en/ or ../../de/ locale tree.
FLAG_LINK_RE = re.compile(
    r"^\s*[\U0001F1E6-\U0001F1FF]{2}\s*\[[^\]]*\]\(\.\./\.\./(?:en|de)/[^)]*\)\s*$"
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

    de = sync_locale(args.repo_root / SRC_DE, args.target / DST_DE, dry_run=args.dry_run)
    en = sync_locale(args.repo_root / SRC_EN, args.target / DST_EN, dry_run=args.dry_run)

    mode = "DRY-RUN" if args.dry_run else "SYNCED"
    print(f"[{mode}] DE: {de['md']} md ({de['removed_md']} removed), {de['media']} media")
    print(f"[{mode}] EN: {en['md']} md ({en['removed_md']} removed), {en['media']} media")
    return 0


if __name__ == "__main__":
    sys.exit(main())
