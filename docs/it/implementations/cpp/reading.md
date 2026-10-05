---
title: Lettura
description: Lettura di file OSF e OSFZ — DataManager, DataChannel, BlockReader, ReaderStats e decompressione trasparente
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
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Lettura

L'implementazione C++ legge **OSF4**, **OSF5** e, in modo trasparente,
**OSFZ** (gzip/zlib) tramite la stessa API. Esistono due livelli di lettura:

- **`osf::DataManager`** — la via standard. Carica il file per intero,
  assembla dal flusso di blocchi canali tipizzati e risolve tutti i
  confini dei blocchi. Per analisi, esportazione, tooling.
- **`osf::BlockReader`** — il livello di stream. Restituisce blocco per blocco in
  ordine di file con fabbisogno di memoria costante. Per file molto grandi,
  aggregazioni proprie e strumenti speciali.

## Avvio rapido

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

### Caricamento

| Metodo | Sorgente | Note |
|---|---|---|
| `DataManager::loadFromFile(path)` | File | OSF, OSFZ; determina la dimensione del file per `stats` |
| `DataManager::loadFromStream(istream&)` | qualsiasi `std::istream` | Lo stream deve essere posizionato all'inizio del file e (per il riconoscimento OSFZ) **seekable** |

Entrambe le vie percorrono la stessa pipeline: riconoscimento OSFZ →
magic header → parser del metablock (JSON o XML) → `BlockReader` fino a
EOF → assemblaggio dei canali. Il risultato è immutabile e può essere letto da
un numero qualsiasi di thread contemporaneamente.

### Accesso

```cpp
mgr.meta;                  // osf::MetaBlock  — FileInfo, Kanal-Definitionen, Infos
mgr.stats;                 // osf::ReaderStats — Telemetrie des Ladevorgangs
mgr.channels();            // std::vector<DataChannel> const& — Metablock-Reihenfolge
mgr.channel("a.b.c");      // DataChannel const* — nullptr, wenn unbekannt (Pflicht-Form)
mgr.channelByIndex(7);     // DataChannel const* — Index aus dem Metablock (optional)
```

`channel(name)` è la forma di accesso primaria;
`channelByIndex` è una comodità. Entrambi restituiscono `nullptr` anziché un
errore, perché «canale non presente» è un caso normale quando si esplorano
file di terzi.

### Cosa può accadere durante il caricamento

- **File troncato:** nessun errore. Tutti i blocchi completamente leggibili
  finiscono nei canali, `mgr.stats.blocksTruncated == 1`.
- **Tipo di dati sconosciuto (futuro):** il canale viene **omesso**
  dall'elenco dei canali (i suoi blocchi sono stati saltati a livello di reader);
  la definizione resta visibile in `mgr.meta.channels`,
  inclusa la grafia originale in `dataTypeRaw`.
- **Errori strutturali:** `InvalidMetablock`, `UnknownChannelIndex`,
  `ChannelMixedBlockTypes` ecc. interrompono il caricamento con un
  errore strutturato; vedere
  [Gestione degli errori](error-handling.md).

## `DataChannel` — i canali tipizzati

`DataChannel` è un `std::variant` su tre layout:

```cpp
using DataChannel = std::variant<EquidistantChannel, TimestampedChannel, VariableChannel>;
```

### Accessor comuni (funzioni libere)

Per il codice indipendente dalla variante esistono funzioni libere che
utilizzano internamente `std::visit`:

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

### `EquidistantChannel` — segmenti anziché timestamp

I canali equidistanti memorizzano **nessun timestamp per campione**.
Invece: un vettore di campioni flat (`NumericValues`, una variante
su tutti i tipi numerici) più un elenco di segmenti. Ogni
blocco `bcStartData` del file apre un segmento:

```cpp
struct Segment {
    std::int64_t startTimestampNs;  // absoluter Startzeitpunkt
    double       sampleRateHz;      // gilt bis zum nächsten Segment
    std::size_t  startIndex;        // erster Sample-Index im flachen Vektor
    std::size_t  sampleCount;       // Anzahl Samples dieses Segments
};
```

Il campione `i` di un segmento si trova a
`startTimestampNs + i * (1e9 / sampleRateHz)`. Le lacune tra
i segmenti **non** vengono interpolate: una pausa di registrazione
resta una pausa.

Chi ha bisogno di coppie `(timestamp, valore)` chiama
`samplesVector()` (materializzato; ricostruisce i timestamp
dai segmenti):

```cpp
auto const& eq = std::get<osf::EquidistantChannel>(*ch);
for (auto const& s : eq.samplesVector()) {
    // s.timestampNs, s.value (NumericValueRef = Variante über die numerischen Typen)
}
```

### `TimestampedChannel` — vettori paralleli

```cpp
auto const& ts = std::get<osf::TimestampedChannel>(*ch);
ts.timestampsNs;  // std::vector<std::int64_t>, Stream-Reihenfolge
ts.values;        // NumericValues, parallel dazu
```

I blocchi `bcAbsTimeStampData` finiscono direttamente qui;
i delta OSF4 `bcContinuedRelStampData` vengono convertiti in timestamp
assoluti durante il caricamento (ancoraggio = ultimo timestamp assoluto del
canale).

### `VariableChannel` — String e Binary

```cpp
auto const& var = std::get<osf::VariableChannel>(*ch);
var.timestampsNs;                       // ein Zeitstempel pro Sample
auto strs = var.asStrings();            // Result<std::vector<std::string> const*>
auto bins = var.asBinaries();           // Result<std::vector<std::vector<uint8_t>> const*>
var.mimeType;                           // z. B. "image/jpeg" bei Binary-Kanälen
```

