# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH
"""Tests for docs/scripts/translation-status.py."""
import importlib.util
import shutil
import tempfile
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_SPEC = importlib.util.spec_from_file_location(
    "translation_status", _HERE / "translation-status.py"
)
ts = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(ts)

DE = "---\ntitle: Seite\n---\n\n# Titel\n\nText.\n\n```json\n{\"a\": 1}\n```\n"
IT = "---\ntitle: Pagina\n---\n\n# Titolo\n\nTesto.\n\n```json\n{\"a\": 1}\n```\n"


class StructureTests(unittest.TestCase):
    def test_faithful_translation_has_no_issues(self):
        self.assertEqual(ts.structure_issues(DE, IT), [])

    def test_changed_code_block_is_reported(self):
        self.assertEqual(
            ts.structure_issues(DE, IT.replace('{"a": 1}', '{"a": 2}')),
            ["code block 1 differs from the German page"],
        )

    def test_dropped_heading_and_front_matter_key_are_reported(self):
        issues = ts.structure_issues(DE + "\n## Mehr\n", IT.replace("title: Pagina\n", ""))
        self.assertEqual(len(issues), 2)

    def test_mermaid_labels_may_be_translated(self):
        de = "```mermaid\ngraph TD\n  A[Start] --> B[Ende]\n```\n"
        it = "```mermaid\ngraph TD\n  A[Inizio] --> B[Fine]\n```\n"
        self.assertEqual(ts.structure_issues(de, it), [])

    def test_hash_ignores_checkout_line_endings(self):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
        (tmp / "lf.md").write_bytes(b"# T\n\nbody\n")
        (tmp / "crlf.md").write_bytes(b"# T\r\n\r\nbody\r\n")
        self.assertEqual(ts.page_hash(tmp / "lf.md"), ts.page_hash(tmp / "crlf.md"))


class StatusTests(unittest.TestCase):
    def setUp(self):
        self.repo = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.repo, ignore_errors=True)
        self._write("docs/de/index.md", DE)
        self._write("docs/de/sub/page.md", DE)
        self._write("docs/de/media/pic.png", "PNG")
        for locale in ts.LOCALES:
            self._write(f"docs/{locale}/index.md", IT)
            self._write(f"docs/{locale}/sub/page.md", IT)
            self._write(f"docs/{locale}/media/pic.png", "PNG")

    def _write(self, rel: str, content: str):
        path = self.repo / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def _open(self, locale="it"):
        return {k: v for k, v in ts.status(self.repo)[locale].items() if v}

    def test_unrecorded_translation_counts_as_stale(self):
        self.assertEqual(self._open(), {"stale": ["index.md", "sub/page.md"]})

    def test_recorded_translation_is_up_to_date_until_german_changes(self):
        self.assertEqual(ts.record(self.repo), 2 * len(ts.LOCALES))
        self.assertEqual(self._open(), {})
        self.assertEqual(ts.main(["--repo-root", str(self.repo)]), 0)

        self._write("docs/de/index.md", DE.replace("Text.", "Neuer Text."))

        self.assertEqual(self._open(), {"stale": ["index.md"]})
        self.assertEqual(self._open("fr"), {"stale": ["index.md"]})
        self.assertEqual(ts.main(["--repo-root", str(self.repo)]), 1)

    def test_record_can_be_limited_to_pages_and_locale(self):
        ts.record(self.repo)
        self._write("docs/de/index.md", DE.replace("Text.", "A."))
        self._write("docs/de/sub/page.md", DE.replace("Text.", "B."))

        ts.record(self.repo, ("it",), ["index.md"])

        self.assertEqual(self._open("it"), {"stale": ["sub/page.md"]})
        self.assertEqual(self._open("fr"), {"stale": ["index.md", "sub/page.md"]})

    def test_missing_orphaned_and_media(self):
        ts.record(self.repo)
        (self.repo / "docs/it/sub/page.md").unlink()
        (self.repo / "docs/it/media/pic.png").unlink()
        self._write("docs/it/old.md", IT)

        self.assertEqual(
            self._open(),
            {"missing": ["sub/page.md"], "orphaned": ["old.md"], "media": ["media/pic.png"]},
        )

    def test_record_forgets_pages_removed_from_german(self):
        ts.record(self.repo)
        (self.repo / "docs/de/sub/page.md").unlink()
        ts.record(self.repo)
        self.assertNotIn("sub/page.md", ts.load_state(self.repo)["pages"]["it"])

    def test_record_rejects_unknown_page(self):
        with self.assertRaises(SystemExit):
            ts.record(self.repo, only=["nope.md"])


if __name__ == "__main__":
    unittest.main()
