---
title: Implementierungen
description: OSF-Implementierungen je Programmiersprache — Delphi, Rust, Python, C++, Java und geplante Sprachen
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Implementierung
  - Delphi
  - Rust
  - Python
  - C++
  - Java
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

🇬🇧 [English version](../../en/implementations/index.md)

## OSF-Implementierungen

OSF ist ein offenes Format — es ist bewusst so einfach gehalten, dass es in
jeder Sprache eigenständig implementiert werden kann. Dieses Kapitel
beschreibt die **konkreten Implementierungen** des Formats je Programmier­sprache:
was sie können, wie man sie installiert bzw. baut, und wo der Quellcode liegt.

Abzugrenzen davon ist das Kapitel [Integrationen](../integrations/index.md):
dort geht es um die Anbindung an **Ökosysteme** (Arrow, PyTorch, MCP …),
nicht um die Sprach-Implementierungen selbst.

### Status-Übersicht

Legende: ✅ verfügbar · 🚧 in aktiver Entwicklung · 📋 geplant

| Implementierung | Status | Kurzbeschreibung |
|---|---|---|
| **[Delphi](delphi.md)** | ✅ | Referenz-Implementierung — vollständige Library, Demos und die `osftool`-CLI (Windows / RAD Studio) |
| **[Rust](rust.md)** (`osf-core`) | ✅ | Lesen, Schreiben und transparentes OSFZ; zugleich Fundament der Python-Anbindung |
| **[Python](python.md)** (`osfdata`) | ✅ | PyO3-Bindings über den Rust-Kern, NumPy-Integration; siehe [Python-Integration](../integrations/python.md) |
| **[C++](cpp.md)** | ✅ | Eigenständige C++17-Implementierung — Reader, beide Writer, C-ABI; CI auf Linux/macOS/Windows. Ausführliches Entwickler-Handbuch unter [C++ im Detail](cpp/architecture.md) |
| **[Java](java.md)** | 📋 | Architektur entschieden (Java 25, Maven, JPMS); noch kein Code |
| **[Weitere Sprachen](planned.md)** | 📋 | C — geplant |

Den jeweils aktuellsten Stand führt das Repository auf
[GitHub](https://github.com/optimeas/osf).

### Womit anfangen?

- **Daten auswerten** (Python/Notebook, Pandas, ML): das Paket
  [`osfdata`](../integrations/python.md) — `pip install osfdata`, Datei
  laden, Kanäle als NumPy-Array.
- **Native Integration / hohe Performance** (Server, Embedded): die
  [Rust](rust.md)- oder [C++](cpp.md)-Implementierung.
- **Windows-Werkzeuge / Referenzverhalten**: die [Delphi](delphi.md)-
  Implementierung samt der `osftool`-Kommandozeile.

Alle Implementierungen lesen denselben Satz an
[Beispieldateien](../examples/osf_file_examples.md) und folgen denselben
semantischen Regeln der Spezifikation.

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
