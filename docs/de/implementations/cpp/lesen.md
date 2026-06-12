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
#include <osf/osf.hpp>

auto result = osf::DataManager::load_from_file("messung.osf");  // auch .osfz
if (!result) {
    std::cerr << result.error().message << "\n";
    return 1;
}
osf::DataManager const& mgr = *result;

// Kanal über den Namen ansprechen (primäre Zugriffsform)
if (osf::DataChannel const* ch = mgr.channel("Sensor.Temperatur")) {
    if (auto werte = osf::as_doubles_flat(std::get<osf::TimestampedChannel>(*ch))) {
        for (auto const& [ts_ns, wert] : *werte) { /* … */ }
    }
}
```

## `DataManager`

### Laden

| Methode | Quelle | Hinweise |
|---|---|---|
| `DataManager::load_from_file(path)` | Datei | OSF, OSFZ; ermittelt die Dateigröße für `stats` |
| `DataManager::load_from_stream(istream&)` | beliebiger `std::istream` | Strom muss am Dateianfang positioniert und (für die OSFZ-Erkennung) **seekbar** sein |

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
mgr.channel_by_index(7);   // DataChannel const* — Index aus dem Metablock (optional)
```

`channel(name)` ist die primäre Zugriffsform;
`channel_by_index` ist Komfort. Beide geben `nullptr` statt eines
Fehlers zurück, weil „Kanal nicht vorhanden" beim Erkunden fremder
Dateien ein normaler Fall ist.

### Was beim Laden passieren kann

- **Trunkierte Datei:** kein Fehler. Alle vollständig lesbaren Blöcke
  landen in den Kanälen, `mgr.stats.blocks_truncated == 1`.
- **Unbekannter (zukünftiger) Datentyp:** Der Kanal wird aus der
  Kanalliste **weggelassen** (seine Blöcke wurden auf Reader-Ebene
  übersprungen); die Definition bleibt in `mgr.meta.channels` sichtbar,
  inklusive Original-Schreibweise in `data_type_raw`.
- **Strukturfehler:** `InvalidMetablock`, `UnknownChannelIndex`,
  `ChannelMixedBlockTypes` usw. brechen das Laden mit einem
  strukturierten Fehler ab — siehe
  [Fehlerbehandlung](fehlerbehandlung.md).

## `DataChannel` — die typisierten Kanäle

`DataChannel` ist eine `std::variant` über drei Layouts:

```cpp
using DataChannel = std::variant<EquidistantChannel, TimestampedChannel, VariableChannel>;
```

### Gemeinsame Accessoren (freie Funktionen)

Für Variante-agnostischen Code gibt es freie Funktionen, die intern
`std::visit` benutzen:

```cpp
osf::channel_index(ch);          // std::uint16_t
osf::channel_name(ch);           // std::string const&
osf::channel_data_type(ch);      // osf::DataType
osf::channel_physical_unit(ch);  // std::optional<std::string>
osf::channel_display_name(ch);   // std::optional<std::string>
osf::channel_sample_count(ch);   // std::size_t (Summe über alle Segmente)
osf::channel_is_empty(ch);       // bool
osf::channel_meta(ch);           // ChannelMeta const& (sekundäre Definitionfelder)
```

### `EquidistantChannel` — Segmente statt Zeitstempel

Äquidistante Kanäle speichern **keinen Zeitstempel pro Sample**.
Stattdessen: ein flacher Sample-Vektor (`NumericValues`, eine Variante
über alle numerischen Typen) plus eine Segmentliste. Jeder
`bcStartData`-Block der Datei öffnet ein Segment:

```cpp
struct Segment {
    std::int64_t start_timestamp_ns;  // absoluter Startzeitpunkt
    double       sample_rate_hz;      // gilt bis zum nächsten Segment
    std::size_t  start_index;         // erster Sample-Index im flachen Vektor
    std::size_t  sample_count;        // Anzahl Samples dieses Segments
};
```

Sample `i` eines Segments liegt bei
`start_timestamp_ns + i * (1e9 / sample_rate_hz)`. Lücken zwischen
Segmenten werden **nicht** interpoliert — eine Aufzeichnungspause
bleibt eine Pause.

Wer `(Zeitstempel, Wert)`-Paare braucht, ruft
`samples_vector()` auf (materialisiert; rekonstruiert die Zeitstempel
aus den Segmenten):

```cpp
auto const& eq = std::get<osf::EquidistantChannel>(*ch);
for (auto const& s : eq.samples_vector()) {
    // s.timestamp_ns, s.value (NumericValueRef = Variante über die numerischen Typen)
}
```

### `TimestampedChannel` — parallele Vektoren

```cpp
auto const& ts = std::get<osf::TimestampedChannel>(*ch);
ts.timestamps_ns;  // std::vector<std::int64_t>, Stream-Reihenfolge
ts.values;         // NumericValues, parallel dazu
```

`bcAbsTimeStampData`-Blöcke landen direkt hier;
OSF4-`bcContinuedRelStampData`-Deltas werden beim Laden in absolute
Zeitstempel umgerechnet (Anker = letzter absoluter Zeitstempel des
Kanals).

### `VariableChannel` — String und Binary

