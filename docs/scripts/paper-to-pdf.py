# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH

"""Render the OSF concept paper from Markdown to PDF, one file per language.

The Markdown sources under ``docs/papers/src/`` are the single source of
truth. Unlike the disposable output of ``docs-to-pdf.py``, the PDFs this
script produces are *published artifacts* and are committed: they are the
files uploaded to Zenodo and printed for distribution.

Layout follows ``docs/papers/src/paper.css``, whose metrics were measured
out of the published v1.0 PDF so that new versions stay visually
continuous with it.

Usage:
    python docs/scripts/paper-to-pdf.py [--langs de en] [--keep-html]

Requires Python 3.9+, Microsoft Edge or Google Chrome, and the DejaVu
fonts installed. The pure-Python ``markdown`` package is bootstrapped
into a temp cache when it is not already importable.

Renderer note: headless Edge/Chrome is used rather than wkhtmltopdf, even
though wkhtmltopdf produced v1.0. Measured against the v1.0 metrics, the
browser reproduces them exactly while a current wkhtmltopdf 0.12.6 build
scales absolute pt values by roughly 0.91 and names the DejaVu faces
differently. Do not switch the renderer without re-running that
comparison.

To check a rendered PDF against the v1.0 metrics:
    python -c "from pypdf import PdfReader; r = PdfReader('<pdf>'); \
        print([str(v.get_object().get('/BaseFont')) \
        for v in r.pages[0]['/Resources']['/Font'].values()])"
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

PAPERS_DIR = Path(__file__).resolve().parent.parent / "papers"
SRC_DIR = PAPERS_DIR / "src"

# Output filename per language. These are committed artifacts, so the
# names are stable and deliberately carry the version.
OUTPUT = {
    "de": "2026-08_osf5-integritaetsprofil_v1.1.pdf",
    "en": "2026-08_osf5-integrity-profile_v1.1_en.pdf",
}

LABELS = {
    "de": {"authors": "Autoren:", "corresponding": "Verantwortlicher Autor:",
           "version": "Version:", "license": "Lizenz:"},
    "en": {"authors": "Authors:", "corresponding": "Corresponding author:",
           "version": "Version:", "license": "License:"},
}

HTML = """<!doctype html>
<html lang="{lang}"><head><meta charset="utf-8">
<title>{title}</title><style>{css}</style></head>
<body>{body}</body></html>"""


def ensure_markdown():
    """Import the markdown package, pip-installing it into a temp cache
    on first use so the script is self-contained on a fresh machine."""
    try:
        import markdown  # noqa: F401
        return
    except ImportError:
        pass
    cache = Path(tempfile.gettempdir()) / "osf-docs-pdf-deps"
    if not (cache / "markdown").is_dir():
        print(f"Bootstrapping 'markdown' into {cache} ...")
        subprocess.run(
            [sys.executable, "-m", "pip", "install", "--target", str(cache),
             "--quiet", "--disable-pip-version-check", "markdown"],
            check=True,
        )
    sys.path.insert(0, str(cache))
    import markdown  # noqa: F401


def find_browser():
    """Locate a Chromium-family browser usable for --print-to-pdf."""
    names = ["msedge", "msedge.exe", "chrome", "chrome.exe",
             "google-chrome", "chromium", "chromium-browser"]
    for n in names:
        hit = shutil.which(n)
        if hit:
            return hit
    candidates = [
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
        "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    ]
    for c in candidates:
        if Path(c).exists():
            return c
    raise SystemExit("No Microsoft Edge or Google Chrome found for PDF rendering.")


def split_frontmatter(text):
    """Return (meta, body) with meta carrying the top-level scalar keys."""
    if text[:1] == "\ufeff":
        text = text[1:]
    m = re.match(r"---[ \t]*\n(.*?)\n---[ \t]*\n", text, re.DOTALL)
    if not m:
        return {}, text
    meta = {}
    for line in m.group(1).splitlines():
        mm = re.match(r"([A-Za-z0-9_-]+):[ \t]*(.*)$", line)
        if mm:
            meta[mm.group(1)] = mm.group(2).strip().strip('"').strip("'")
    return meta, text[m.end():]


def title_block(meta, labels):
    """Build the title, subtitle and metadata list shown above the abstract."""
    values = {
        "authors": meta.get("authors", ""),
        "corresponding": meta.get("corresponding", ""),
        # Version and date share one line, as in the published v1.0.
        "version": " · ".join(v for v in (meta.get("version"), meta.get("date")) if v),
        "license": meta.get("license", ""),
    }
    rows = "".join(f"<dt>{labels[key]}</dt><dd>{values[key]}</dd>"
                   for key in ("authors", "corresponding", "version", "license")
                   if values[key])
    return (f"<h1>{meta.get('title', '')}</h1>"
            f"<p class=\"subtitle\">{meta.get('subtitle', '')}</p>"
            f"<dl class=\"titlemeta\">{rows}</dl>")


def build(lang, browser, keep_html):
    import markdown

    src = SRC_DIR / f"osf5-integrity-profile.{lang}.md"
    if not src.exists():
        raise SystemExit(f"Source not found: {src}")
    if lang not in LABELS:
        raise SystemExit(f"No title-block labels configured for language: {lang}")

    css = (SRC_DIR / "paper.css").read_text(encoding="utf-8")
    meta, body_md = split_frontmatter(src.read_text(encoding="utf-8"))

    body_html = markdown.markdown(body_md, extensions=["extra", "sane_lists"])
    document = HTML.format(
        lang=lang, css=css, title=meta.get("title", "OSF paper"),
        body=title_block(meta, LABELS[lang]) + body_html)

    html_path = PAPERS_DIR / f".paper-{lang}.html"
    pdf_path = PAPERS_DIR / OUTPUT[lang]
    html_path.write_text(document, encoding="utf-8")
    render_pdf(html_path, pdf_path, browser)
    if not keep_html:
        html_path.unlink()
    print(f"  {lang}: {pdf_path.name} ({pdf_path.stat().st_size // 1024} kB)")


def render_pdf(html_path, pdf_path, browser):
    """Render html_path to pdf_path with headless Edge/Chrome."""
    if pdf_path.exists():
        pdf_path.unlink()
    user_data = Path(tempfile.mkdtemp(prefix="osf-paper-browser-"))
    try:
        for headless in ("--headless=new", "--headless"):
            subprocess.run(
                [browser, headless, "--disable-gpu", "--no-pdf-header-footer",
                 f"--user-data-dir={user_data}",
                 f"--print-to-pdf={pdf_path}", html_path.as_uri()],
                capture_output=True, timeout=180,
            )
            if pdf_path.exists() and pdf_path.stat().st_size > 2000:
                return
            if pdf_path.exists():
                pdf_path.unlink()
    finally:
        shutil.rmtree(user_data, ignore_errors=True)
    raise SystemExit(f"Browser failed to render {pdf_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--langs", nargs="*", default=None,
                        help="Languages to build (default: all sources found).")
    parser.add_argument("--keep-html", action="store_true",
                        help="Keep the intermediate HTML next to the PDF.")
    args = parser.parse_args()

    ensure_markdown()
    browser = find_browser()
    langs = args.langs or sorted(
        p.name.split(".")[-2]
        for p in SRC_DIR.glob("osf5-integrity-profile.*.md"))

    print(f"sources : {SRC_DIR}")
    print(f"browser : {browser}")
    for lang in langs:
        if lang not in OUTPUT:
            raise SystemExit(f"No output filename configured for language: {lang}")
        build(lang, browser, args.keep_html)


if __name__ == "__main__":
    main()
