---
title: Fehlerbehandlung
description: Result<T> und osf::Error im exception-freien Kern, der vollständige Fehlercode-Katalog und die opt-in-Schicht osf::throwing
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - Result
  - Error
  - Exceptions
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Fehlerbehandlung

Der Kern der C++-Implementierung ist **exception-frei**: Jede
Operation, die scheitern kann, gibt `osf::Result<T>` zurück — ein
`tl::expected<T, osf::Error>`. Wer Exceptions bevorzugt, legt die
dünne opt-in-Schicht `osf::throwing` darüber. Beide Stile lassen sich
mischen.

## `osf::Error` und `osf::Result<T>`

```cpp
struct Error {
    Code        code;     // stabile Kategorie — hierauf verzweigen
    std::string message;  // menschenlesbares Detail — nur zur Anzeige
};

template <typename T>
using Result = tl::expected<T, Error>;
```

Grundidiom:

```cpp
auto r = osf::DataManager::loadFromFile(pfad);
if (!r) {
    log("Laden fehlgeschlagen [{}]: {}",
        osf::errorCategoryName(r.error().code),   // stabiler Name, z. B. "io_error"
        r.error().message);
    return;
}
osf::DataManager const& mgr = *r;   // oder r.value()
```

Regeln:

- **Auf `code` verzweigen, `message` nur anzeigen.** Der Wortlaut der
  Message ist nicht Teil der API und darf sich ändern.
- Alle `Result`-Rückgaben sind `[[nodiscard]]` — der Compiler mahnt
  ignorierte Fehler an.
- `errorCategoryName(code)` liefert einen stabilen String-Bezeichner
  für Logs (statisch, kein Besitz).
- `Result<void>` signalisiert reine Erfolg/Fehler-Operationen
  (`writer.start()`, `writeToFile(...)`, …): `if (!r) …`.

## Fehlercode-Katalog

### Eingabe- und API-Fehler

| Code | Bedeutung | Typische Quelle |
|---|---|---|
| `InvalidArgument` | API-Vorbedingung verletzt: ungültige `ChannelDef`, unbekannter Kanalindex am Writer, `count == 0`, Writer in falscher Lebenszyklus-Phase, nicht-positive Abtastrate | Writer |
| `IoError` | Datei/Stream-Fehler: nicht öffnenbar, Lese-/Schreibfehler, fsync-Fehler | überall |
| `NotFound` | reserviert für Lookup-APIs (Kanal-Lookups geben stattdessen `nullptr`) | — |
| `Unknown` | Fallback ohne speziellere Kategorie; Code eines default-konstruierten `Error` | — |
| `ParseError` | generischer Parse-Fehler, wenn keine speziellere Kategorie passt (selten) | Parser |

### Magic-Header

