---
title: Datei-Beispiele
description: OSF-Beispieldateien — synthetische Referenzdateien und echte Feldaufnahmen zum Testen und Lernen
image: "/img/om_social_card.png"
keywords:
  - Fileformat
  - OSF4
  - OSF5
  - OSF
  - Beispiele
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
---

🇬🇧 [English version](../../en/examples/osf_file_examples.md)

## OSF-Beispieldateien

Das Verzeichnis [`examples/`](https://github.com/optimeas/osf/tree/main/examples)
im Repository enthält zwei Arten von Dateien, mit denen sich das Format
erlernen und ein Reader stückweise prüfen lässt: **generierte
Referenzdateien** (synthetisch, ein Merkmal pro Datei) und **Feldproben**
(echte Aufnahmen von optiMEAS-Messgeräten).

## Generierte Referenzdateien

Die Dateien unter `examples/generated/` werden von der
[Delphi-Referenzimplementierung](../implementations/delphi.md) erzeugt
(nicht-interaktiv mit `OSFGeneratorCLI` reproduzierbar). Jede Datei zielt
auf **ein** Merkmal, und es gibt sie jeweils als OSF4-Variante (XML-Header)
und OSF5-Variante (JSON-Header):

| Datei (OSF4 / OSF5) | Zeigt |
|---|---|
| `osf4_equidistant.osf` / `osf5_equidistant.osf` | Äquidistanter Kanal (feste Abtastrate) |
| `osf4_scalar_numeric.osf` / `osf5_scalar_numeric.osf` | Zeitgestempelte Fließkomma-Skalarkanäle |
| `osf4_scalar_int64.osf` / `osf5_scalar_int64.osf` | Vorzeichenbehafteter 64-Bit-Ganzzahl-Kanal |
| `osf4_scalar_unsigned.osf` / `osf5_scalar_unsigned.osf` | Vorzeichenlose Ganzzahl-Kanäle |
| `osf4_gpslocation.osf` / `osf5_gpslocation.osf` | GPS-Positions-Kanal |
| `osf4_timestamped_string.osf` / `osf5_timestamped_string.osf` | Zeitgestempelte `string`-Nutzdaten |
| `osf4_timestamped_binary.osf` / `osf5_timestamped_binary.osf` | Zeitgestempelte `binary`-Blobs |
| `osf4_mixed.osf` / `osf5_mixed.osf` | Mehrere Kanaltypen in einer Datei |
| — / `osf5_mixed_extended.osf` | Erweiterte gemischte OSF5-Datei |

Insgesamt **17 Dateien** (8 OSF4 + 9 OSF5). Ein korrekter Reader muss alle
fehlerfrei verarbeiten. Diese Dateien sind die sprachübergreifenden
Lese-Fixtures; die OSF4/OSF5-Paarung prüft zugleich die
versions­deterministischen Regeln — etwa die Null-Terminierung von
`string`-/`binary`-Nutzdaten (OSF4 mit, OSF5 ohne abschließendes `0x00`).

## Feldproben — echte Aufnahmen

Echte Daten von optiMEAS-Geräten. Sie decken ab, was synthetische Dateien
nicht können: große Kanalzahlen, reale Zeitstempel-Muster und abrupt
endende Ströme. Ein Reader soll sie ohne Absturz lesen und die vorhandenen
Daten zurückliefern.

| Pfad | Format | Beschreibung |
|---|---|---|
| `motorbike.osf` | OSF4 | 81 Kanäle Motorrad-Telemetrie — Geschwindigkeiten, Temperaturen, GPS, Systemstatus |
| `steam_loco.osf` (+ `.csv`) | OSF4 | 123 Kanäle aus einer Dampflok-Aufnahme (`.csv` ist ein Referenz-Export derselben Daten) |
| `weather_station.osfz` | OSF4, gzip | 28 Kanäle, gzip-komprimiertes OSFZ — prüft die transparente Dekompression beim Lesen |
| `Testdata Motorbike/` | OSFZ | Mehrtägige Motorrad-Aufnahmen in Tages-Unterordnern (`YYYYMMDD/…`) — Robustheits-/Massen-Lesetests |

## Eine Datei öffnen

Am schnellsten geht das mit dem Python-Paket
[`osfdata`](../integrations/python.md):

```python
import osf

mgr = osf.load("examples/steam_loco.osf")
print(f"{len(mgr)} Kanäle")
for ch in mgr.channels:
    print(ch.name, ch.data_type, ch.sample_count)
```

Ebenso lassen sich die Dateien mit der [Rust](../implementations/rust.md)-
(`cargo run --example inspect -- <pfad>`), der
[C++](../implementations/cpp.md)- oder der
[Delphi](../implementations/delphi.md)-Implementierung (`osftool info`)
öffnen. Eine Format-Einführung gibt das Kapitel
[OSF-Format](../osf_general.md).
