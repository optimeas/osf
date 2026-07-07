---
title: Kochbuch
description: Rezepte für typische Aufgaben mit der OSF-C++-Bibliothek — von der Datei-Inspektion über CSV-Export bis zur Embedded-Aufzeichnungsschleife
sidebar_position: 7
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - Beispiele
  - Kochbuch
  - Rezepte
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Kochbuch

Kompakte, kopierfertige Rezepte. Vier davon existieren als lauffähige
Programme im `examples/`-Verzeichnis der Bibliothek
(`inspect`, `dump`, `write`, `copy`) — bei Abweichungen gilt der
mitgelieferte Beispiel-Code.

Alle Rezepte setzen `#include <osf/osf.h>` voraus, sofern nicht
anders angegeben; Fehlerbehandlung ist auf das Minimum gekürzt.

## Datei inspizieren (Metadaten + Kanalliste)

```cpp
auto r = osf::DataManager::loadFromFile(pfad);          // .osf oder .osfz
if (!r) { std::cerr << r.error().message << "\n"; return 1; }
auto const& mgr = *r;

std::cout << "version:  " << mgr.meta.fileInfo.version << "\n"
          << "creator:  " << mgr.meta.fileInfo.creator.value_or("-") << "\n"
          << "created:  " << mgr.meta.fileInfo.createdUtc.value_or("-") << "\n"
          << "channels: " << mgr.channels().size() << "\n";

for (osf::DataChannel const& ch : mgr.channels()) {
    std::cout << "  [" << osf::channelIndex(ch) << "] "
              << osf::channelName(ch)
              << "  (" << osf::channelSampleCount(ch) << " Samples, Einheit: "
              << osf::channelPhysicalUnit(ch).value_or("-") << ")\n";
}
```

## Einen Kanal als `double`-Werte mit Zeitstempeln holen

```cpp
osf::DataChannel const* ch = mgr.channel("Motor.Drehzahl");
if (!ch) { /* Kanal existiert nicht */ }

// Timestamped-Kanal -> (ts, wert)-Paare:
if (auto const* ts = std::get_if<osf::TimestampedChannel>(ch)) {
    if (auto paare = osf::asDoublesFlat(*ts)) {
        for (auto const& [t_ns, v] : *paare) { /* … */ }
    }
}

// Äquidistanter Kanal -> Werte + Zeit aus Segmenten:
if (auto const* eq = std::get_if<osf::EquidistantChannel>(ch)) {
    for (auto const& s : eq->samplesVector()) {        // rekonstruiert Zeitstempel
        double v = std::get<double>(s.value);
        /* s.timestampNs, v */
    }
}
```

## Über gemischte Datentypen generisch iterieren

`std::visit` über die Wert-Variante macht den Code datentyp-agnostisch
(praktisch für Export-Werkzeuge):

```cpp
auto const& tc = std::get<osf::TimestampedChannel>(*ch);
for (auto const& s : tc.samplesVector()) {
    std::visit([&](auto const& wert) {
        using T = std::decay_t<decltype(wert)>;
        if constexpr (std::is_same_v<T, osf::GpsLocation>) {
            csv << s.timestampNs << ";" << wert.latitude << ";" << wert.longitude << "\n";
        } else if constexpr (std::is_arithmetic_v<T>) {
            csv << s.timestampNs << ";" << +wert << "\n";   // +wert: int8 als Zahl drucken
        }
    }, s.value);
}
```

## Minimaler CSV-Export

```cpp
#include <fstream>

std::ofstream csv("kanal.csv");
csv << "timestamp_ns;value\n";

auto const* tc = std::get_if<osf::TimestampedChannel>(mgr.channel(name));
if (!tc) return;
auto paare = osf::asDoublesFlat(*tc);
if (!paare) return;                                  // DataTypeMismatch etc.
for (auto const& [t, v] : *paare) csv << t << ";" << v << "\n";
```

## OSF4 → OSF5 konvertieren (auch OSFZ-Eingabe)

```cpp
auto mgr = osf::DataManager::loadFromFile("alt_osf4.osf");
if (!mgr) { /* … */ }
if (auto r = osf::writeToFile(*mgr, "neu_osf5.osf"); !r) { /* … */ }
// Samples bitgenau erhalten; Ausgabe immer OSF5
```

Das Beispielprogramm `copy` macht genau das und verifiziert per
Reload.

## Neue Datei mit Analyse-Daten schreiben

```cpp
osf::BlockWriter w;
w.setCreator("mein-tool/1.0");

osf::ChannelDef def;
def.name        = "ergebnis.fft_peak";
def.dataType    = osf::DataType::Double;
def.channelType = osf::ChannelType::Scalar;
def.physicalUnit = "Hz";
auto ch = w.addChannel(def);
if (!ch) { /* … */ }

for (auto const& [ts_ns, peak] : ergebnisse)
    if (auto r = w.addTimestampedSample<double>(*ch, ts_ns, peak); !r) { /* … */ }

if (auto r = w.writeToFile("ergebnis.osf"); !r) { /* … */ }
```

