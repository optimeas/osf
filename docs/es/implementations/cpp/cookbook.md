---
title: Recetario
description: Recetas para tareas habituales con la biblioteca C++ de OSF — desde la inspección de archivos y la exportación a CSV hasta el bucle de registro embebido
sidebar_position: 7
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - Ejemplos
  - Recetario
  - Recetas
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Recetario

Recetas compactas y listas para copiar. Cuatro de ellas existen como
programas ejecutables en el directorio `examples/` de la biblioteca
(`inspect`, `dump`, `write`, `copy`); en caso de discrepancias, prevalece el
código de ejemplo incluido.

Todas las recetas presuponen `#include <osf/osf.h>`, salvo que se indique lo
contrario; la gestión de errores se ha reducido al mínimo.

## Inspeccionar un archivo (metadatos + lista de canales)

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

## Obtener un canal como valores `double` con marcas de tiempo

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

## Iterar de forma genérica sobre tipos de datos mixtos

`std::visit` sobre la variante de valor hace que el código sea agnóstico al
tipo de datos (práctico para herramientas de exportación):

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

## Exportación mínima a CSV

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

## Convertir OSF4 → OSF5 (también con entrada OSFZ)

```cpp
auto mgr = osf::DataManager::loadFromFile("alt_osf4.osf");
if (!mgr) { /* … */ }
if (auto r = osf::writeToFile(*mgr, "neu_osf5.osf"); !r) { /* … */ }
// Samples bitgenau erhalten; Ausgabe immer OSF5
```

El programa de ejemplo `copy` hace exactamente esto y lo verifica
recargando el archivo.

## Escribir un archivo nuevo con datos de análisis

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

## Bucle de registro embebido (resistente a fallos, con garantía de frescura)

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

Cada escritura confirmada se sincroniza con `fsync`: tras un corte de
alimentación, el `DataManager` lee el archivo hasta el último bloque
completo.

## Imágenes/blobs como canal binario

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

## Agregar en streaming un archivo enorme (sin DataManager)

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

## Mostrar las estadísticas de lectura

```cpp
std::cout << mgr.stats;                                  // mehrzeilige Zusammenfassung
if (mgr.stats.blocksTruncated) std::cout << "ACHTUNG: Datei war abgeschnitten\n";
if (mgr.stats.compressed)
    std::cout << "Quelle war OSFZ ("
              << osf::compressionFormatName(mgr.stats.compressionFormat) << ")\n";
```

## Excepciones en lugar de Result (borde de la aplicación)

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

## Consumir desde C

Véase el ejemplo completo en la página [C-ABI](c-abi.md): cargar, listar
canales, lectores copy-out, `osf_write_to_file`.

> Este documento se distribuye bajo la licencia [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
