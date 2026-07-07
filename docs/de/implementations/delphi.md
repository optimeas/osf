---
title: Delphi-Implementierung
description: Die Delphi-Referenzimplementierung des Open Streaming Format — Library, Demos und die osftool-Kommandozeile
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Delphi
  - osftool
  - RAD Studio
  - Referenzimplementierung
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

🇬🇧 [English version](../../en/implementations/delphi.md)

# Delphi-Implementierung

Die Delphi-Implementierung ist die **Referenz-Implementierung** des Open
Streaming Format: vollständiger Lese- und Schreibpfad für OSF4 und OSF5,
ein Satz Demos, und die `osftool`-Kommandozeile. Sie erzeugt zugleich den
Satz an [Referenzdateien](../examples/osf_file_examples.md), gegen den die
übrigen Implementierungen geprüft werden. Entwickelt mit RAD Studio 23.0
(Delphi 12).

## Bibliothek

Die Library-Units liegen unter `implementations/delphi/src/`. Die
wichtigsten öffentlichen Bausteine:

| Unit | Inhalt |
|---|---|
| `OSF.Types` | Datentypen und Kanaltypen (`TOSFDataType`, `TOSFVersion`, `TOSFGpsLocation`) und Hilfsfunktionen |
| `OSF.Filer` | `TOSFFile` — Streaming-Reader/Writer für OSF4 und OSF5; transparente OSFZ-Dekompression; OSF4-XML-Parsing über OmniXML (keine MSXML-Abhängigkeit) |
| `OSF.Data.Manager` | `TOSFDataManager` — High-Level-Lesen, füllt typisierte Kanäle; Zugriff per Name |
| `OSF.Data.Channels` | typisierte In-Memory-Kanäle inkl. äquidistanter Segmente (jedes `bcStartData` öffnet ein Segment) |
| `OSF.Log` | prozessweiter Logging-/Fortschritts-Dispatcher (`Logger: TOSFLog`) mit Listener-Pattern (DECISIONS §22) |
| `OSF.Export.CSV` / `OSF.Export.CSV.Unified` | CSV-Export — pro Kanal (XY) bzw. gemeinsame Zeitachse |
| `OSF.Export.HDF5` | HDF5-Export (Windows; ein komprimiertes Compound-Dataset je Kanal) |
| `OSF.Merger` | `TOSFMerger` — mehrere OSF-Dateien eines Zeitintervalls zu einer Ausgabedatei zusammenführen |
| `OSF.Meta.Cache` | Sidecar-`.json`-Cache je OSF-Datei (Zeitbereiche, Sample-Zahlen) für schnelle Metadaten |

Ein no-Form-Programm `OSFCompileCheck.dpr` im Implementierungs-Wurzel­verzeichnis
deckt per `uses` jede Library-Unit ab und gibt nach Refactorings ein
sauberes Kompilier-Signal.

### Logging-Architektur

Jede Library-Unit schreibt Log- und Fortschritts-Ereignisse an den
globalen `Logger: TOSFLog`. Anwendungen registrieren einen
`TLoggerListener` (mit eigenem `MinLevel` und Event-Callbacks); der
Singleton übernimmt die Verteilung. Details in
[DECISIONS §22](https://github.com/optimeas/osf/blob/main/DECISIONS.md).

## Demos

Unter `implementations/delphi/demos/`:

| Demo | Zweck |
|---|---|
| `osfviewer/` | OSF-Datei laden, Kanäle auflisten, einen Kanal als TeeChart darstellen (nur in der IDE) |
| `osfgenerator/` | schreibt den 17-Datei-Referenzsatz; die Konsolen-Variante `OSFGeneratorCLI` erzeugt ihn nicht-interaktiv |
| `osfcsvexport/` | Pipeline-Demo `TOSFFile` → `TOSFDataManager` → CSV-Export |
| `osfmerger/` | VCL-Oberfläche für `TOSFMerger` (Win64) |

## Kommandozeile: `osftool`

`osftool` ist das verb-basierte Kommandozeilenwerkzeug der
Delphi-Implementierung (Win64) mit neun Verben — `merge`, `export`,
`info`, `channels`, `stat`, `cache`, `config`, `convert`, `verify`.

Die vollständige Beschreibung steht im eigenen Kapitel
**[osftool](../tools/osftool.md)**.

## Bauen

Die Library und die meisten Demos werden mit `dcc32` (Win32) bzw. `dcc64`
(Win64) aus dem jeweiligen Projektverzeichnis übersetzt — `osfmerger` und
`osftool` als Win64. Beispiel (Compile-Check):

```powershell
cd implementations\delphi
dcc32 -B -Q OSFCompileCheck.dpr
```

Der HDF5-Export benötigt zur Laufzeit `hdf5.dll`; die Bindung
(`src/hdf5/`) ist eine eigenständige, OSF-unabhängige Delphi-Anbindung an
die HDF5-C-Bibliothek.

## Quellcode und weiterführende Informationen

- Quellcode auf GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  Verzeichnis `implementations/delphi/`
- Kommandozeile: Kapitel [osftool](../tools/osftool.md)
- Format-Spezifikation: Kapitel [OSF-Format](../osf_general.md)

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
