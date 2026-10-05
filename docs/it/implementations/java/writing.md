---
title: Scrittura
description: Scrivere OSF5 con osf-java — StreamingWriter (embedded, a prova di interruzione), BlockWriter (a misura di analista), il profilo di integrità crc e round-trip
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - StreamingWriter
  - BlockWriter
  - crc32c
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Scrittura

La libreria scrive **esclusivamente OSF5** — anche quando la sorgente
era un file OSF4 o OSFZ. Due classi writer nel package
`com.optimeas.osf` coprono due profili d'uso molto diversi:

| | `StreamingWriter` | `BlockWriter` |
|---|---|---|
| Impiego | Registrazione embedded | Analisi, conversione, esportazione |
| Memoria | limitata (un buffer di blocco per canale, scartato dopo l'emissione) | raccoglie tutti i campioni in RAM |
| Durabilità | `FileChannel.force(true)` per blocco — a prova di perdita di alimentazione | nessun `force`; il file viene creato alla fine |
| Destinazione | percorso di file (`Path`) | `Path` **oppure** `OutputStream` qualsiasi (memoria, socket) |
| `sizeOfLengthValue` | fisso dalla dichiarazione del canale (il metablock è già su disco) | bump automatico 2 → 4 per i canali variabili |
| Ciclo di vita | `create()` → canali → `begin()` → scrittura → `close()` | `new` → raccolta → `writeToFile()` / `writeTo()` |
| Emissione multipla | no (un file per istanza, `Closeable`) | sì (`writeTo*` richiamabile a piacere) |

Entrambi condividono gli stessi tipi di canale, la stessa aritmetica del
chunking e le stesse famiglie di scrittura (equidistante, numerico
timestamped, GPS, String/Binary). Per canali, campioni,
`sizeOfLengthValue` e `created_utc` identici producono file OSF5
**identici byte per byte**.

## Dichiarare i canali — `ChannelDef`

Un canale non viene costruito direttamente, ma registrato tramite un
metodo `add…Channel` del writer, che internamente crea una `ChannelDef`
e restituisce l'**indice del canale** (sequenziale a partire da 0), che
tutte le chiamate di scrittura utilizzano. La `ChannelDef` (un `record`)
descrive il canale così come compare nel metablock:

```java
public record ChannelDef(
    int index,               // Kanalindex (0..65535), vom Block-Strom referenziert
    String name,             // vollqualifizierter Kanalname (Pflicht)
    DataType dataType,       // aufgelöster Datentyp (Pflicht)
    ChannelType channelType, // Datenform: SCALAR/VECTOR/MATRIX/BINARY
    int sizeOfLengthValue,   // Breite des Längenpräfix: 2 oder 4
    long timeIncrementNs,    // äquidistante Periode in ns; 0 = timestamped
    String physicalUnit,     // physikalische Einheit oder null
    Map<String,String> attributes) { }  // z. B. displayname, comment, reference
```

Due tipi di dichiarazione per writer:

```java
// Timestamped-Kanal (jedes Sample trägt seinen eigenen Zeitstempel):
int rpm = writer.addTimestampedChannel("motor.drehzahl", DataType.DOUBLE, 2);
int evt = writer.addTimestampedChannel("ereignis", DataType.STRING, 2,
                                       "1/min", Map.of("displayname", "Ereignis"));

// Äquidistanter Kanal (feste Rate; nur der Segmentstart trägt einen Zeitstempel):
int sig = writer.addEquidistantChannel("beschleunigung", DataType.DOUBLE, 4,
                                       1000.0 /* Hz */);
```

Vengono rifiutati con `IllegalArgumentException`: nome vuoto, tipo di
dati `null` o `UNSUPPORTED`, `sizeOfLengthValue` ≠ 2/4, un canale
equidistante con un tipo diverso da `FLOAT`/`DOUBLE` nonché una
frequenza di campionamento non positiva o non finita. Con lo
`StreamingWriter` ogni chiamata `add…Channel` dopo `begin()` genera
un'`OsfException` (la fase di configurazione è ormai terminata). Il
`channeltype` viene sempre normalizzato a `scalar` in scrittura — la
equidistanza è portata unicamente dal `timeincrement`, non dal
`channeltype`.

### Scegliere correttamente `sizeOfLengthValue`

Il campo di lunghezza di ogni blocco è largo 2 o 4 byte e limita la
dimensione del blocco (~64 KB o ~2 GB). Regole pratiche:

- **Canali String/Binary con campioni grandi** (immagini, audio, blob):
  con lo `StreamingWriter` dichiarare obbligatoriamente `4` — il valore
  non può più essere modificato dopo `begin()`. Il `BlockWriter`, per un
  canale variabile che può dimensionare autonomamente, passa
  automaticamente da 2 a 4 (auto-bump in fase di emissione); lì `2` come
  valore iniziale va sempre bene.
- **I canali numerici** non vengono mai modificati — si suddividono
  invece in più blocchi. Con lo `StreamingWriter`, `4` risparmia nei
  canali ad alta frequenza il chunking in molti piccoli blocchi
  sottoposti singolarmente a fsync.
- Altrimenti rimanere sul valore predefinito `2` (blocchi più compatti).

## `StreamingWriter` — embedded, a prova di interruzione

```java
import com.optimeas.osf.*;

try (StreamingWriter w = StreamingWriter.create(Path.of("aufzeichnung.osf"))) {
    w.setMetadata("creator", "logger-fw/3.2");   // Metadaten vor begin()
    w.setMetadata("tag", "pruefstand-7");

    int rpm = w.addTimestampedChannel("motor.drehzahl", DataType.DOUBLE, 2);
    int gps = w.addTimestampedChannel("fahrzeug.gps", DataType.GPS_LOCATION, 2);
    w.begin();                                    // Header + Metablock auf Platte, fsync

    while (running) {
        w.writeSample(rpm, nowNs(), readRpm());   // je Block: kodieren, schreiben, fsync
    }
}                                                 // close() emittiert Restblöcke + fsync
```

Garanzie e comportamento:

- **Ogni `writeSample` terminato è su disco come blocco completo**
  (`FileChannel.force(true)` = fsync). Dopo un'interruzione
  dell'alimentazione il file resta leggibile fino all'ultimo blocco
  confermato; il reader best effort ripristina ogni blocco prima del
  taglio e contrassegna un residuo troncato tramite
  `ReaderStats.truncationSeen()` (vedere [Lettura](./reading.md)).
- **Preambolo lazy o eager:** `begin()` scrive una sola volta la riga del
  magic header (`OSF5 <len>\n`) e il metablock JSON e li sottopone a
  fsync; se non viene richiamato esplicitamente, ciò avviene
  automaticamente al primo campione. In seguito il metablock non viene
  più toccato — **i setter dei metadati hanno effetto solo prima**.
- **`created_utc`** viene marcato automaticamente in `begin()` se non
  impostato (ISO-8601 UTC, ad es. `2026-07-11T08:30:00Z`).
- **I canali sono vincolati a una famiglia di blocchi:** chi scrive lo
  stesso canale una volta come timestamped e una volta come equidistante
  ottiene un'`OsfException`.
- `close()` è idempotente ed emette gli eventuali blocchi residui
  bufferizzati; in quanto `Closeable`, il writer va usato in un
  try-with-resources. **Non è thread-safe** — serializzare gli accessi
  dall'esterno.

### Famiglie di scrittura

```java
// Timestamped numerisch — Einzel- und Batch-Überladungen:
w.writeSample(ch, tsNs, 3.14);                        // double
w.writeSample(ch, tsNs, 42L);                         // beliebiger Integer-Kanal
w.writeSamples(ch, tsArray, werteArray);              // parallele Arrays (Bulk)

// GPS (eigene Überladung):
w.writeSample(ch, tsNs, new GpsLocation(lat, lon, alt));

// String / Binary (ein Sample pro Block; OSF5: kein 0x00-Terminator):
w.writeSample(ch, tsNs, "Ereignis: Tür offen");
w.writeSample(ch, tsNs, jpegBytes);                   // byte[]

// Äquidistant (nur float/double; Rate stammt aus addEquidistantChannel):
w.startEquidistantSegment(ch, t0Ns, daten);           // öffnet ein Segment
w.appendEquidistantSamples(ch, weitere);              // verlängert das offene Segment
```

`writeSample(int, long, long)` serve ogni canale intero (`int8…int64`,
`uint8…uint64`); il valore viene ristretto alla larghezza del canale
durante la codifica. Ogni chiamata batch e ogni chiamata di segmento
viene automaticamente suddivisa in chunk in base alla capacità di blocco
del canale — un fsync per blocco emesso. Un nuovo
`startEquidistantSegment` chiude il precedente e apre deliberatamente un
**nuovo** segmento; i vuoti tra i segmenti sono il mezzo conforme alla
specifica per le pause di registrazione.

## `BlockWriter` — raccogliere ed emettere

```java
BlockWriter w = new BlockWriter();
w.setMetadata("creator", "analyse-tool/1.0");

int ch  = w.addTimestampedChannel("messwert", DataType.DOUBLE);   // Auto-Bump-Form
int sig = w.addEquidistantChannel("signal", DataType.DOUBLE, 4, 100.0);
w.startEquidistantSegment(sig, t0Ns, samples);
w.writeSample(ch, tsNs, 42.0);

w.writeToFile(Path.of("ergebnis.osf"));

var mem = new java.io.ByteArrayOutputStream();   // oder ein beliebiger OutputStream
w.writeTo(mem);                                  // dieselbe Instanz erneut emittieren
```

- La famiglia `writeSample` / `startEquidistantSegment` rispecchia quella
  dello streaming writer (stessi tipi, stessa validazione), ma raccoglie
  solo in memoria; il chunking in blocchi conformi alla specifica avviene
  solo durante l'emissione.
- `writeToFile` / `writeTo` possono essere richiamati **più volte** — la
  stessa istanza può essere scritta contemporaneamente su file e in rete.
- **Auto-bump:** un canale variabile registrato con la forma a due
  argomenti `addTimestampedChannel(name, type)` parte con
  `sizeOfLengthValue = 2` e viene portato a `4` per l'emissione se il suo
  campione più grande supera il campo di lunghezza u16. La forma a tre
  argomenti con valore esplicito viene rispettata e rifiuta invece un
  campione troppo grande (come lo StreamingWriter, che non può aumentare
  il valore). I canali numerici non vengono mai aumentati.
- Nessun fsync — la durabilità è responsabilità del chiamante.
- `channelCount()` e `channelIndex("name")` sono utili quando gli indici
  non vengono tenuti da parte (`channelIndex` restituisce `-1` se
  sconosciuto).

## Valori predefiniti automatici dei metadati

Entrambi i writer scrivono le voci impostate tramite
`setMetadata(key, value)` **verbatim** nell'oggetto `osf.file` del
metablock. L'unico intervento automatico:

| Campo | Comportamento se non impostato |
|---|---|
| `created_utc` | **sempre** marcato (ora UTC attuale, `YYYY-MM-DDTHH:MM:SSZ`) — tramite `putIfAbsent`, un valore già presente non viene toccato |
| tutti gli altri (`creator`, `tag`, …) | scritti solo se impostati; mai come `null` |

Non esiste quindi alcun valore predefinito forzato per `creator` o `tag`
— ciò che non viene impostato non compare nel metablock.

## Profilo di integrità in scrittura (`crc`)

Entrambi i writer possono facoltativamente generare il profilo di
integrità OSF5 al livello `crc` — per impostazione predefinita è
disattivato (`IntegrityProfile.NONE`):

```java
w.setIntegrity(IntegrityProfile.CRC32C);   // vor begin() bzw. writeTo
```

Quando è attivato, il writer scrive

- un token `crc32c` nella riga del magic header, che riporta il **CRC
  del metablock**, e
- per ogni blocco di dati un **CRC32C di frame** aggiunto in coda (4
  byte, conteggiati nel campo di lunghezza del blocco). Con lo
  `StreamingWriter` il CRC di frame viene reso durevole con lo stesso
  `force(true)` del blocco stesso.

Il checksum è `java.util.zip.CRC32C` (nativo del JDK). I 4 byte del CRC
di frame sottraggono spazio al budget di payload di ogni blocco, per cui
in un blocco entrano leggermente meno campioni — l'aritmetica del
chunking ne tiene conto automaticamente. Il modo in cui il reader
verifica questi valori di controllo in modalità fail-closed è descritto
in [Lettura](./reading.md) e [Gestione degli errori](./error-handling.md).

Il livello di firma (`IntegrityProfile.ED25519`) **non** è supportato dal
writer e viene rifiutato con un'`OsfException` durante la scrittura del
preambolo.

## Round-trip e conversione

Riscrivere un `DataManager` caricato — al tempo stesso la conversione
OSF4 → OSF5 o OSFZ → OSF5:

```java
DataManager mgr = DataManager.loadFromFile(Path.of("alt.osf"));  // auch OSF4 / OSFZ
BlockWriter.fromManager(mgr).writeToFile(Path.of("neu.osf"));     // immer OSF5
```

`BlockWriter.fromManager(mgr)` costruisce un writer a partire dai canali
tipizzati e dai campioni del manager; chi desidera filtrare o rinominare
prima della scrittura continua a lavorare sul writer restituito.

Vengono conservati: nomi dei canali, tipi di dati, valori dei campioni
(identici bit per bit), confini dei segmenti e metadati del file —
**incluso `created_utc`**, poiché un valore caricato è già impostato e
non viene marcato di nuovo. **Non** viene ripreso il `sizeOfLengthValue`
(il writer parte da 2 e, se necessario, aumenta i canali variabili);
l'indice del canale viene riassegnato in base alla posizione
nell'elenco.

## Ciò che i writer deliberatamente non fanno

- **Nessun output OSF4** — OSF5 è l'unico formato di scrittura.
- **Nessun output OSFZ** — la libreria legge in modo trasparente i file
  compressi con gzip, ma non comprime autonomamente; la compressione è
  un passaggio successivo.
- **Nessun timestamp relativo** — il formato di tempo relativo è un
  retaggio di lettura di OSF4; i writer emettono timestamp assoluti.
- **Nessuna validazione dei timestamp** — la monotonia, secondo la
  specifica, non è richiesta e non viene imposta.
- **Nessuna firma** — il livello Ed25519 viene rifiutato.
- **Nessuna thread safety** — serializzare dall'esterno gli accessi a
  un'istanza di writer.

I dettagli di framing e chunking su cui si basano entrambi i writer sono
descritti nel capitolo [Interni](./internals.md); l'architettura
complessiva e gli strumenti si trovano in
[Architettura](./architecture.md), [Strumenti](./tools.md) e
[Build](./building.md). Esempi introduttivi sono raccolti nel
[Cookbook](./cookbook.md); la definizione vincolante del formato è nel
capitolo [Formato OSF](../../osf_general.md), la panoramica
nell'[implementazione Java](../java.md).

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