## Embedded-Aufzeichnungsschleife (ausfallsicher, mit Frische-Garantie)

```cpp
#include <osf/streamingwriter.h>
#include <osf/stalevalueguard.h>

osf::StreamingWriter w("/data/rec_0001.osf");
w.setCreator("logger-fw/3.2");

auto temp = w.addChannel(temp_def);      // timestamped double
auto tuer = w.addChannel(tuer_def);      // timestamped bool (Event-Kanal)
if (!temp || !tuer) { /* … */ }
if (auto r = w.start(); !r) { /* … */ }

osf::StaleValueGuard guard(w);            // 100-s-Default

while (laeuft) {
    std::int64_t const now = jetzt_ns();

    if (neuer_messwert)
        if (auto r = guard.writeTimestampedSample<double>(*temp, now, wert); !r) break;
    if (tuer_geaendert)
        if (auto r = guard.writeTimestampedSample<bool>(*tuer, now, offen); !r) break;

    if (auto r = guard.poll(now); !r) break;   // re-emittiert stale Kanäle
    warte_auf_naechsten_tick();
}
if (auto r = w.close(); !r) { /* Original- oder Close-Fehler loggen */ }
```

Jeder bestätigte Write ist gefsynct — nach Stromausfall liest der
`DataManager` die Datei bis zum letzten vollständigen Block.

## Bilder/Blobs als Binary-Kanal

```cpp
// Schreiben (StreamingWriter: sov=4 deklarieren — Samples > ~64 KB!):
osf::ChannelDef cam;
cam.name             = "kamera.snapshots";
cam.dataType         = osf::DataType::Binary;
cam.channelType      = osf::ChannelType::Scalar;
cam.sizeOfLengthValue = 4;
cam.mimeType         = "image/jpeg";
auto cam_ch = w.addChannel(cam);
/* … start() … */
w.writeTimestampedBinary(*cam_ch, ts_ns, osf::BinarySample::fromVector(jpeg));

// Lesen:
auto const& vc = std::get<osf::VariableChannel>(*mgr.channel("kamera.snapshots"));
if (auto bins = vc.asBinaries()) {
    for (std::size_t i = 0; i < (*bins)->size(); ++i) {
        std::int64_t ts = vc.timestampsNs[i];
        std::vector<std::uint8_t> const& jpeg = (**bins)[i];
        /* … */
    }
}
```

## Riesige Datei streamend aggregieren (ohne DataManager)

```cpp
#include <fstream>

std::ifstream raw(pfad, std::ios::binary);
osf::DecompressingIStream in(raw);                       // OSFZ transparent

auto hdr = osf::parseMagicHeader(in);
if (!hdr) { /* … */ }
std::vector<std::uint8_t> mb(hdr->metablockLen);
in.read(reinterpret_cast<char*>(mb.data()), static_cast<std::streamsize>(mb.size()));
auto meta = (hdr->version == osf::OsfVersion::Osf5)
    ? osf::parseMetablockJson(mb.data(), mb.size())
    : osf::parseMetablockXml(mb.data(), mb.size());
if (!meta) { /* … */ }

double summe = 0; std::uint64_t n = 0;
osf::BlockReader reader(in, *meta);
for (auto& blk : reader) {
    if (!blk) break;                                     // harter Fehler
    if (auto const* abs = std::get_if<osf::AbsTimestampData>(&blk->kind)) {
        if (auto const* v = std::get_if<std::vector<std::pair<std::int64_t,double>>>(&abs->samples)) {
            for (auto const& [t, x] : *v) { summe += x; ++n; }
        }
    }
}
// Konstanter Speicher unabhängig von der Dateigröße.
```

## Lese-Statistik ausgeben

```cpp
std::cout << mgr.stats;                                  // mehrzeilige Zusammenfassung
if (mgr.stats.blocksTruncated) std::cout << "ACHTUNG: Datei war abgeschnitten\n";
if (mgr.stats.compressed)
    std::cout << "Quelle war OSFZ ("
              << osf::compressionFormatName(mgr.stats.compressionFormat) << ")\n";
```

## Exceptions statt Result (App-Außenkante)

```cpp
#include <osf/throwing.h>

int main(int argc, char** argv) try {
    auto mgr = osf::throwing::load(argv[1]);
    osf::throwing::writeToFile(mgr, argv[2]);
    return 0;
} catch (osf::Exception const& e) {
    std::cerr << "OSF-Fehler [" << osf::errorCategoryName(e.code()) << "]: "
              << e.what() << "\n";
    return 1;
}
```

## Aus C konsumieren

Siehe das vollständige Beispiel auf der Seite [C-ABI](c-abi.md) — laden,
Kanäle auflisten, Copy-out-Reader, `osf_write_to_file`.

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
