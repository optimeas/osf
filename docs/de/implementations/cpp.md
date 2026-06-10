---
title: C++-Implementierung
description: Eigenständige C++17-Implementierung des Open Streaming Format — Reader, DataManager, beide Writer, transparentes OSFZ und C-ABI
sidebar_position: 5
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - C++17
  - CMake
  - C-ABI
  - osf-c
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
---

🇬🇧 [English version](../../en/implementations/cpp.md)

# C++-Implementierung

Eine **eigenständige C++17-Implementierung** des Open Streaming Format —
kein FFI, keine Rust-Abhängigkeit, idiomatisches modernes C++. Sie liest
`.osf`- und `.osfz`-Dateien und schreibt OSF5. Die Implementierung ist als
Parallel-Implementierung zum Rust-Kern entstanden, nicht als Portierung.

## Funktionsumfang

Die geplante Implementierungs-Reihenfolge (siehe
[DECISIONS §20](https://github.com/optimeas/osf/blob/main/DECISIONS.md)) ist
**vollständig abgeschlossen**. Lese- und Schreibpfad sind durch eine
GoogleTest-/ctest-Suite abgedeckt (0 Warnungen unter MSVC `/W4 /permissive-`),
und CI baut und testet auf **Linux, macOS und Windows**.

**Lesepfad**

- Magic-Header-Parser; OSF5-JSON- und OSF4-XML-Metablock-Parser
- Block-Stream-Reader und der typisierte `DataManager` (einheitlicher
  In-Memory-Reader mit typisierten Kanälen)
- Transparente **OSFZ**-Dekompression (gzip/zlib) — `.osf` und `.osfz`
  werden über dieselbe API gelesen

**Schreibpfad (OSF5)**

- `StreamingWriter` — eingebettet, Sample für Sample, `fsync` pro Block
  (ausfallsicher bei Stromverlust), konstanter Speicherbedarf
- `BlockWriter` — analystenfreundlich, sammelt im Speicher und schreibt
  die komplette Datei am Ende; passt `sizeoflengthvalue` bei Bedarf
  automatisch von 2 → 4 an
- `StaleValueGuard` — optionale Frische-Schicht, die den letzten Wert
  inaktiver Kanäle erneut ausgibt

**Komfort und Anbindung**

- Eine **werfende Komfort-Schicht** (`osf::throwing`) über dem
  `Result<T>`-Kern für Aufrufer, die Exceptions bevorzugen
- Die **C-ABI-Bibliothek `osf-c`** (`osf/c_api.h`) — eine reine C99-Schicht
  für die sprachübergreifende Nutzung (DLL/Shared Object)

## Bauen — Schnellstart

```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

Plattform­spezifische Hinweise, CMake-Optionen und FAQ stehen in
[`BUILD.md`](https://github.com/optimeas/osf/blob/main/implementations/cpp/BUILD.md).

### CMake-Optionen

| Option | Standard | Wirkung |
|---|---|---|
| `OSF_BUILD_TESTS` | `ON` | GoogleTest/ctest-Suite bauen |
| `OSF_BUILD_C_API` | `OFF` | die C-ABI-Bibliothek `osf-c` (+ C-Test) mitbauen |
| `OSF_USE_SYSTEM_ZLIB` | `OFF` | System-zlib statt FetchContent verwenden |
| `OSF_WARNINGS_AS_ERRORS` | `OFF` | Warnungen als Fehler (`/WX` bzw. `-Werror`); in CI `ON` |
| `BUILD_SHARED_LIBS` | `OFF` | die Kern-Bibliothek als Shared Library bauen |

C++17 ist die fest definierte Sprachbaseline der Bibliothek. Der Wechsel auf C++20 oder höher ist ein bewusstes Library-Upgrade, keine Build-Option. Drittanbieter-Code (`tl::expected`,
`nlohmann/json`, `pugixml`) ist im Repository unter `third_party/`
mitgeliefert; zlib kommt per FetchContent oder System.

### Einbinden

Die Bibliothek exportiert zwei CMake-Ziele:

- `osf::osf` — die Kern-Bibliothek (Standard statisch; Dateiname `libosf.a`
  / `osf.lib`)
- `osf::headers` — ein INTERFACE-Ziel mit den öffentlichen Include-Pfaden

## API im Überblick

Der Kern ist **exception-frei**: Operationen, die scheitern können, geben
`osf::Result<T>` zurück (ein `tl::expected<T, osf::Error>`).

### Lesen

```cpp
#include <osf/manager.hpp>

auto result = osf::DataManager::load_from_file("messung.osf");  // auch .osfz
if (!result) {
    // result.error().message  —  strukturierter Fehler, keine Exception
    return;
}
osf::DataManager const& mgr = *result;

// Kanal über den Namen ansprechen (verpflichtend, DECISIONS §10)
if (osf::DataChannel const* ch = mgr.channel("Sensor.Temperatur")) {
    auto werte = ch->as_doubles_flat();   // typisierter Zugriff
}
```

Wer lieber mit Exceptions arbeitet, nutzt die opt-in-Schicht:

```cpp
#include <osf/throwing.hpp>

auto mgr = osf::throwing::load("messung.osf");   // wirft osf::Exception bei Fehler
```

### Schreiben (OSF5)

```cpp
#include <osf/block_writer.hpp>

osf::BlockWriter writer;
auto idx = writer.add_channel(/* Name, Datentyp, Kanaltyp, … */);
// … Samples zu idx hinzufügen …
writer.write_to_file("ausgabe.osf");
```

Für eingebettetes, ausfallsicheres Schreiben gibt es stattdessen den
`StreamingWriter` (`fsync` pro Block). Ein geladener `DataManager` lässt
sich mit der freien Funktion `osf::write_to_file(mgr, pfad)` direkt als
OSF5 zurückschreiben (Round-Trip / OSF4 → OSF5).

### C-ABI (`osf-c`)

Mit `-D OSF_BUILD_C_API=ON` entsteht zusätzlich die Shared Library
`osf-c` mit einer reinen C99-Schnittstelle (`osf/c_api.h`): opake Handles
(`osf_manager`, `osf_channel`), `osf_status`-Codes, eine thread-lokale
`osf_last_error_message()` und Copy-out-Reader für Zeitstempel und Werte —
plus `osf_write_to_file` für den Round-Trip-Schreibpfad. Keine C++-Exception
überschreitet die ABI-Grenze. Gedacht für die Anbindung aus C, C#/P-Invoke,
ActiveX/OCX und künftige Sprach-Bindings.

## Hinweise

- **Nur OSF5 wird geschrieben** (DECISIONS §6) — auch wenn die Quelle eine
  OSF4-Datei war.
- **Kein OSFZ-Output**: Kompression ist ein nachgelagertes Thema
  (DECISIONS §12); gelesen wird OSFZ transparent.
- **Best-Effort beim Lesen**: abgeschnittene Dateien liefern alle Daten bis
  zum letzten vollständig lesbaren Block, ohne Absturz.
- Die Bibliothek ist **Qt-neutral**; ein Qt-naher Zusatz kann später als
  eigener `integrations/`-Eintrag folgen.

## Quellcode und weiterführende Informationen

- Quellcode auf GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  Verzeichnis `implementations/cpp/`
- Bauanleitung: [`BUILD.md`](https://github.com/optimeas/osf/blob/main/implementations/cpp/BUILD.md)
- Architektur und phasenweiser Plan:
  [DECISIONS §20](https://github.com/optimeas/osf/blob/main/DECISIONS.md) und
  die C-ABI in §23
- Format-Spezifikation: Kapitel [OSF-Format](../osf_general.md)