Esattamente uno tra `string_values` / `binary_values` è valorizzato
(in base a `dataType`); l'accessor con tipo errato restituisce
`DataTypeMismatch`. La gestione del terminatore nullo è
deterministica rispetto alla versione (revisione della specifica 2026-05-24): con **OSF4** il
reader ha già rimosso l'ultimo byte, con **OSF5** il payload
arriva invariato.

### Accessor flat — copie tipizzate

Per ogni tipo numerico (più GPS) esistono helper `as_<typ>_flat` in
due forme:

```cpp
// EquidistantChannel: nur die Werte
Result<std::vector<double>> osf::asDoublesFlat(EquidistantChannel const&);

// TimestampedChannel: (Zeitstempel, Wert)-Paare
Result<std::vector<std::pair<std::int64_t, double>>> osf::asDoublesFlat(TimestampedChannel const&);
```

(analogamente `asFloatsFlat`, `asInt32Flat`, …, `asGpsFlat`). Essi
**copiano** a ogni chiamata e restituiscono `DataTypeMismatch` se
il tipo memorizzato non corrisponde. Per gli hot path si accede invece
una sola volta al vettore memorizzato tramite `std::get` / `std::visit`.

## `BlockReader` — il livello di stream

Quando `DataManager` è eccessivo (RAM, file enormi, aggregazione
propria), si legge direttamente il flusso di blocchi:

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

Proprietà importanti:

- **Primitiva `next()`:** `std::optional<Result<Block>>` —
  `std::nullopt` = fine pulita (EOF, trailer consumato o
  troncamento), valore con errore = interruzione netta (ad es.
  `UnknownChannelIndex`).
- **Single-pass:** l'iteratore è un input iterator; una seconda
  iterazione richiede un nuovo reader (e il reset dello stream).
- **Gli skip restano visibili:** i control byte deprecati/riservati e
  i blocchi di canali dichiarati `Unsupported` arrivano come
  `BlockKind::Skipped` con `SkipReason`. I byte del payload
  vengono scartati per impostazione predefinita senza allocazione; chi desidera
  ispezionarli (ad es. nei blocchi `bcStatusEvent` o `bcTrustedTimestamp`, che
  continuano a essere saltati): `reader.withCaptureSkippedPayload(true)`.
  `bcMessageEvent` viene decodificato per i canali `string`/`binary` anziché
  saltato e non richiede quindi più questo opt-in.
- **Trailer OSF4:** l'infoblock opzionale `0xFFFF` + il trailer di 40 byte
  vengono consumati in silenzio; `reader.trailerSeen()` ne segnala la presenza.
- Il `BlockReader` **non** decomprime da sé: con OSFZ si antepone
  un `DecompressingIStream` (è esattamente ciò che fa
  `DataManager`).

## OSFZ trasparente

```cpp
#include <osf/compression.h>

std::ifstream raw("messung.osfz", std::ios::binary);
osf::CompressionFormat fmt = osf::detectCompression(raw); // None/Zlib/Gzip, nicht-konsumierend

osf::DecompressingIStream in(raw);   // istream-Fassade; inflatet bei Bedarf
// in wie jeden std::istream benutzen: parseMagicHeader(in), BlockReader, …
```

Il riconoscimento avviene tramite i primi due byte (gzip `1F 8B`, zlib
`78 01/5E/9C/DA`; un vero OSF inizia con `O` = `0x4F`, quindi non entra mai
in collisione). La decompressione è a memoria costante (`std::streambuf`
in streaming su `z_stream`), best effort in caso di troncamento e
senza tipi zlib nell'header pubblico (PIMPL). `DataManager` utilizza
questo livello automaticamente: `loadFromFile("x.osfz")` funziona
senza ulteriori interventi, e `stats.compressed` /
`stats.compressionFormat` documentano il riscontro.

## `ReaderStats` — telemetria

Dopo ogni caricamento (o tramite `reader.stats()`):

| Campo | Significato |
|---|---|
| `fileSizeBytes` | Dimensione del file (se nota) |
| `headerSizeBytes` / `metablockSizeBytes` / `dataSectionSizeBytes` | Dimensioni delle tre sezioni del file |
| `elapsed` | Tempo di orologio dell'iterazione dei blocchi |
| `channelsTotal` / `channelsWithData` / `channelsUnsupported` | Contatori dei canali |
| `blocksTotal` / `blocksRead` / `blocksSkipped*` / `blocksTruncated` | Contatori dei blocchi per motivo |
| `trailerSeen` | Infoblock/trailer OSF4 rilevato |
| `compressed` / `compressionFormat` | Riconoscimento OSFZ |
| `perChannel` | `ChannelStats` per indice di canale: nome, contatori di blocchi/campioni/byte, numero di segmenti, intervallo temporale |

`operator<<` formatta entrambe le strutture su più righe per l'output CLI;
`formatBytes` / `formatDuration` sono disponibili
singolarmente.

```cpp
std::cout << mgr.stats;                     // mehrzeilige Zusammenfassung
for (auto const& [idx, cs] : mgr.stats.perChannel)
    std::cout << cs << "\n";                // einzeilig pro Kanal
```

## Note sulle prestazioni

- I file di campo reali nell'ordine di pochi MB si caricano in build Release
  in pochi millisecondi.
- `DataManager` mantiene tutti i campioni in memoria; come regola empirica
  un file richiede circa la propria dimensione decompressa in RAM. Per
  insiemi di dati più grandi: utilizzare `BlockReader` in streaming.
- Gli accessor flat copiano. Eseguire una sola volta `std::get` e lavorare direttamente sul
  vettore è la forma più veloce per accessi ripetuti.

> Questo documento è rilasciato con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
