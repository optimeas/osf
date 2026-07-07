---
title: Lesen
description: OSF- und OSFZ-Dateien lesen — DataManager, DataChannel, BlockReader, ReaderStats und transparente Dekompression
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - DataManager
  - BlockReader
  - OSFZ
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Lesen

Die C++-Implementierung liest **OSF4**, **OSF5** und transparent
**OSFZ** (gzip/zlib) über dieselbe API. Es gibt zwei Lese-Ebenen:

- **`osf::DataManager`** — der Standardweg. Lädt die Datei komplett,
  setzt aus dem Block-Strom typisierte Kanäle zusammen und löst alle
  Blockgrenzen auf. Für Analyse, Export, Tooling.
- **`osf::BlockReader`** — die Stream-Ebene. Liefert Block für Block in
  Datei-Reihenfolge mit konstantem Speicherbedarf. Für sehr große
  Dateien, eigene Aggregationen und Spezialwerkzeuge.

## Schnellstart

```cpp
#include <osf/osf.h>

auto result = osf::DataManager::loadFromFile("messung.osf");  // auch .osfz
if (!result) {
    std::cerr << result.error().message << "\n";
    return 1;
}
osf::DataManager const& mgr = *result;

// Kanal über den Namen ansprechen (primäre Zugriffsform)
if (osf::DataChannel const* ch = mgr.channel("Sensor.Temperatur")) {
    if (auto werte = osf::asDoublesFlat(std::get<osf::TimestampedChannel>(*ch))) {
        for (auto const& [ts_ns, wert] : *werte) { /* … */ }
    }
}
```

## `DataManager`

### Laden

| Methode | Quelle | Hinweise |
|---|---|---|
| `DataManager::loadFromFile(path)` | Datei | OSF, OSFZ; ermittelt die Dateigröße für `stats` |
| `DataManager::loadFromStream(istream&)` | beliebiger `std::istream` | Strom muss am Dateianfang positioniert und (für die OSFZ-Erkennung) **seekbar** sein |

Beide Wege durchlaufen dieselbe Pipeline: OSFZ-Erkennung →
Magic-Header → Metablock-Parser (JSON oder XML) → `BlockReader` bis
EOF → Kanal-Zusammenbau. Das Ergebnis ist unveränderlich und darf von
beliebig vielen Threads gleichzeitig gelesen werden.

### Zugriff

```cpp
mgr.meta;                  // osf::MetaBlock  — FileInfo, Kanal-Definitionen, Infos
mgr.stats;                 // osf::ReaderStats — Telemetrie des Ladevorgangs
mgr.channels();            // std::vector<DataChannel> const& — Metablock-Reihenfolge
mgr.channel("a.b.c");      // DataChannel const* — nullptr, wenn unbekannt (Pflicht-Form)
mgr.channelByIndex(7);     // DataChannel const* — Index aus dem Metablock (optional)
```

`channel(name)` ist die primäre Zugriffsform;
`channelByIndex` ist Komfort. Beide geben `nullptr` statt eines
Fehlers zurück, weil „Kanal nicht vorhanden" beim Erkunden fremder
Dateien ein normaler Fall ist.

### Was beim Laden passieren kann

- **Trunkierte Datei:** kein Fehler. Alle vollständig lesbaren Blöcke
  landen in den Kanälen, `mgr.stats.blocksTruncated == 1`.
- **Unbekannter (zukünftiger) Datentyp:** Der Kanal wird aus der
  Kanalliste **weggelassen** (seine Blöcke wurden auf Reader-Ebene
  übersprungen); die Definition bleibt in `mgr.meta.channels` sichtbar,
  inklusive Original-Schreibweise in `dataTypeRaw`.
- **Strukturfehler:** `InvalidMetablock`, `UnknownChannelIndex`,
  `ChannelMixedBlockTypes` usw. brechen das Laden mit einem
  strukturierten Fehler ab — siehe
  [Fehlerbehandlung](error-handling.md).

## `DataChannel` — die typisierten Kanäle

`DataChannel` ist eine `std::variant` über drei Layouts:

```cpp
using DataChannel = std::variant<EquidistantChannel, TimestampedChannel, VariableChannel>;
```

### Gemeinsame Accessoren (freie Funktionen)

Für Variante-agnostischen Code gibt es freie Funktionen, die intern
`std::visit` benutzen:

