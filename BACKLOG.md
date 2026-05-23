# OSF — Backlog

Open ideas, future considerations, and items intentionally deferred.
Separate from DECISIONS.md, which records decisions that have been
made. An entry in this backlog is **not** a commitment; it is a
note that the item has been considered and parked for later
discussion.

Organised by category. New entries should land in the most fitting
section (or open a new section if none fits).

---

## Spec Extensions / Future Format Revisions

### OSF6 (or future spec revision): null-terminator handling for string/binary

The trailing `0x00` byte on `string` and `binary` payloads in
`bcAbsTimeStampData` (mandatory in OSF4 and OSF5 per spec rev
2026-05-04) is a historical artefact from C-style string handling.
For binary payloads in particular it is an awkward fit: a JPEG file
ending in `0xD9 0xFF` becomes `0xD9 0xFF 0x00` on disk, and readers
that fail to strip the trailing byte produce invalid JPEGs.

Making the null-terminator optional in OSF5 was considered and
rejected: without a flag bit in the control byte, readers cannot
distinguish a deliberately-trailing `0x00` (e.g. inside an ASN.1
binary payload) from a spec-mandated terminator. Such a flag bit
would be a true spec change requiring all implementations to be
revised, with marginal payload savings (one byte per string or
binary sample).

A future spec revision (OSF6 or similar) could remove the trailing
byte entirely, with a clear version-bump signalling the format
change. Until then, the byte stays in both OSF4 and OSF5.

---

## Documentation Strategy

### Multi-language documentation system (after Java implementation)

Documentation strategy for the project as a whole is pending a
dedicated design session, scheduled to follow the Java
implementation reaching feature parity with C++ and Python. Topics
to be decided:

- Tooling per language (native Doxygen / rustdoc / Pydoc / PasDoc
  vs. unifying through Sphinx with breathe / sphinx-rustdocgen /
  autodoc).
- Multi-natural-language strategy (German and English minimum; how
  to keep them in sync — gettext workflow vs. parallel Markdown
  sources, manual translation vs. machine-assisted).
- Hosting (GitHub Pages, ReadTheDocs, dedicated domain).
- Example-code structure: examples kept in the repo, tested in CI,
  embedded in docs via `literalinclude` or equivalent so they cannot
  drift out of sync.
- Adoption focus: docs aimed at external readers who want to
  integrate OSF into their own projects or analyse OSF files
  produced by others — not primarily at internal contributors.

The strategic goal: make OSF easy enough to adopt that engineers
choose it over CSV or Parquet as the carrier format for measurement
data. Documentation is a first-class product, not an afterthought.

---

## Implementation Gaps and Conventions

(Empty for now. Add entries as gaps surface.)

---

## How to add an entry

- Place under the most fitting section, or add a new section if
  needed.
- Each entry: a short `###` title summarising the topic, then a
  paragraph or two of context.
- State explicitly what was considered, what was decided (parked
  vs. ready to act), and what would unlock further action.
- Cross-link to relevant `DECISIONS.md` sections or
  `implementations/*/CHANGELOG.md` entries where applicable.
- Backlog entries are not commitments. They can be removed when a
  decision is taken (with the decision moving to `DECISIONS.md`)
  or when the item becomes irrelevant.
