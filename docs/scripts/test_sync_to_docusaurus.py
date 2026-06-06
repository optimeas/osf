# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH
"""Tests for docs/scripts/sync-to-docusaurus.py."""
import importlib.util
import shutil
import tempfile
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_SPEC = importlib.util.spec_from_file_location(
    "sync_to_docusaurus", _HERE / "sync-to-docusaurus.py"
)
sync = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(sync)


class StripFlagLinkTests(unittest.TestCase):
    def test_strips_de_page_english_flag_and_blank(self):
        text = (
            "---\ntitle: X\n---\n\n"
            "🇬🇧 [English version](../../en/implementations/index.md)\n\n"
            "## Heading\nbody\n"
        )
        out = sync.strip_flag_link(text)
        self.assertNotIn("🇬🇧", out)
        self.assertNotIn("English version", out)
        self.assertEqual(out, "---\ntitle: X\n---\n\n## Heading\nbody\n")

    def test_strips_en_page_german_flag(self):
        text = "🇩🇪 [German version](../../de/examples/osf_file_examples.md)\n\n# H\n"
        self.assertEqual(sync.strip_flag_link(text), "# H\n")

    def test_no_flag_is_unchanged(self):
        text = "---\ntitle: X\n---\n\n## Heading\nbody\n"
        self.assertEqual(sync.strip_flag_link(text), text)

    def test_flag_without_following_blank_line(self):
        text = "🇩🇪 [German version](../../de/index.md)\n# H\n"
        self.assertEqual(sync.strip_flag_link(text), "# H\n")

    def test_strips_root_level_flag_single_dotdot(self):
        # root-level pages (docs/de/index.md) use a single ../ ; deeper pages
        # use ../../ . Both must be stripped.
        text = "🇬🇧 [English version](../en/index.md)\n\n# Title\n"
        self.assertEqual(sync.strip_flag_link(text), "# Title\n")


class SyncLocaleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.src = self.tmp / "src"
        self.dst = self.tmp / "dst"
        (self.src / "media").mkdir(parents=True)
        (self.dst / "media").mkdir(parents=True)

    def _write(self, path: Path, content: str):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def test_clean_replace_md_and_additive_media(self):
        self._write(
            self.src / "index.md",
            "🇬🇧 [English version](../../en/index.md)\n\n# Title\n",
        )
        self._write(self.src / "media" / "shared.png", "NEW")
        self._write(self.dst / "old.md", "stale\n")
        self._write(self.dst / "media" / "shared.png", "OLD")
        self._write(self.dst / "media" / "target_only.png", "KEEP")

        tally = sync.sync_locale(self.src, self.dst, dry_run=False)

        self.assertEqual(
            (self.dst / "index.md").read_text(encoding="utf-8"), "# Title\n"
        )
        self.assertFalse((self.dst / "old.md").exists())
        self.assertEqual((self.dst / "media" / "shared.png").read_text(), "NEW")
        self.assertEqual(
            (self.dst / "media" / "target_only.png").read_text(), "KEEP"
        )
        self.assertEqual(tally["md"], 1)
        self.assertEqual(tally["removed_md"], 1)
        self.assertEqual(tally["media"], 1)

    def test_missing_target_raises(self):
        self._write(self.src / "index.md", "# T\n")
        with self.assertRaises(SystemExit):
            sync.sync_locale(self.src, self.tmp / "does_not_exist", dry_run=False)

    def test_dry_run_writes_nothing(self):
        self._write(self.src / "index.md", "# T\n")
        self._write(self.dst / "old.md", "stale\n")
        sync.sync_locale(self.src, self.dst, dry_run=True)
        self.assertFalse((self.dst / "index.md").exists())
        self.assertTrue((self.dst / "old.md").exists())


class MainTests(unittest.TestCase):
    def test_main_errors_on_non_directory_target(self):
        with self.assertRaises(SystemExit):
            sync.main(["this/path/does/not/exist"])
