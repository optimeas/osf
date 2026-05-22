# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH

"""Build one combined PDF per language from the Docusaurus docs tree.

For every language directory under ``docs/`` (any immediate subdirectory
that contains an ``index.md``) this script:

  * discovers every Markdown file recursively,
  * orders them the way Docusaurus builds its sidebar — by
    ``sidebar_position`` frontmatter, then alphabetically; a folder's
    position is taken from its ``index.md`` — with ``<lang>/index.md``
    as the opening page,
  * concatenates them, each page becoming an ``H1`` chapter titled by
    its frontmatter ``title``,
  * renders the result to ``docs/pdf-out/osf-docs-<lang>.pdf`` via
    headless Microsoft Edge or Google Chrome.

The Markdown files are the single source of truth; the PDFs are
disposable build artifacts (``docs/pdf-out/`` is gitignored). Adding a
new ``.md`` file — or a whole new language directory — needs no change
here; it is picked up automatically on the next run.

Usage:
    python docs/scripts/docs-to-pdf.py [--langs en de] [--keep-html]

Requires Python 3.9+ and Microsoft Edge or Google Chrome. The pure-Python
``markdown`` package is bootstrapped automatically into a temp cache when
it is not already importable.
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DOCS_DIR = Path(__file__).resolve().parent.parent
OUT_DIR = DOCS_DIR / "pdf-out"
INF = 10**9


# --------------------------------------------------------------------------
# dependency + browser bootstrap
# --------------------------------------------------------------------------
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


# --------------------------------------------------------------------------
# frontmatter + Docusaurus ordering
# --------------------------------------------------------------------------
def split_frontmatter(text):
    """Return (meta, body). meta carries only the top-level scalar keys
    (enough for 'title' and 'sidebar_position')."""
    if text[:1] == "﻿":
        text = text[1:]
    m = re.match(r"---[ \t]*\n(.*?)\n---[ \t]*\n", text, re.DOTALL)
    if not m:
        return {}, text
    meta = {}
    for line in m.group(1).splitlines():
        if not line.strip() or line.lstrip().startswith("#") or line[0] in " \t":
            continue
        mm = re.match(r"([A-Za-z0-9_-]+):[ \t]*(.*)$", line)
        if mm:
            meta[mm.group(1)] = mm.group(2).strip().strip('"').strip("'")
    return meta, text[m.end():]


def index_file(directory):
    for name in ("index.md", "index.mdx", "README.md"):
        p = directory / name
        if p.exists():
            return p
    return None


def position_of(md_path):
    meta, _ = split_frontmatter(md_path.read_text(encoding="utf-8"))
    val = meta.get("sidebar_position", "")
    return int(val) if val.lstrip("-").isdigit() else None


def order_dir(directory):
    """Return the Markdown files of a directory in Docusaurus sidebar order:
    the directory index first, then children sorted by (position, name)."""
    idx = index_file(directory)
    entries = []
    for child in sorted(directory.iterdir()):
        if child.is_dir():
            if child.name in ("media", "img", "assets"):
                continue
            ci = index_file(child)
            pos = position_of(ci) if ci else None
            entries.append((pos if pos is not None else INF, child.name, child))
        elif child.suffix in (".md", ".mdx") and child != idx:
            pos = position_of(child)
            entries.append((pos if pos is not None else INF, child.name, child))
    entries.sort(key=lambda e: (e[0], e[1].lower()))
    out = [idx] if idx else []
    for _, _, path in entries:
        out.extend(order_dir(path) if path.is_dir() else [path])
    return out


# --------------------------------------------------------------------------
# Markdown preprocessing (Docusaurus conventions)
# --------------------------------------------------------------------------
# A leading line that is just a flag emoji (two regional-indicator
# symbols) plus a link — the Docusaurus language switcher. Dropped from
# the combined per-language PDF.
FLAG_LINK = re.compile(
    r"^[ \t]*[\U0001F1E6-\U0001F1FF]{2}[ \t]*\[[^\]]*\]\([^)]*\)[ \t]*$",
    re.MULTILINE)
ADMONITION = re.compile(r"^:::(\w+)(?:\[([^\]]*)\])?[ \t]*\n(.*?)\n:::[ \t]*$",
                        re.MULTILINE | re.DOTALL)
IMAGE = re.compile(r"!\[([^\]]*)\]\(([^)]+)\)")


def _admonition(m):
    kind = m.group(1).lower()
    title = m.group(2) or kind.upper()
    return (f'<div class="admonition adm-{kind}" markdown="1">\n'
            f'<p class="admonition-title">{title}</p>\n\n{m.group(3)}\n\n</div>')


def preprocess(body, md_path):
    """Apply the Docusaurus-specific cleanups a plain Markdown renderer
    would otherwise mishandle."""
    body = FLAG_LINK.sub("", body)

    # Drop a leading level-1 heading — the frontmatter title is injected
    # as the chapter H1, so a body H1 would just duplicate it.
    lines = body.split("\n")
    for i, line in enumerate(lines):
        s = line.strip()
        if s.startswith("#"):
            if re.match(r"#[ \t]+\S", s):
                lines[i] = ""
            break
    body = "\n".join(lines)

    body = ADMONITION.sub(_admonition, body)

    # Resolve relative image paths to absolute file:// URIs so they load
    # regardless of where the intermediate HTML is written.
    def fix_image(m):
        alt, src = m.group(1), m.group(2).strip()
        if src.startswith(("http://", "https://", "data:", "file:")):
            return m.group(0)
        target = (md_path.parent / src.lstrip("/")).resolve()
        return f"![{alt}]({target.as_uri()})" if target.exists() else m.group(0)

    return IMAGE.sub(fix_image, body)


# --------------------------------------------------------------------------
# rendering
# --------------------------------------------------------------------------
CSS = """
@page { size: A4; margin: 16mm 15mm; }
* { box-sizing: border-box; }
body { font-family: "Segoe UI","Helvetica Neue",Arial,sans-serif;
       font-size: 10pt; line-height: 1.45; color: #1a1a1a; margin: 0; }
h1 { font-size: 20pt; margin: 0 0 9pt; padding-bottom: 5pt;
     border-bottom: 2px solid #c8c8c8; page-break-before: always; }
h1:first-of-type { page-break-before: avoid; }
h2 { font-size: 14pt; margin: 16pt 0 6pt; padding-bottom: 2pt;
     border-bottom: 1px solid #e2e2e2; page-break-after: avoid; }
h3 { font-size: 11.5pt; margin: 12pt 0 5pt; page-break-after: avoid; }
h4 { font-size: 10.5pt; margin: 10pt 0 4pt; page-break-after: avoid; }
p { margin: 5pt 0; }
a { color: #1452cc; text-decoration: none; }
img { max-width: 100%; }
code { font-family: Consolas,"Cascadia Mono",monospace; font-size: 8.8pt;
       background: #f3f3f3; padding: 0.5pt 3pt; border-radius: 3px; }
pre { font-family: Consolas,"Cascadia Mono",monospace; font-size: 8.6pt;
      line-height: 1.4; background: #f6f6f6; border: 1px solid #e2e2e2;
      border-radius: 4px; padding: 7pt 9pt; white-space: pre;
      overflow: hidden; page-break-inside: avoid; }
pre code { background: none; padding: 0; font-size: inherit; }
table { border-collapse: collapse; width: 100%; margin: 7pt 0; font-size: 8.8pt; }
th, td { border: 1px solid #d4d4d4; padding: 3.5pt 6pt; text-align: left;
         vertical-align: top; }
th { background: #efefef; font-weight: 600; }
tr { page-break-inside: avoid; }
ul, ol { margin: 5pt 0; padding-left: 20pt; }
li { margin: 2pt 0; }
hr { border: none; border-top: 1px solid #e2e2e2; margin: 12pt 0; }
blockquote { margin: 6pt 0; padding: 2pt 10pt; border-left: 3px solid #ccc;
             color: #444; }
strong { font-weight: 600; }
.admonition { border-left: 4px solid #bbb; background: #f6f6f6;
              padding: 5pt 10pt; margin: 8pt 0; border-radius: 0 4px 4px 0;
              page-break-inside: avoid; }
.admonition-title { font-weight: 700; margin: 0 0 3pt; text-transform: uppercase;
                    font-size: 8.3pt; letter-spacing: .04em; }
.adm-tip { border-color: #2a9d3b; } .adm-success { border-color: #2a9d3b; }
.adm-note { border-color: #3578e5; } .adm-info { border-color: #3578e5; }
.adm-warning { border-color: #e6a700; } .adm-caution { border-color: #e6a700; }
.adm-danger { border-color: #d83b3b; }
"""

HTML = ("<!DOCTYPE html>\n<html lang=\"{lang}\"><head><meta charset=\"utf-8\">\n"
        "<title>{title}</title>\n<style>{css}</style></head>\n<body>\n{body}\n"
        "</body></html>\n")


def build_language(lang_dir, browser, keep_html):
    import markdown

    pages = order_dir(lang_dir)
    if not pages:
        print(f"  {lang_dir.name}: no Markdown files, skipped.")
        return

    chunks = []
    for page in pages:
        meta, body = split_frontmatter(page.read_text(encoding="utf-8"))
        title = meta.get("title") or page.stem
        chunks.append(f"# {title}\n\n{preprocess(body, page)}\n")
    combined = "\n\n\n".join(chunks)

    html_body = markdown.markdown(combined, extensions=["extra", "sane_lists"])
    document = HTML.format(lang=lang_dir.name, css=CSS,
                           title=f"OSF Documentation ({lang_dir.name})",
                           body=html_body)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    html_path = OUT_DIR / f"osf-docs-{lang_dir.name}.html"
    pdf_path = OUT_DIR / f"osf-docs-{lang_dir.name}.pdf"
    html_path.write_text(document, encoding="utf-8")

    render_pdf(html_path, pdf_path, browser)
    if not keep_html:
        html_path.unlink()
    size_kb = pdf_path.stat().st_size // 1024
    print(f"  {lang_dir.name}: {len(pages)} pages -> {pdf_path.name} ({size_kb} kB)")


def render_pdf(html_path, pdf_path, browser):
    """Render html_path to pdf_path with headless Edge/Chrome."""
    if pdf_path.exists():
        pdf_path.unlink()
    user_data = Path(tempfile.mkdtemp(prefix="osf-pdf-browser-"))
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


# --------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--langs", nargs="*",
                        help="Language directories to build (default: autodetect).")
    parser.add_argument("--keep-html", action="store_true",
                        help="Keep the intermediate HTML next to the PDF.")
    args = parser.parse_args()

    ensure_markdown()
    browser = find_browser()

    if args.langs:
        lang_dirs = [DOCS_DIR / name for name in args.langs]
    else:
        lang_dirs = sorted(d for d in DOCS_DIR.iterdir()
                           if d.is_dir() and index_file(d))

    print(f"docs root : {DOCS_DIR}")
    print(f"browser   : {browser}")
    print(f"output    : {OUT_DIR}")
    for lang_dir in lang_dirs:
        if not lang_dir.is_dir():
            raise SystemExit(f"Language directory not found: {lang_dir}")
        build_language(lang_dir, browser, args.keep_html)


if __name__ == "__main__":
    main()
