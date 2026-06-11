---
title: Architektur
description: Schichtenmodell, Module, Datenmodell und Designentscheidungen der OSF-C++-Implementierung
sidebar_position: 1
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - Architektur
  - Result
  - DataManager
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
---

# Architektur der C++-Implementierung

Diese Seite beschreibt den inneren Aufbau der C++-Implementierung:
das Schichtenmodell, die Module und ihr Zusammenspiel, das Datenmodell
und die zentralen Designentscheidungen. Sie richtet sich an Entwickler,
die die Bibliothek einbinden **und** an solche, die daran mitarbeiten
wollen. Einen schnellen Überblick gibt die
[Übersichtsseite](../cpp.md); die Detail-Themen Lesen, Schreiben,
Fehlerbehandlung, C-ABI und Build haben eigene Seiten.

## Leitlinien

Die Implementierung folgt vier Grundsätzen (festgehalten in
[DECISIONS §20](https://github.com/optimeas/osf/blob/main/DECISIONS.md)):

1. **Eigenständiges C++17** — kein FFI auf den Rust-Kern, keine
   Portierung aus C. Idiomatisches modernes C++ mit der Rust-Implementierung
   als *Verhaltensreferenz* (gleiche Semantik, nicht gleiche Struktur).
2. **Exception-freier Kern.** Jede Operation, die scheitern kann, gibt
   `osf::Result<T>` zurück (ein `tl::expected<T, osf::Error>`).
   Exceptions gibt es nur in der opt-in-Schicht `osf::throwing`.
3. **Best-Effort beim Lesen.** Abgeschnittene Dateien (Stromausfall
   beim Embedded-Schreiber) liefern alle vollständig lesbaren Blöcke
   statt eines Fehlers; unbekannte zukünftige Datentypen werden
   übersprungen statt das Laden abzubrechen.
4. **Schlanke Abhängigkeiten.** Drei vendorte Header-Bibliotheken
   (`tl::expected`, `nlohmann/json`, `pugixml`) plus zlib (FetchContent
   oder System). Keine Boost-, keine Qt-Abhängigkeit.

## Schichtenmodell

```mermaid
flowchart TB
    subgraph CONV ["Komfort-Schichten (opt-in)"]
        THR["osf::throwing<br/>Exception-Wrapper, header-only"]
        CAPI["osf-c<br/>C99-ABI, eigene Shared Library"]
    end
    subgraph HIGH ["Hohe Ebene"]
        DM["DataManager<br/>typisierte Kanäle"]
        BW["BlockWriter"]
        SW["StreamingWriter"]
        SVG["StaleValueGuard"]
    end
    subgraph LOW ["Niedrige Ebene"]
        BR["BlockReader<br/>roher Block-Strom"]
        HDR["parse_magic_header"]
        MB["parse_metablock_json / _xml"]
        CMP["DecompressingIStream"]
    end
    subgraph FOUND ["Fundament"]
        ERR["Error / Result&lt;T&gt;"]
        TYP["DataType / ChannelType"]
        BLK["Block-Datenmodell"]
    end
    THR --> DM
    THR --> BW
    CAPI --> DM
    CAPI --> BW
    SVG --> SW
    DM --> BR
    DM --> CMP
    BR --> MB
    MB --> HDR
    HIGH --> FOUND
    LOW --> FOUND
```

Die meisten Anwendungen arbeiten ausschließlich auf der hohen Ebene
(`DataManager` zum Lesen, einer der beiden Writer zum Schreiben). Die
niedrige Ebene ist öffentlich und stabil — wer streamend lesen oder
eigene Werkzeuge bauen will, benutzt `BlockReader` direkt.

## Module und Verantwortlichkeiten

| Header | Inhalt | Schicht |
|---|---|---|
| `osf/error.hpp` | `Error` (Code + Message), `Result<T>` | Fundament |
| `osf/types.hpp` | `DataType`, `ChannelType`, `SpectrumType` + Parser | Fundament |
| `osf/header.hpp` | Magic-Header: `OsfVersion`, `MagicHeader`, `parse_magic_header` | Niedrig |
| `osf/metablock.hpp` | `MetaBlock`/`FileInfo`/`Channel`/`Info`; JSON- und XML-Parser; JSON-Serialisierung | Niedrig |
| `osf/block.hpp` | Block-Datenmodell: `Block`, `BlockKind`, Payload-Varianten, Control-Byte-Decoder | Fundament |
| `osf/reader.hpp` | `BlockReader` — Iterator über den Block-Strom | Niedrig |
| `osf/stats.hpp` | `ReaderStats` / `ChannelStats` — Lese-Telemetrie | Niedrig |
| `osf/compression.hpp` | `DecompressingIStream`, `detect_compression` — transparentes OSFZ | Niedrig |
| `osf/data_channel.hpp` | `DataChannel`-Variante (Equidistant / Timestamped / Variable), `Segment`, Flat-Accessoren | Hoch |
| `osf/manager.hpp` | `DataManager` — Laden + typisierte Kanalliste | Hoch |
| `osf/streaming_writer.hpp` | `StreamingWriter` + `ChannelDef` | Hoch |
| `osf/block_writer.hpp` | `BlockWriter` + freie Funktionen `write_to_file` / `write_to` | Hoch |
| `osf/stale_value_guard.hpp` | `StaleValueGuard` — Frische-Schicht über `StreamingWriter` | Hoch |
| `osf/binary_sample.hpp` | `BinarySample` — nicht-besitzende Byte-Sicht (Span-Ersatz) | Fundament |
| `osf/throwing.hpp` | `osf::Exception`, `throwing::unwrap/load/write_to_file` — **nicht** im Umbrella | Komfort |
| `osf/c_api.h` | reines C99-ABI der Bibliothek `osf-c` — **nicht** im Umbrella | Komfort |
| `osf/osf.hpp` | Umbrella-Header (alles außer `throwing.hpp` und `c_api.h`) | — |
| `osf/version.hpp` | generiert; `osf::version()` und `OSF_VERSION_*` | Fundament |

Private Implementierungsbausteine (unter `src/`, nicht installierbar):
`block_encode.{hpp,cpp}` (OSF5-Block-Encoder), `writer_common.{hpp,cpp}`
(Chunking-Mathematik + Metablock-Assembly), `durable_file.{hpp,cpp}`
(RAII-Datei mit `fsync`), `binary_io.hpp` (Little-Endian-Helfer).
Details siehe [Interna](interna.md).

## Drei Datenmodelle — wer sieht was

Die Bibliothek hat bewusst drei Repräsentationen derselben Daten, je
nach Abstraktionsebene:

```mermaid
flowchart LR
    DISK[("Datei<br/>(Bytes)")] -->|BlockReader| BLOCKS["osf::Block<br/>pro Block,<br/>Stream-Reihenfolge"]
    BLOCKS -->|DataManager| CHANNELS["osf::DataChannel<br/>pro Kanal,<br/>Blockgrenzen aufgelöst"]
    META[("Metablock")] -->|Parser| MB["osf::MetaBlock<br/>Definitionen"]
    MB -.->|Kanal-Definitionen| BLOCKS
    MB -.->|Meta-Felder| CHANNELS
```

1. **`osf::MetaBlock`** (`metablock.hpp`) — die *Definitionen*:
   Datei-Metadaten (`FileInfo`), Kanal-Definitionen (`osf::Channel`)
   und optionale `Info`-Einträge. OSF4 (XML) und OSF5 (JSON)
   unterscheiden sich nur in der Serialisierung; beide Parser füllen
   dasselbe Modell symmetrisch.
2. **`osf::Block`** (`block.hpp`) — die *Stream-Sicht*: ein dekodierter
   Block mit Kanalindex und `BlockKind`-Variante (`StartData`,
   `ContinuedData`, `AbsTimestampData`, `ContinuedRelStampData`,
   `Skipped`). Payloads sind ausgepackte, typisierte Vektoren — kein
   Zero-Copy (Blöcke sind KB bis wenige MB groß; die einfache
   Lebensdauer-Semantik wiegt die Allokation auf).
3. **`osf::DataChannel`** (`data_channel.hpp`) — die *Kanal-Sicht*:
   eine `std::variant` über drei Speicher-Layouts, weil sich die
   Speicherung tatsächlich unterscheidet:

   | Variante | Speicherung |
   |---|---|
   | `EquidistantChannel` | flacher Sample-Vektor + `std::vector<Segment>` |
   | `TimestampedChannel` | parallele Vektoren `timestamps_ns` + `values` |
   | `VariableChannel` | Timestamps + String- **oder** Binary-Samples |

**Namenshinweis:** `osf::Channel` ist die *Kanal-Definition* aus dem
Metablock; `osf::DataChannel` sind die *zusammengesetzten Samples*. Die
Rust-Referenz trennt beide per Modul; in C++ teilen sie sich den
Namespace `osf`, daher die unterschiedlichen Namen.

## Namens- und API-Konventionen

- **Typen** in PascalCase (`DataManager`, `BlockReader`), **Methoden
  und freie Funktionen** in snake_case (`load_from_file`,
  `channel_name`) — die im Projekt dokumentierte Konvention.
- Diskriminatoren in Varianten heißen `kind` (`BlockKind`,
  `SkipReason::Kind`, `VariableValueRef::Kind`).
- Alles Fehlbare gibt `Result<T>` zurück und ist `[[nodiscard]]`.
- Konstruktion über statische Fabriken (`DataManager::load_from_file`)
  oder Builder-artige Konfiguration (Writer: `set_*` → `add_channel` →
  Schreibphase).
- Fluent-Setter am `BlockReader` (`with_capture_skipped_payload`,
  `with_file_size`) geben `BlockReader&` zurück.
- Zeitstempel sind durchgehend `std::int64_t` **Nanosekunden seit der
  Unix-Epoche (UTC)**; Abtastraten `double` in Hz.

## Zentrale Designentscheidungen

### `Result<T>` statt Exceptions im Kern

Die Bibliothek zielt auch auf Embedded- und Industrie-Codebasen, in
denen Exceptions deaktiviert oder unerwünscht sind. Der Kern wirft
deshalb nie; `tl::expected` (vendort, CC0) liefert die Monade. Wer
Exceptions bevorzugt, nimmt [`osf::throwing`](fehlerbehandlung.md) —
eine dünne, header-only Schicht, die bewusst **nicht** in den
Umbrella-Header aufgenommen wurde, damit Kern-Nutzer keine
Exception-Maschinerie einziehen.

### Best-Effort und Vorwärtskompatibilität

Reale OSF-Dateien entstehen auf Geräten, die jederzeit die
Stromversorgung verlieren können, und mit Spec-Ständen, die der
Leser noch nicht kennt. Daraus folgen drei Verhaltensregeln:

- **Trunkierung ist kein Fehler.** Endet die Datei mitten im Block,
  liefert der `BlockReader` alle vollständigen Blöcke, erhöht
  `stats().blocks_truncated` auf 1 und beendet die Iteration sauber.
- **Unbekanntes wird übersprungen, nicht verschluckt.** Kanäle mit
  unbekanntem (zukünftigem) Datentyp parsen als
  `DataType::Unsupported`; ihre Blöcke erscheinen als
  `BlockKind::Skipped` (Payload-Bytes werden konsumiert, damit der
  Strom ausgerichtet bleibt). Die Original-Schreibweise bleibt auf
  `Channel::data_type_raw` erhalten.
- **Entfernte Spec-Elemente sind harte Fehler.** Datentypen, die die
  Spec-Revision 2026-05-04 entfernt hat (`pair`, `triple`, `candata`,
  `gpsdata`), werden mit `Error::Code::RemovedInSpec` abgelehnt —
  ihr Payload-Layout lässt sich aus einem aktuellen Build nicht
  reproduzieren, stilles Raten wäre Datenkorruption.

### Zwei Writer statt einem

`StreamingWriter` (Embedded: `fsync` pro Block, konstanter Speicher,
ausfallsicher) und `BlockWriter` (Analyst: sammelt im Speicher, emittiert
am Ende, kann `sizeoflengthvalue` automatisch anheben) haben
unvereinbare Invarianten — ein gemeinsamer Writer hätte beide Profile
verwässert. Gemeinsame Bausteine (Chunking, Metablock-Assembly) leben
in `src/writer_common.*`. Details auf der Seite [Schreiben](schreiben.md).

### Transparentes OSFZ nur beim Lesen

OSFZ (= gzip- oder zlib-komprimiertes OSF) wird beim **Lesen**
transparent erkannt und dekomprimiert (`DecompressingIStream` vor dem
Magic-Header-Parse). Beim **Schreiben** komprimiert die Bibliothek
bewusst nie inline (DECISIONS §12): Kompression ist ein nachgelagerter
Schritt nach dem Datei-Abschluss, damit Schreib- und
Kompressions-Fehlermodi entkoppelt bleiben.

### Thread-Sicherheit

| Klasse | Vertrag |
|---|---|
| `DataManager` (geladen) | unveränderlich → beliebig parallel lesbar |
| `BlockReader` | nicht thread-safe; eine Instanz pro Thread |
| `StreamingWriter` / `BlockWriter` / `StaleValueGuard` | nicht thread-safe; Aufrufe extern serialisieren (z. B. `std::mutex`) |
| verschiedene Writer auf verschiedene Dateien | parallel unproblematisch |
| `osf-c` | `osf_last_error_message()` ist thread-lokal; Handles nicht über Threads teilen, ohne zu serialisieren |

## Verzeichnislayout

```
implementations/cpp/
├── CMakeLists.txt           — Projekt, Optionen, Targets
├── BUILD.md                 — Bauanleitung (EN)
├── cmake/                   — CompilerWarnings.cmake, version.hpp.in
├── include/osf/             — öffentliche Header (API-Fläche)
├── src/                     — Implementierung + private Header
├── tests/
│   ├── unit/                — GoogleTest-Units (synthetische Daten)
│   ├── integration/         — Tests gegen examples/*.osf(z)
│   └── c_api/               — reiner C99-Test für osf-c
├── examples/                — inspect, dump, write, copy
└── third_party/             — tl::expected, nlohmann/json, pugixml (vendort)
```

## Weiterführend

- [Lesen — DataManager, DataChannel, BlockReader, OSFZ](lesen.md)
- [Schreiben — StreamingWriter, BlockWriter, StaleValueGuard](schreiben.md)
- [Fehlerbehandlung — Result, Error-Katalog, throwing](fehlerbehandlung.md)
- [C-ABI — osf-c für C, C#, OCX](c-abi.md)
- [Bauen & Einbinden — CMake, Optionen, CI](bauen.md)
- [Kochbuch — Rezepte für typische Aufgaben](kochbuch.md)
- [Interna — Encoder, Chunking, Builder-Zustandsmaschine](interna.md)
