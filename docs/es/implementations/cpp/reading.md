---
title: Lectura
description: Lectura de archivos OSF y OSFZ — DataManager, DataChannel, BlockReader, ReaderStats y descompresión transparente
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
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Lectura

La implementación en C++ lee **OSF4**, **OSF5** y, de forma transparente,
**OSFZ** (gzip/zlib) con la misma API. Hay dos niveles de lectura:

- **`osf::DataManager`**: la vía estándar. Carga el archivo por completo,
  ensambla canales tipados a partir del flujo de bloques y resuelve todos
  los límites de bloque. Para análisis, exportación y herramientas.
- **`osf::BlockReader`**: el nivel de flujo. Entrega bloque a bloque en el
  orden del archivo con necesidad de memoria constante. Para archivos muy
  grandes, agregaciones propias y herramientas especiales.

## Inicio rápido

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

### Carga

| Método | Origen | Notas |
|---|---|---|
| `DataManager::loadFromFile(path)` | Archivo | OSF, OSFZ; determina el tamaño del archivo para `stats` |
| `DataManager::loadFromStream(istream&)` | Cualquier `std::istream` | El flujo debe estar posicionado al inicio del archivo y ser **posicionable (seekable)** (para la detección de OSFZ) |

Ambas vías recorren la misma canalización: detección de OSFZ → cabecera
mágica → analizador del metabloque (JSON o XML) → `BlockReader` hasta EOF →
ensamblado de canales. El resultado es inmutable y puede ser leído
simultáneamente por cualquier número de hilos.

### Acceso

```cpp
mgr.meta;                  // osf::MetaBlock  — FileInfo, Kanal-Definitionen, Infos
mgr.stats;                 // osf::ReaderStats — Telemetrie des Ladevorgangs
mgr.channels();            // std::vector<DataChannel> const& — Metablock-Reihenfolge
mgr.channel("a.b.c");      // DataChannel const* — nullptr, wenn unbekannt (Pflicht-Form)
mgr.channelByIndex(7);     // DataChannel const* — Index aus dem Metablock (optional)
```

`channel(name)` es la forma de acceso principal; `channelByIndex` es una
comodidad. Ambos devuelven `nullptr` en lugar de un error, porque «el canal
no existe» es un caso normal al explorar archivos ajenos.

### Qué puede ocurrir durante la carga

- **Archivo truncado:** no es un error. Todos los bloques completamente
  legibles acaban en los canales, `mgr.stats.blocksTruncated == 1`.
- **Tipo de datos desconocido (futuro):** el canal se **omite** de la lista
  de canales (sus bloques se omitieron a nivel del lector); la definición
  sigue siendo visible en `mgr.meta.channels`, incluida la grafía original
  en `dataTypeRaw`.
- **Errores de estructura:** `InvalidMetablock`, `UnknownChannelIndex`,
  `ChannelMixedBlockTypes`, etc. interrumpen la carga con un error
  estructurado; véase [Gestión de errores](error-handling.md).

## `DataChannel` — los canales tipados

`DataChannel` es un `std::variant` sobre tres diseños:

```cpp
using DataChannel = std::variant<EquidistantChannel, TimestampedChannel, VariableChannel>;
```

### Accesores comunes (funciones libres)

Para código agnóstico a la variante existen funciones libres que usan
internamente `std::visit`:

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

### `EquidistantChannel` — segmentos en lugar de marcas de tiempo

Los canales equidistantes **no almacenan una marca de tiempo por muestra**.
En su lugar: un vector plano de muestras (`NumericValues`, una variante
sobre todos los tipos numéricos) más una lista de segmentos. Cada bloque
`bcStartData` del archivo abre un segmento:

```cpp
struct Segment {
    std::int64_t startTimestampNs;  // absoluter Startzeitpunkt
    double       sampleRateHz;      // gilt bis zum nächsten Segment
    std::size_t  startIndex;        // erster Sample-Index im flachen Vektor
    std::size_t  sampleCount;       // Anzahl Samples dieses Segments
};
```

La muestra `i` de un segmento se encuentra en
`startTimestampNs + i * (1e9 / sampleRateHz)`. Los huecos entre segmentos
**no** se interpolan: una pausa de grabación sigue siendo una pausa.

Quien necesite pares `(marca de tiempo, valor)` llama a `samplesVector()`
(materializa; reconstruye las marcas de tiempo a partir de los segmentos):

```cpp
auto const& eq = std::get<osf::EquidistantChannel>(*ch);
for (auto const& s : eq.samplesVector()) {
    // s.timestampNs, s.value (NumericValueRef = Variante über die numerischen Typen)
}
```

### `TimestampedChannel` — vectores paralelos

```cpp
auto const& ts = std::get<osf::TimestampedChannel>(*ch);
ts.timestampsNs;  // std::vector<std::int64_t>, Stream-Reihenfolge
ts.values;        // NumericValues, parallel dazu
```

Los bloques `bcAbsTimeStampData` acaban directamente aquí; los deltas
`bcContinuedRelStampData` de OSF4 se convierten a marcas de tiempo
absolutas durante la carga (ancla = última marca de tiempo absoluta del
canal).

### `VariableChannel` — cadena y binario

```cpp
auto const& var = std::get<osf::VariableChannel>(*ch);
var.timestampsNs;                       // ein Zeitstempel pro Sample
auto strs = var.asStrings();            // Result<std::vector<std::string> const*>
auto bins = var.asBinaries();           // Result<std::vector<std::vector<uint8_t>> const*>
var.mimeType;                           // z. B. "image/jpeg" bei Binary-Kanälen
```