| Code | Bedeutung |
|---|---|
| `InvalidMagicHeader` | Erste Zeile ist kein wohlgeformter OSF-Header — meist „keine OSF-Datei" |
| `UnsupportedVersion` | Header parsebar, aber der Bezeichner ist keine der vier akzeptierten Schreibweisen (`OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) |
| `MagicHeaderTooLong` | Kein Zeilenumbruch innerhalb von 128 Bytes — sicher keine OSF-Datei |

### Metablock

| Code | Bedeutung |
|---|---|
| `InvalidMetablock` | Strukturfehler: Pflichtfeld fehlt, Zahl nicht parsebar, `sizeOfLengthValue` ≠ 2/4 (würde sonst jeden Block-Read still korrumpieren), Wurzelelement falsch |
| `JsonParseError` | OSF5: Metablock-Body ist kein gültiges JSON (Parser-Diagnose in `message`) |
| `XmlParseError` | OSF4: Metablock-Body ist kein wohlgeformtes XML (Diagnose + Byte-Offset in `message`) |
| `RemovedInSpec` | Datei verwendet einen mit Spec-Rev 2026-05-04 **entfernten** Datentyp (`pair`, `triple`, `candata`, `gpsdata`). Wird hart abgelehnt — das alte Payload-Layout ist aus einem aktuellen Build nicht reproduzierbar; die Message nennt den Ersatz |

### Block-Strom

| Code | Bedeutung |
|---|---|
| `UnknownChannelIndex` | Block referenziert einen Kanalindex ohne Metablock-Definition. Ohne Definition ist die Breite des Längenfelds unbekannt → Korruptionssignal, harter Abbruch |
| `InvalidBlock` | Payload strukturell defekt (falsche Länge für den Datentyp, äquidistanter Block auf String-Kanal, Sample sprengt die Blockkapazität des Streaming-Writers, …) |
| `ChannelMixedBlockTypes` | Ein Kanal liefert sowohl äquidistante (`bcStartData`/`bcContinuedData`) als auch timestamped Blöcke — von der Spec verboten |
| `ContinuedDataWithoutStart` | `bcContinuedData` ohne vorausgehendes `bcStartData` — ohne offenes Segment hat die Fortsetzung keine Zeitbasis |
| `RelStampWithoutAnchor` | `bcContinuedRelStampData` ohne vorherigen absoluten Zeitstempel — die Deltas haben keinen Anker |
| `DataTypeMismatch` | Angeforderter Typ ≠ gespeicherter Typ (z. B. `asDoublesFlat` auf einem `int32`-Kanal, `asStrings` auf einem Binary-Kanal) |

### Was bewusst **kein** Fehler ist

| Situation | Verhalten |
|---|---|
| Datei endet mitten im Block (Stromausfall) | Best-Effort: alle vollständigen Blöcke werden geliefert, `stats.blocksTruncated = 1`, Iteration endet sauber |
| Unbekannter zukünftiger Datentyp/Kanaltyp | Kanal parst als `Unsupported`, Blöcke werden als `Skipped` ausgerichtet konsumiert, restliche Kanäle laden normal |
| Deprecated/reservierte Control-Bytes (alte Felddateien) | `BlockKind::Skipped` mit `SkipReason`, Zähler in `stats` |
| Deprecated Kanal-Felder (`scale`, `offset`, `physicalunit1..3`, …) | stillschweigend toleriert und ignoriert (reale Felddateien tragen sie alle) |
| Kanal-Lookup ohne Treffer | `nullptr`, kein `Result` |

## Die werfende Schicht — `osf::throwing`

Header-only, opt-in (`#include <osf/throwing.h>`), bewusst **nicht**
im Umbrella-Header `<osf/osf.h>` und nicht in die Bibliothek
einkompiliert. Wer sie nie einbindet, zieht keinerlei
Exception-Maschinerie ein.

```cpp
#include <osf/throwing.h>

try {
    auto mgr = osf::throwing::load("messung.osf");      // DataManager oder wirft
    osf::throwing::writeToFile(mgr, "kopie.osf");

    osf::StreamingWriter w{pfad};
    auto ch = osf::throwing::unwrap(w.addChannel(def)); // Result<T> -> T oder wirft
    osf::throwing::unwrap(w.start());
    osf::throwing::unwrap(w.writeTimestampedSample<double>(ch, ts, wert));
    osf::throwing::unwrap(w.close());
} catch (osf::Exception const& e) {
    // e.what()  — Message (oder Kategorie-Name, wenn Message leer)
    // e.code()  — Error::Code für programmatisches Verzweigen
    // e.error() — der vollständige strukturierte osf::Error
}
```

Die Schicht besteht aus genau drei Bausteinen:

| Baustein | Zweck |
|---|---|
| `osf::Exception : std::runtime_error` | trägt den vollständigen `osf::Error`; lebt in `osf`, nicht `osf::throwing` |
| `osf::throwing::unwrap(Result<T>)` | universeller Adapter: Wert herausziehen oder werfen. Funktioniert mit **jedem** `Result` des Kerns, auch von Writer-Methoden — deshalb braucht es keine werfenden Wrapper pro Methode |
| `osf::throwing::load / writeToFile / writeTo` | werfende Pendants der häufigsten High-Level-Operationen |

## Stilwahl in der Praxis

- **Bibliotheks-/Embedded-Code, Hot-Paths, Codebasen mit
  `-fno-exceptions`:** beim `Result`-Kern bleiben.
- **Anwendungscode mit vorhandener Exception-Strategie:** `throwing`
  an der Außenkante benutzen; intern bleibt alles `Result`.
- **Gemischt:** `unwrap` punktuell dort, wo ein Fehler ohnehin nur
  propagiert würde — z. B. in einem CLI-`main`, das oben einen
  `try/catch` hat.

## Sonderfall Writer: Sticky Errors

Der `StreamingWriter` merkt sich den ersten I/O-Fehler („Broken"-
Zustand) und gibt ihn bei **jedem** Folgeaufruf zurück, einschließlich
`close()`. In Schreibschleifen genügt deshalb ein Fehler-Check pro
Iteration; die Ursache geht auch dann nicht verloren, wenn erst am
Ende ausgewertet wird.

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