```cpp
auto const& var = std::get<osf::VariableChannel>(*ch);
var.timestamps_ns;                       // ein Zeitstempel pro Sample
auto strs = var.as_strings();            // Result<std::vector<std::string> const*>
auto bins = var.as_binaries();           // Result<std::vector<std::vector<uint8_t>> const*>
var.mime_type;                           // z. B. "image/jpeg" bei Binary-Kanälen
```

Genau eines von `string_values` / `binary_values` ist belegt
(entsprechend `data_type`); der falsch-typisierte Accessor liefert
`DataTypeMismatch`. Die Null-Terminator-Behandlung ist
versions-deterministisch (Spec-Rev 2026-05-24): bei **OSF4** hat der
Reader das letzte Byte bereits abgeschnitten, bei **OSF5** kommt die
Payload unverändert an.

### Flat-Accessoren — typisierte Kopien

Für jeden numerischen Typ (plus GPS) gibt es `as_<typ>_flat`-Helfer in
zwei Formen:

```cpp
// EquidistantChannel: nur die Werte
Result<std::vector<double>> osf::as_doubles_flat(EquidistantChannel const&);

// TimestampedChannel: (Zeitstempel, Wert)-Paare
Result<std::vector<std::pair<std::int64_t, double>>> osf::as_doubles_flat(TimestampedChannel const&);
```

(analog `as_floats_flat`, `as_int32_flat`, …, `as_gps_flat`). Sie
**kopieren** bei jedem Aufruf und geben `DataTypeMismatch` zurück, wenn
der gespeicherte Typ nicht passt. Für Hot-Paths greift man stattdessen
einmal per `std::get` / `std::visit` auf den gespeicherten Vektor zu.

## `BlockReader` — die Stream-Ebene

Wenn der `DataManager` zu viel ist (RAM, Riesen-Dateien, eigene
Aggregation), liest man den Block-Strom selbst:

```cpp
#include <osf/osf.hpp>
#include <fstream>

std::ifstream in("messung.osf", std::ios::binary);
auto header = osf::parse_magic_header(in);          // Result<MagicHeader>
// … Metablock-Bytes (header->metablock_len) lesen und parsen …
auto meta = osf::parse_metablock_json(buf.data(), buf.size());

osf::BlockReader reader(in, *meta);
for (auto& blk : reader) {                          // Input-Iterator + Sentinel
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
  `reader.with_capture_skipped_payload(true)`.
- **OSF4-Trailer:** Der optionale `0xFFFF`-Infoblock + 40-Byte-Trailer
  wird stillschweigend konsumiert; `reader.trailer_seen()` meldet ihn.
- Der `BlockReader` dekomprimiert **nicht** selbst — bei OSFZ legt man
  einen `DecompressingIStream` davor (genau das tut der
  `DataManager`).

## Transparentes OSFZ

```cpp
#include <osf/compression.hpp>

std::ifstream raw("messung.osfz", std::ios::binary);
osf::CompressionFormat fmt = osf::detect_compression(raw); // None/Zlib/Gzip, nicht-konsumierend

osf::DecompressingIStream in(raw);   // istream-Fassade; inflatet bei Bedarf
// in wie jeden std::istream benutzen: parse_magic_header(in), BlockReader, …
```

Die Erkennung läuft über die ersten zwei Bytes (gzip `1F 8B`, zlib
`78 01/5E/9C/DA`; echtes OSF beginnt mit `O` = `0x4F`, kollidiert also
nie). Die Dekompression ist konstant-speichernd (streamender
`std::streambuf` über `z_stream`), Best-Effort bei Trunkierung und
ohne zlib-Typen im öffentlichen Header (PIMPL). `DataManager` nutzt
diese Schicht automatisch — `load_from_file("x.osfz")` funktioniert
ohne weiteres Zutun, und `stats.compressed` /
`stats.compression_format` dokumentieren den Fund.

## `ReaderStats` — Telemetrie

Nach jedem Ladevorgang (bzw. via `reader.stats()`):

| Feld | Bedeutung |
|---|---|
| `file_size_bytes` | Dateigröße (wenn bekannt) |
| `header_size_bytes` / `metablock_size_bytes` / `data_section_size_bytes` | Größen der drei Dateiabschnitte |
| `elapsed` | Wanduhr-Zeit der Block-Iteration |
| `channels_total` / `channels_with_data` / `channels_unsupported` | Kanal-Zähler |
| `blocks_total` / `blocks_read` / `blocks_skipped_*` / `blocks_truncated` | Block-Zähler nach Grund |
| `trailer_seen` | OSF4-Infoblock/Trailer angetroffen |
| `compressed` / `compression_format` | OSFZ-Erkennung |
| `per_channel` | `ChannelStats` je Kanalindex: Name, Block-/Sample-/Byte-Zähler, Segmentanzahl, Zeitbereich |

`operator<<` formatiert beide Strukturen mehrzeilig für CLI-Ausgaben;
`format_bytes` / `format_duration` stehen einzeln zur
Verfügung.

```cpp
std::cout << mgr.stats;                     // mehrzeilige Zusammenfassung
for (auto const& [idx, cs] : mgr.stats.per_channel)
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