```cpp
osf::channelIndex(ch);          // std::uint16_t
osf::channelName(ch);           // std::string const&
osf::channelDataType(ch);       // osf::DataType
osf::channelPhysicalUnit(ch);   // std::optional<std::string>
osf::channelDisplayName(ch);    // std::optional<std::string>
osf::channelSampleCount(ch);    // std::size_t (Summe über alle Segmente)
osf::channelIsEmpty(ch);        // bool
osf::channelMeta(ch);           // ChannelMeta const& (sekundäre Definitionfelder)
```

### `EquidistantChannel` — Segmente statt Zeitstempel

Äquidistante Kanäle speichern **keinen Zeitstempel pro Sample**.
Stattdessen: ein flacher Sample-Vektor (`NumericValues`, eine Variante
über alle numerischen Typen) plus eine Segmentliste. Jeder
`bcStartData`-Block der Datei öffnet ein Segment:

```cpp
struct Segment {
    std::int64_t startTimestampNs;  // absoluter Startzeitpunkt
    double       sampleRateHz;      // gilt bis zum nächsten Segment
    std::size_t  startIndex;        // erster Sample-Index im flachen Vektor
    std::size_t  sampleCount;       // Anzahl Samples dieses Segments
};
```

Sample `i` eines Segments liegt bei
`startTimestampNs + i * (1e9 / sampleRateHz)`. Lücken zwischen
Segmenten werden **nicht** interpoliert — eine Aufzeichnungspause
bleibt eine Pause.

Wer `(Zeitstempel, Wert)`-Paare braucht, ruft
`samplesVector()` auf (materialisiert; rekonstruiert die Zeitstempel
aus den Segmenten):

```cpp
auto const& eq = std::get<osf::EquidistantChannel>(*ch);
for (auto const& s : eq.samplesVector()) {
    // s.timestampNs, s.value (NumericValueRef = Variante über die numerischen Typen)
}
```

### `TimestampedChannel` — parallele Vektoren

```cpp
auto const& ts = std::get<osf::TimestampedChannel>(*ch);
ts.timestampsNs;  // std::vector<std::int64_t>, Stream-Reihenfolge
ts.values;        // NumericValues, parallel dazu
```

`bcAbsTimeStampData`-Blöcke landen direkt hier;
OSF4-`bcContinuedRelStampData`-Deltas werden beim Laden in absolute
Zeitstempel umgerechnet (Anker = letzter absoluter Zeitstempel des
Kanals).

### `VariableChannel` — String und Binary

```cpp
auto const& var = std::get<osf::VariableChannel>(*ch);
var.timestampsNs;                       // ein Zeitstempel pro Sample
auto strs = var.asStrings();            // Result<std::vector<std::string> const*>
auto bins = var.asBinaries();           // Result<std::vector<std::vector<uint8_t>> const*>
var.mimeType;                           // z. B. "image/jpeg" bei Binary-Kanälen
```

Genau eines von `string_values` / `binary_values` ist belegt
(entsprechend `dataType`); der falsch-typisierte Accessor liefert
`DataTypeMismatch`. Die Null-Terminator-Behandlung ist
versions-deterministisch (Spec-Rev 2026-05-24): bei **OSF4** hat der
Reader das letzte Byte bereits abgeschnitten, bei **OSF5** kommt die
Payload unverändert an.

### Flat-Accessoren — typisierte Kopien

Für jeden numerischen Typ (plus GPS) gibt es `as_<typ>_flat`-Helfer in
zwei Formen:

```cpp
// EquidistantChannel: nur die Werte
Result<std::vector<double>> osf::asDoublesFlat(EquidistantChannel const&);

// TimestampedChannel: (Zeitstempel, Wert)-Paare
Result<std::vector<std::pair<std::int64_t, double>>> osf::asDoublesFlat(TimestampedChannel const&);
```

(analog `asFloatsFlat`, `asInt32Flat`, …, `asGpsFlat`). Sie
**kopieren** bei jedem Aufruf und geben `DataTypeMismatch` zurück, wenn
der gespeicherte Typ nicht passt. Für Hot-Paths greift man stattdessen
einmal per `std::get` / `std::visit` auf den gespeicherten Vektor zu.

## `BlockReader` — die Stream-Ebene

Wenn der `DataManager` zu viel ist (RAM, Riesen-Dateien, eigene
Aggregation), liest man den Block-Strom selbst:

```cpp
#include <osf/osf.h>
#include <fstream>

std::ifstream in("messung.osf", std::ios::binary);
auto header = osf::parseMagicHeader(in);          // Result<MagicHeader>
// … Metablock-Bytes (header->metablockLen) lesen und parsen …
auto meta = osf::parseMetablockJson(buf.data(), buf.size());

osf::BlockReader reader(in, *meta);
for (auto& blk : reader) {                        // Input-Iterator + Sentinel
    if (!blk) { /* harter Fehler, Iteration endet */ break; }
    std::visit([](auto const& kind) { /* StartData / ContinuedData / … */ },
               blk->kind);
}
auto stats = reader.stats();
```