Exactamente uno de `string_values` / `binary_values` está ocupado (según
`dataType`); el accesor con tipo equivocado devuelve `DataTypeMismatch`. El
tratamiento del terminador nulo es determinista según la versión
(revisión de la especificación 2026-05-24): en **OSF4** el lector ya ha
eliminado el último byte; en **OSF5** los datos útiles llegan sin
modificar.

### Accesores planos — copias tipadas

Para cada tipo numérico (más GPS) existen funciones auxiliares
`as_<tipo>_flat` en dos formas:

```cpp
// EquidistantChannel: nur die Werte
Result<std::vector<double>> osf::asDoublesFlat(EquidistantChannel const&);

// TimestampedChannel: (Zeitstempel, Wert)-Paare
Result<std::vector<std::pair<std::int64_t, double>>> osf::asDoublesFlat(TimestampedChannel const&);
```

(análogamente `asFloatsFlat`, `asInt32Flat`, …, `asGpsFlat`). **Copian** en
cada llamada y devuelven `DataTypeMismatch` si el tipo almacenado no
coincide. Para rutas críticas de rendimiento se accede, en su lugar, una
sola vez al vector almacenado mediante `std::get` / `std::visit`.

## `BlockReader` — el nivel de flujo

Cuando el `DataManager` es demasiado (RAM, archivos gigantes, agregación
propia), se lee el flujo de bloques directamente:

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

Propiedades importantes:

- **Primitiva `next()`:** `std::optional<Result<Block>>`: `std::nullopt` =
  final limpio (EOF, trailer consumido o truncamiento); un valor con error =
  interrupción estricta (p. ej., `UnknownChannelIndex`).
- **Una sola pasada:** el iterador es un iterador de entrada; una segunda
  iteración necesita un lector nuevo (y reiniciar el flujo).
- **Los saltos siguen siendo visibles:** los bytes de control
  obsoletos/reservados y los bloques de canales declarados `Unsupported`
  llegan como `BlockKind::Skipped` con `SkipReason`. Los bytes de datos
  útiles se descartan de forma predeterminada sin asignación de memoria;
  quien quiera inspeccionarlos (p. ej., los bloques `bcStatusEvent` o
  `bcTrustedTimestamp`, que se siguen omitiendo):
  `reader.withCaptureSkippedPayload(true)`. `bcMessageEvent` se decodifica
  para los canales `string`/`binary` en lugar de omitirse, por lo que ya no
  necesita esta activación opcional.
- **Trailer de OSF4:** el bloque de información opcional `0xFFFF` + el
  trailer de 40 bytes se consumen en silencio; `reader.trailerSeen()` lo
  notifica.
- El `BlockReader` **no** descomprime por sí mismo: con OSFZ se antepone un
  `DecompressingIStream` (justo lo que hace el `DataManager`).

## OSFZ transparente

```cpp
#include <osf/compression.h>

std::ifstream raw("messung.osfz", std::ios::binary);
osf::CompressionFormat fmt = osf::detectCompression(raw); // None/Zlib/Gzip, nicht-konsumierend

osf::DecompressingIStream in(raw);   // istream-Fassade; inflatet bei Bedarf
// in wie jeden std::istream benutzen: parseMagicHeader(in), BlockReader, …
```

La detección se realiza mediante los dos primeros bytes (gzip `1F 8B`, zlib
`78 01/5E/9C/DA`; un OSF genuino comienza con `O` = `0x4F`, por lo que nunca
colisiona). La descompresión es de memoria constante (`std::streambuf` en
streaming sobre `z_stream`), de mejor esfuerzo ante truncamientos y sin
tipos de zlib en la cabecera pública (PIMPL). `DataManager` usa esta capa
automáticamente: `loadFromFile("x.osfz")` funciona sin más, y
`stats.compressed` / `stats.compressionFormat` documentan el hallazgo.

## `ReaderStats` — telemetría

Tras cada proceso de carga (o mediante `reader.stats()`):

| Campo | Significado |
|---|---|
| `fileSizeBytes` | Tamaño del archivo (si se conoce) |
| `headerSizeBytes` / `metablockSizeBytes` / `dataSectionSizeBytes` | Tamaños de las tres secciones del archivo |
| `elapsed` | Tiempo de reloj de pared de la iteración de bloques |
| `channelsTotal` / `channelsWithData` / `channelsUnsupported` | Contadores de canales |
| `blocksTotal` / `blocksRead` / `blocksSkipped*` / `blocksTruncated` | Contadores de bloques por motivo |
| `trailerSeen` | Se encontró el bloque de información/trailer de OSF4 |
| `compressed` / `compressionFormat` | Detección de OSFZ |
| `perChannel` | `ChannelStats` por índice de canal: nombre, contadores de bloques/muestras/bytes, número de segmentos, intervalo de tiempo |

`operator<<` formatea ambas estructuras en varias líneas para las salidas de
CLI; `formatBytes` / `formatDuration` están disponibles por separado.

```cpp
std::cout << mgr.stats;                     // mehrzeilige Zusammenfassung
for (auto const& [idx, cs] : mgr.stats.perChannel)
    std::cout << cs << "\n";                // einzeilig pro Kanal
```

## Indicaciones de rendimiento

- Los archivos de campo reales de un solo dígito de MB se cargan en pocos
  milisegundos en compilaciones Release.
- El `DataManager` mantiene todas las muestras en memoria; como regla
  práctica, un archivo necesita aproximadamente su tamaño descomprimido en
  RAM. Para conjuntos mayores: usar el `BlockReader` en streaming.
- Los accesores planos copian. Hacer una vez `std::get` y trabajar
  directamente sobre el vector es la forma más rápida para accesos
  repetidos.

> Este documento se distribuye bajo la licencia [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
