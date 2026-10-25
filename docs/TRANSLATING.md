# Translating the documentation

The documentation exists in five languages:

| Tree | Language | Role |
|---|---|---|
| `docs/de/` | German | source of the Italian, French and Spanish translations |
| `docs/en/` | English | maintained by hand alongside German |
| `docs/it/` | Italian | translated from German |
| `docs/fr/` | French | translated from German |
| `docs/es/` | Spanish | translated from German |

All five are published from this repository:
`docs/scripts/sync-to-docusaurus.py` copies them into the Docusaurus site and
overwrites what is there. A page edited only in the Docusaurus repository is
lost at the next sync, so every change to an OSF page starts here.

## Workflow

A change to a page under `docs/de/` is complete when the same change is in
`docs/en/`, `docs/it/`, `docs/fr/` and `docs/es/`.

1. Edit the German page and mirror the change in `docs/en/`.
2. Ask what is open:

   ```bash
   python docs/scripts/translation-status.py
   ```

   It lists, per language, the pages whose German source changed since they
   were translated (`stale`), German pages without a translation (`missing`),
   translations without a German page (`orphaned`), pages whose headings, code
   blocks or front matter no longer line up with German (`structure`) and
   media files that are absent (`media`). It exits with 1 while anything is
   open.
3. Bring the listed pages up to date following the rules below. For a small
   change, apply the same edit to the Italian, French and Spanish page; for a new or
   reworked page, translate it again from German.
4. Record that the translations now match the German text:

   ```bash
   python docs/scripts/translation-status.py --record
   ```

   This writes `docs/translation-state.json`, which holds the hash of the
   German page each translation was made from. Commit it together with the
   pages. `--record implementations/java.md` or `--locale it` limits what is
   recorded; use that when only part of the list has been done.
5. Run the status again; it must report `up to date` for every language.

Recording a page states that its translation is current. Do not record to
make the list go away.

## Rules for a translation

A translated page keeps the path and file name of its German page.

Left exactly as in German:

- fenced code blocks, including comments inside them, and inline `code`
- front matter keys and structure; the values of `image`, `sidebar_position`,
  `slug`, `id`, `last_update`
- link and image targets: relative paths, file names, `#anchors`, URLs
- heading count, order and level, and explicit heading ids (`{#...}`)
- HTML, table structure, math, identifiers, numbers, units
- product and format names (`OSF`, `OSF4`, `OSF5`, `osftool`, `osfdata`,
  `optiMEAS`, `smartCORE`)

Translated: body text, headings, table headers and cell prose, list items,
admonition text, image alt text, link text, and the front matter values
`title`, `description`, `sidebar_label` and generic `keywords`.

`mermaid` diagrams: translate only the human-readable labels. Keep node ids,
keywords and arrows, and add no character to a label that was not there
before (parentheses, quotes, colons, brackets); they can break the diagram.

A heading that another page links to by anchor must keep that anchor. When the
translated heading text would produce a different one, append the German
anchor as an explicit id: `## Configurazione {#konfiguration}`.

Write natural technical prose for engineers, formal address (Italian:
impersonal or "voi"; French: "vous"; Spanish: "usted"). Add nothing that is not in the German
page and drop nothing. Keep English terms that engineers use in the target
language (CAN bus, timestamp, firmware).

## What the status tool cannot see

It compares structure, not meaning. A page can pass every check and still be
a poor translation; nobody has proofread the Italian, French and Spanish pages
as a native speaker.