Wichtige Eigenschaften:

- **`next()`-Primitive:** `std::optional<Result<Block>>` —
  `std::nullopt` = sauberes Ende (EOF, Trailer konsumiert oder
  Trunkierung), Wert mit Fehler = harter Abbruch (z. B.
  `UnknownChannelIndex`).
- **Single-Pass:** Der Iterator ist ein Input-Iterator; eine zweite
  Iteration braucht einen neuen Reader (und Stream-Reset).
- **Skips bleiben sichtbar:** Deprecated/reservierte Control-Bytes und
  Blöcke `Unsupported`-deklarierter Kanäle kommen als
  `BlockKind::Skipped` mit `SkipReason` durch. Die Payload-Bytes
  werden standardmäßig ohne Allokation verworfen; wer hineinschauen
  will (z. B. in alte `bcMessageEvent`-Blöcke):
  `reader.withCaptureSkippedPayload(true)`.
- **OSF4-Trailer:** Der optionale `0xFFFF`-Infoblock + 40-Byte-Trailer
  wird stillschweigend konsumiert; `reader.trailerSeen()` meldet ihn.
- Der `BlockReader` dekomprimiert **nicht** selbst — bei OSFZ legt man
  einen `DecompressingIStream` davor (genau das tut der
  `DataManager`).

## Transparentes OSFZ

```cpp
#include <osf/compression.h>

std::ifstream raw("messung.osfz", std::ios::binary);
osf::CompressionFormat fmt = osf::detectCompression(raw); // None/Zlib/Gzip, nicht-konsumierend

osf::DecompressingIStream in(raw);   // istream-Fassade; inflatet bei Bedarf
// in wie jeden std::istream benutzen: parseMagicHeader(in), BlockReader, …
```

Die Erkennung läuft über die ersten zwei Bytes (gzip `1F 8B`, zlib
`78 01/5E/9C/DA`; echtes OSF beginnt mit `O` = `0x4F`, kollidiert also
nie). Die Dekompression ist konstant-speichernd (streamender
`std::streambuf` über `z_stream`), Best-Effort bei Trunkierung und
ohne zlib-Typen im öffentlichen Header (PIMPL). `DataManager` nutzt
diese Schicht automatisch — `loadFromFile("x.osfz")` funktioniert
ohne weiteres Zutun, und `stats.compressed` /
`stats.compressionFormat` dokumentieren den Fund.

## `ReaderStats` — Telemetrie

Nach jedem Ladevorgang (bzw. via `reader.stats()`):

| Feld | Bedeutung |
|---|---|
| `fileSizeBytes` | Dateigröße (wenn bekannt) |
| `headerSizeBytes` / `metablockSizeBytes` / `dataSectionSizeBytes` | Größen der drei Dateiabschnitte |
| `elapsed` | Wanduhr-Zeit der Block-Iteration |
| `channelsTotal` / `channelsWithData` / `channelsUnsupported` | Kanal-Zähler |
| `blocksTotal` / `blocksRead` / `blocksSkipped*` / `blocksTruncated` | Block-Zähler nach Grund |
| `trailerSeen` | OSF4-Infoblock/Trailer angetroffen |
| `compressed` / `compressionFormat` | OSFZ-Erkennung |
| `perChannel` | `ChannelStats` je Kanalindex: Name, Block-/Sample-/Byte-Zähler, Segmentanzahl, Zeitbereich |

`operator<<` formatiert beide Strukturen mehrzeilig für CLI-Ausgaben;
`formatBytes` / `formatDuration` stehen einzeln zur
Verfügung.

```cpp
std::cout << mgr.stats;                     // mehrzeilige Zusammenfassung
for (auto const& [idx, cs] : mgr.stats.perChannel)
    std::cout << cs << "\n";                // einzeilig pro Kanal
```

## Performance-Hinweise

- Reale Felddateien im einstelligen MB-Bereich laden in Release-Builds
  in wenigen Millisekunden.
- Der `DataManager` hält alle Samples im Speicher; als Faustregel
  braucht eine Datei etwa ihre entpackte Größe an RAM. Für größere
  Bestände: `BlockReader` streamend benutzen.
- Flat-Accessoren kopieren. Einmal `std::get` und direkt auf dem
  Vektor arbeiten ist die schnellere Form für wiederholten Zugriff.

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
