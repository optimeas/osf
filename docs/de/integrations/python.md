---
title: Python Integration
description: Python-Bindings für das Open Streaming Format (OSF) — Lesen und Schreiben mit nativer Performance über osfdata
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Python
  - osfdata
  - PyO3
  - NumPy
  - pandas
last_update:
  date: 2026-05-06
  author: Optimeas GmbH
---

🇬🇧 [English version](../../en/integrations/python.md)

# Python-Integration

Das Paket **`osfdata`** stellt eine vollständige Python-Anbindung an das Open Streaming Format zur Verfügung. Es liest und schreibt OSF4- und OSF5-Dateien, erkennt komprimierte OSFZ-Dateien automatisch, und integriert sich nahtlos in das wissenschaftliche Python-Ökosystem rund um NumPy.

Im Unterschied zu reinen Python-Implementierungen wird die eigentliche Arbeit in einer Rust-Bibliothek erledigt. Python sieht und nutzt nur den dünnen Wrapper darüber. Das Ergebnis: Lade-Zeiten im Millisekunden-Bereich auch bei mehreren hunderttausend Samples, sehr geringer Speicher-Overhead, und numerische Daten werden ohne Kopie in NumPy-Arrays übertragen.

## Wofür `osfdata` gedacht ist

`osfdata` löst typische Aufgaben rund um OSF-Daten in Python:

- **Daten in Analyse-Pipelines bringen.** OSF-Dateien werden geladen, einzelne Kanäle direkt als NumPy-Arrays abgerufen und an Bibliotheken wie SciPy, scikit-learn, oder PyTorch weitergegeben.
- **Daten von verschiedenen Quellen zusammenführen.** OSF-Dateien aus dem Feld werden gelesen, gefiltert oder kombiniert und als neue OSF-Datei geschrieben.
- **Bestandsdaten auf den aktuellen Stand bringen.** OSF4-Dateien werden gelesen und als OSF5 neu geschrieben — eine bequeme Migration ohne separates Werkzeug.
- **Komprimierte Dateien transparent verarbeiten.** OSFZ-Dateien (zlib oder gzip) werden automatisch erkannt und entpackt, ohne dass der Anwender etwas konfigurieren muss.

Das Paket richtet sich an Datenanalysten, Ingenieure und Wissenschaftler, die OSF-Daten in Python-Workflows einbinden wollen. Es richtet sich nicht an Embedded-Entwickler, die Daten *erzeugen* — dafür gibt es separate, schlanke Schreib-Implementierungen direkt auf den Geräten.

## Beziehung zu `python-osf`

