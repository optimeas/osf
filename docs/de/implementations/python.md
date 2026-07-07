---
title: Python-Implementierung
description: Das osfdata-Paket — Python-Anbindung an OSF über die Rust-Foundation
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Python
  - osfdata
  - PyO3
  - NumPy
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

🇬🇧 [English version](../../en/implementations/python.md)

# Python-Implementierung

Die Python-Anbindung wird vom Paket **`osfdata`** bereitgestellt (Import als
`osf`). Sie ist keine reine Python-Implementierung, sondern ein dünner
PyO3-Wrapper über die [Rust-Foundation](rust.md) `osf-core`: das ergibt
Lade-Zeiten im Millisekunden-Bereich und numerische Daten ohne Kopie als
NumPy-Arrays.

```python
import osf

mgr = osf.load("messung.osf")             # .osf und .osfz
temp = mgr.channel("Sensor.Temperatur")
werte = temp.samples()                     # NumPy-Array, dtype passt zum OSF-Typ
zeit  = temp.timestamps_ns()
```

Installation:

```bash
pip install osfdata        # oder:  uv pip install osfdata
```

Die **vollständige Dokumentation** — unterstützte Plattformen, API-Übersicht
(`DataManager`, `Channel`, `Segment`, `WriterBuilder`), Schreiben, OSFZ und
Hinweise zur Verwendung — steht im Kapitel
**[Python-Integration](../integrations/python.md)**. Sie wird hier bewusst
nicht dupliziert.

## Quellcode und weiterführende Informationen

- Paket auf PyPI: [pypi.org/project/osfdata](https://pypi.org/project/osfdata/)
- Quellcode auf GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  Verzeichnis `implementations/python/`
- Vollständige API: [Python-Integration](../integrations/python.md)
- Zugrunde liegender Kern: [Rust-Implementierung](rust.md)

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
