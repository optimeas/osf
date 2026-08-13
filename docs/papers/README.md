---
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Papers

Companion papers to the Open Streaming Format.

## Integrity and Signing in Streaming Measurement Data Formats — The OSF5 Approach

*Integrität und Signierung in Streaming-Messdatenformaten: Der OSF5-Ansatz*

- **English edition:** [`2026-08_osf5-integrity-profile_v1.1_en.pdf`](2026-08_osf5-integrity-profile_v1.1_en.pdf)
- **German edition (authoritative):** [`2026-08_osf5-integritaetsprofil_v1.1.pdf`](2026-08_osf5-integritaetsprofil_v1.1.pdf)
- **Authors:** Schranz, B. / optiMEAS GmbH & optiMEAS Switzerland GmbH
- **Version:** 1.1 (August 2026)
- **DOI:** [10.5281/zenodo.21227941](https://doi.org/10.5281/zenodo.21227941)
- **License:** CC BY 4.0

Concept paper on the OSF5 integrity profile: a three-level ladder of *none*,
*crc* and *signed*, combining per-block CRC32C corruption detection, a SHA-256
hash chain with periodic Ed25519 signature anchors, and an X.509 certificate
model that makes a file verifiable offline by uninvolved third parties.

The CRC level it describes is normative in the specification since the
2026-07-07 revision and implemented in all five reference implementations; the
signature and PKI levels are described here in full for the first time.

German is the authoritative version; the English edition is a translation of it.

### Citing

The DOI above is the **concept DOI** — it always resolves to the newest
version, so prefer it in citations:

> Schranz, B. / optiMEAS GmbH & optiMEAS Switzerland GmbH (2026):
> Integrity and Signing in Streaming Measurement Data Formats: The OSF5
> Approach. Zenodo. https://doi.org/10.5281/zenodo.21227941

Version 1.0 (June 2026, German only) is superseded and no longer kept in this
repository, but remains permanently available at its own version DOI,
[10.5281/zenodo.21227942](https://doi.org/10.5281/zenodo.21227942).

The [Zenodo record](https://doi.org/10.5281/zenodo.21227941) is the citable
reference; the copies in this repository are provided for convenience.

### Building the PDFs

The Markdown sources under [`src/`](src/) are the single source of truth — the
PDFs are generated from them and committed as published artifacts:

```
python docs/scripts/paper-to-pdf.py [--langs de en]
```

Requires Microsoft Edge or Google Chrome and the **DejaVu fonts** installed.
The layout metrics in `src/paper.css` were measured out of the published v1.0
PDF — font sizes, page margins and leading — so that versions stay visually
continuous. Headless Edge/Chrome reproduces those metrics exactly; a current
wkhtmltopdf build does not, despite having produced v1.0, so do not switch the
renderer without re-running that comparison.

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