`osfdata` ist der moderne Nachfolger des bestehenden Pakets [`python-osf`](https://github.com/optimeas/python-osf). `python-osf` ist eine reine Python-Implementierung, die ausschließlich OSF4 lesen kann und bei großen Dateien deutlich langsamer ist. `osfdata` bietet darüber hinaus:

- vollständige OSF4- und OSF5-Unterstützung (Lesen *und* Schreiben),
- erheblich höhere Leistung durch das Rust-Fundament,
- vollständige Abdeckung aller Datentypen, einschließlich `binary`, `gpslocation` und vorzeichenloser Ganzzahlen,
- Konformität mit der aktuellen Spezifikations-Revision (2026-05-04),
- transparente OSFZ-Dekompression für sowohl zlib- als auch gzip-komprimierte Dateien.

`python-osf` wird zu einem späteren Zeitpunkt als veraltet markiert, sobald `osfdata` in der Praxis alle Anwendungsfälle abdeckt.

## Unterstützte Plattformen

`osfdata` wird als vorkompiliertes Binärpaket (Wheel) für die folgenden Plattformen verteilt:

| Plattform | Architektur | Python-Versionen |
|---|---|---|
| Linux | x86_64 | 3.9 – 3.13 |
| Linux | aarch64 (ARM 64-Bit) | 3.9 – 3.13 |
| macOS | x86_64 (Intel) | 3.9 – 3.13 |
| macOS | arm64 (Apple Silicon) | 3.9 – 3.13 |
| Windows | x86_64 | 3.9 – 3.13 |

Auf allen unterstützten Plattformen läuft die Installation ohne Compiler oder weitere Werkzeuge. Auf exotischen Plattformen (FreeBSD, Windows-on-ARM, ältere Linux-Distributionen ohne manylinux-Kompatibilität) ist eine Installation aus der Source-Distribution möglich, erfordert dort aber eine lokale Rust-Toolchain.

## Installation

`osfdata` lässt sich mit beiden gängigen Python-Paketmanagern installieren.

### Mit pip

```bash
pip install osfdata
```

### Mit uv

```bash
uv pip install osfdata
```

`uv` ist ein neuerer, deutlich schnellerer Paketmanager, der `pip` und `venv` kombiniert. Wer ihn noch nicht kennt: Installation und Dokumentation unter [docs.astral.sh/uv](https://docs.astral.sh/uv).

### Aus der Vorab-Version auf TestPyPI

Während der Stabilisierungsphase wird `osfdata` zunächst auf TestPyPI veröffentlicht. Installation von dort:

```bash
pip install --index-url https://test.pypi.org/simple/ \
            --extra-index-url https://pypi.org/simple/ \
            osfdata
```

Der zweite Index-Verweis ist nötig, weil TestPyPI nicht alle Abhängigkeiten (NumPy etc.) bereitstellt — pip muss für die regulär auf PyPI nachschlagen können.

## Import und erstes Beispiel

Nach der Installation wird das Paket unter dem kurzen Namen `osf` importiert (der Verteilungsname `osfdata` wird auf PyPI verwendet, der Importname ist davon unabhängig — ein in Python übliches Muster, vergleichbar mit `scikit-learn` ↔ `import sklearn`).

```python
import osf

mgr = osf.load("messung.osf")
print(f"Datei enthält {len(mgr)} Kanäle")

temp = mgr.channel("Sensor/Temperatur")
samples = temp.samples()      # NumPy-Array, dtype passt zum OSF-Datentyp
zeitstempel = temp.timestamps_ns()
```

Damit ist das Wesentliche schon getan: Datei laden, Kanal über den Namen ansprechen, Werte als NumPy-Array erhalten.

## Überblick über die API

Die Bibliothek besteht aus wenigen, klaren öffentlichen Bausteinen. Alle weiteren Details — etwa wie ein Kanal mit mehreren Segmenten gehandhabt wird oder wie zeitgestempelte und äquidistante Daten unterschieden werden — ergeben sich beim Arbeiten mit den hier aufgeführten Objekten.

### Funktionen auf Modul-Ebene

| Funktion | Zweck |
|---|---|
| `osf.load(path)` | Lädt eine OSF- oder OSFZ-Datei und gibt einen `DataManager` zurück. Erkennt das Format automatisch. |
| `osf.save(manager, path)` | Schreibt einen `DataManager` als OSF5-Datei. |

### Klasse `DataManager`

Repräsentiert eine geladene OSF-Datei mit allen Kanälen und Metadaten.

| Attribut / Methode | Beschreibung |
|---|---|
| `len(mgr)` | Anzahl der Kanäle. |
| `mgr.channels` | Liste aller Kanäle (`list[Channel]`). |
| `mgr.channel(name)` | Kanal über den Namen ansprechen. Gibt `None` zurück, wenn nicht vorhanden. |
| `mgr.channel_by_index(i)` | Kanal über den numerischen Index ansprechen. |
| `mgr.stats` | `ReaderStats`-Objekt mit Statistiken zum Lesevorgang. |

### Klasse `Channel`

Ein einzelner Kanal mit Metadaten und Werten. Drei Spielarten — äquidistant, zeitgestempelt-numerisch, zeitgestempelt-variabel (für Strings und Binärdaten) — werden über dieselbe API angesprochen.

| Attribut / Methode | Beschreibung |
|---|---|
| `ch.name` | Kanalname (häufig hierarchisch, z. B. `"Motor/Drehzahl"`). |
| `ch.index` | Numerischer Kanalindex in der Datei. |
| `ch.data_type` | Datentyp als String (`"double"`, `"int32"`, `"string"` …). |
| `ch.channel_type` | `"equidistant"`, `"timestamped"` oder `"variable"`. |
| `ch.sample_count` | Anzahl der Samples. |
| `ch.physical_unit` | Physikalische Einheit (sofern angegeben). |
| `ch.is_empty` | True, wenn der Kanal keine Daten enthält. |
| `ch.samples()` | Werte als NumPy-Array. |
| `ch.timestamps_ns()` | Zeitstempel in Nanosekunden seit Epoch als `int64`-NumPy-Array. |
| `ch.segments` | Liste der Segmente (nur bei äquidistanten Kanälen aussagekräftig). |

### Klasse `Segment`

Beschreibt einen Abschnitt eines äquidistanten Kanals — etwa, wenn eine Aufzeichnung durch Trigger-Ereignisse oder Driftkorrekturen in mehrere Phasen aufgeteilt ist.

| Attribut | Beschreibung |
|---|---|
| `seg.start_timestamp_ns` | Anfangszeitpunkt des Segments in Nanosekunden seit Epoch. |
| `seg.sample_rate_hz` | Abtastrate innerhalb dieses Segments in Hertz. |
| `seg.sample_count` | Anzahl der Samples in diesem Segment. |

### Klasse `ReaderStats`

Diagnose-Informationen zum letzten Ladevorgang.

| Attribut | Beschreibung |
|---|---|
| `stats.compressed` | True, wenn die Datei OSFZ-komprimiert war. |
| `stats.compression_format` | `"gzip"`, `"zlib"` oder `None`. |
| `stats.channels_total` | Anzahl der Kanäle in der Datei. |
| `stats.blocks_total` | Anzahl der gelesenen Datenblöcke. |
| `stats.elapsed_ms` | Lade-Zeit in Millisekunden. |
| `stats.file_size_bytes` | Dateigröße auf der Festplatte. |

### Klasse `WriterBuilder`

Baut OSF5-Dateien aus Kanal-Definitionen und Sample-Daten auf. Der Builder akzeptiert Kanäle nacheinander, jeder Kanal kann mehrere Segmente oder Sample-Blöcke aufnehmen.

```python
b = osf.WriterBuilder().creator("messsystem-v1").tag("vortest")

idx = b.add_channel(
    name="Sensor/Druck",
    data_type="double",
    channel_type="scalar",
    physical_unit="bar",
)

import numpy as np
werte = np.array([1.013, 1.015, 1.014, 1.012], dtype=np.float64)
b.add_equidistant_segment(
    idx,
    start_ns=1_700_000_000_000_000_000,
    sample_rate_hz=1.0,
    values=werte,
)

b.write_to_file("ausgabe.osf")
```

Die wichtigsten Methoden:

| Methode | Zweck |
|---|---|
| `b.creator(s)` / `b.tag(s)` / `b.reason(s)` | Datei-Metadaten setzen. |
| `b.location(lat, lon, alt)` | Geografische Position der Aufzeichnung. |
| `b.add_channel(...)` | Neuen Kanal definieren, gibt den Kanal-Index zurück. |
| `b.add_equidistant_segment(idx, start_ns, sample_rate_hz, values)` | Äquidistantes Segment hinzufügen (nur `float`/`double`). |
| `b.add_timestamped_samples(idx, ts_ns, values)` | Numerisch-zeitgestempelte Samples hinzufügen. |
| `b.add_string_samples(idx, ts_ns, values)` | String-Samples mit Zeitstempel hinzufügen. |
| `b.add_binary_samples(idx, ts_ns, values)` | Binärdaten-Samples (z. B. Bilder, Audio) hinzufügen. |
| `b.write_to_file(path)` | Die zusammengetragenen Daten in eine Datei schreiben. |

## Hinweise zur Verwendung

**Zeitstempel.** Alle Zeitangaben sind 64-Bit-Ganzzahlen in Nanosekunden seit Unix-Epoch (UTC). Diese Genauigkeit deckt sowohl hochfrequente Schwingungsmessungen als auch langsame Prozessdaten ab, ohne unterschiedliche Zeit-Datentypen zu erfordern. Eine optionale Konvertierungsschicht zu Pythons `datetime`-Typ ist für eine spätere Version geplant.

**NumPy-Datentypen.** Der dtype des zurückgegebenen NumPy-Arrays entspricht direkt dem OSF-Datentyp: `double` → `float64`, `int32` → `int32`, `bool` → `bool` und so weiter. Es findet keine implizite Konvertierung statt — wenn der Kanal `int16` enthält, ist auch das NumPy-Array `int16`.

**Speicherverwaltung.** Numerische Sample-Arrays werden zwischen Rust und Python ohne Kopie übertragen. Das macht das Lesen großer Kanäle (mehrere Millionen Samples) selbst auf bescheidener Hardware sehr schnell.

**Fehlerbehandlung.** Alle Funktionen werfen `osf.OsfError` (eine Unterklasse von `Exception`), wenn etwas schiefgeht — Datei nicht gefunden, ungültiges Format, unbekannter Datentyp. Das Verhalten beim Auftreten unbekannter oder beschädigter Datenblöcke folgt dem Best-Effort-Prinzip: bis zum letzten gut lesbaren Block werden Daten geliefert, danach wird sauber abgebrochen.

## Anwendungsbeispiele

Ausführliche Beispielnotebooks und Skripte folgen in einer separaten Sektion. Geplante Themen:

- Schnelles Erkunden einer unbekannten OSF-Datei
- Migration von OSF4 nach OSF5
- Filterung und Zusammenführung mehrerer Aufnahmen
- Übergang zu pandas für tabellarische Auswertung
- Integration in PyTorch-Datasets

## Quellcode und weiterführende Informationen

- Das Paket auf PyPI: [pypi.org/project/osfdata](https://pypi.org/project/osfdata/)
- Quellcode auf GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf), Verzeichnis `implementations/python/`
- Build- und Release-Prozess: siehe [`BUILD.md`](https://github.com/optimeas/osf/blob/main/implementations/python/BUILD.md) im Repository
- Format-Spezifikation: siehe das Kapitel [OSF-Format](../osf_general.md) in dieser Dokumentation
