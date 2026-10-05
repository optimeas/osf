---
title: Lettura
description: Leggere file OSF e OSFZ in Java — DataManager, DataChannel, il flusso di blocchi, ReaderStats e decompressione trasparente
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - DataManager
  - DataChannel
  - OSFZ
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Lettura

L'implementazione Java legge **OSF4**, **OSF5** e, in modo
trasparente, **OSFZ** (gzip/zlib) tramite la stessa API. Legge inoltre
il profilo di integrità `crc` verificando ogni checksum
([Gestione degli errori](./error-handling.md)). Il punto di ingresso
pubblico è esattamente una classe:

- **`com.optimeas.osf.DataManager`** — il percorso di lettura standard
  e unico pubblico. Carica il file per intero, assembla canali
  tipizzati dal flusso di blocchi e risolve tutti i confini dei blocchi.
  Per analisi, esportazione e tooling.

Il vero decoder del flusso di blocchi opera al di sotto, nel package
non esportato `com.optimeas.osf.internal` — è un dettaglio
implementativo del `DataManager` e non una superficie pubblica (vedere
la sezione [Il livello del flusso di blocchi](#die-blockstrom-ebene)).

## Avvio rapido

```java
import com.optimeas.osf.DataManager;
import com.optimeas.osf.DataChannel;
import java.nio.file.Path;

DataManager mgr = DataManager.loadFromFile(Path.of("messung.osf")); // auch .osfz

// Alle Kanäle auflisten (Metablock-Reihenfolge)
for (DataChannel ch : mgr.channels()) {
    System.out.printf("%-30s %-12s %d Samples%n",
            ch.name(), ch.dataType(), ch.sampleCount());
}

// Einen Kanal über den Namen ansprechen (primäre Zugriffsform)
mgr.channelByName("Sensor.Temperatur").ifPresent(ch -> {
    double[] werte     = ch.asDoubles();     // Werte, auf double geweitet
    long[]   zeitstamp = ch.timestampsNs();  // parallele Zeitstempel (ns)
    // …
});
```

## `DataManager`

### Caricamento

| Metodo | Sorgente | Note |
|---|---|---|
| `DataManager.loadFromFile(Path)` | File | OSF, OSFZ; apre e chiude lo stream autonomamente |
| `DataManager.load(InputStream)` | `InputStream` qualsiasi | viene consumato per intero; deve trovarsi all'inizio del file |

Entrambi i percorsi attraversano la stessa pipeline: riconoscimento
OSFZ → magic header → (con `crc` il checksum del metablock) → parser
del metablock (JSON per OSF5, XML/StAX per OSF4) → decoder del flusso
di blocchi fino a EOF → assemblaggio dei canali. Il risultato è
immutabile e può essere letto contemporaneamente da un numero
qualsiasi di thread.

### Accesso

```java
mgr.version();                 // OsfVersion — OSF4 oder OSF5, aus dem Magic-Header
mgr.metadata();                // Map<String,String> — Datei-Metadaten aus dem "file"-Block
mgr.stats();                   // ReaderStats — Telemetrie des Ladevorgangs
mgr.channels();                // List<DataChannel> — Metablock-Reihenfolge
mgr.channelByName("a.b.c");    // Optional<DataChannel> — leer, wenn unbekannt
mgr.channelByIndex(7);         // Optional<DataChannel> — Index aus dem Metablock
```

`channelByName` è la forma di accesso primaria; `channelByIndex` è una
comodità. Entrambi restituiscono un `Optional` vuoto anziché un errore,
perché «canale non presente» è un caso normale quando si esplorano file
di terzi. In caso di nome assegnato due volte prevale la **prima**
definizione.

Le chiavi di `metadata()` corrispondono esattamente ai nomi di campo
wire del blocco `file` (ad esempio `creator`, `created_utc`, `tag`,
`reason`).

### Che cosa può accadere durante il caricamento

- **File troncato:** nessuna eccezione. Tutti i blocchi leggibili per
  intero finiscono nei canali, `mgr.stats().truncationSeen() == true`.
- **Tipo di dati sconosciuto (futuro):** il canale viene **omesso** da
  `channels()` (i suoi blocchi sono stati saltati a livello di reader).
  Un *tipo di canale* sconosciuto (la forma dei dati) lascia invece il
  canale presente — la leggibilità dipende solo dal tipo di dati e dai
  tipi di blocco, mai dal `channeltype`.
- **Errore di checksum del metablock** (solo con `crc`): il caricamento
  si interrompe in modalità **fail-closed** con
  `OsfException.MetablockCrcMismatch` — un metablock manipolato non
  viene mai analizzato.
- **Errori strutturali:** un header o un metablock difettoso, una
  lunghezza del metablock superiore a `Integer.MAX_VALUE` o un errore di
  I/O interrompono il caricamento con `OsfException.MalformedFile` —
  vedere [Gestione degli errori](./error-handling.md).

## `DataChannel` — i canali tipizzati

`DataChannel` è **una** classe; il suo layout di memorizzazione è
distinto da `kind()` tramite l'enum `DataChannel.Kind`:

```java
enum Kind { EQUIDISTANT, TIMESTAMPED, VARIABLE }
```

I confini dei blocchi su disco sono risolti; i campioni compaiono come
un'unica sequenza piatta con timestamp assoluti paralleli.

### Metadati comuni

```java
ch.index();          // int    — On-Disk-Kanalindex aus dem Metablock
ch.name();           // String — vollqualifizierter Name
ch.dataType();       // DataType — aufgelöster Datentyp der Samples
ch.channelType();    // ChannelType — Datenform (scalar/vector/matrix/binary)
ch.physicalUnit();   // String — physikalische Einheit, oder null
ch.kind();           // DataChannel.Kind — Speicherlayout
ch.sampleCount();    // long   — Anzahl Samples (Summe über alle Segmente)
ch.timestampsNs();   // long[] — absolute Zeitstempel, parallel zu den Werten
ch.segments();       // List<DataChannel.Segment> — nur bei EQUIDISTANT belegt
```

`timestampsNs()` restituisce l'array di backing del canale — non
modificarlo.

### `EQUIDISTANT` — segmenti anziché timestamp per campione

I canali equidistanti **non memorizzano alcun timestamp per campione**.
Riportano invece una sequenza piatta di valori più un elenco di
segmenti. Ogni blocco `bcStartData` del file apre un segmento, ogni
blocco `bcContinuedData` successivo estende il segmento più recente:

```java
public record Segment(long startTimestampNs, double sampleRateHz,
                      int startIndex, int sampleCount) {}
```

`timestampsNs()` **ricostruisce** i timestamp: il campione `i` di un
segmento si trova a
`startTimestampNs + (long)(i * 1e9 / sampleRateHz)` (troncato verso
zero, addizione con saturazione). I vuoti **tra** i segmenti non
vengono interpolati — ogni segmento inizia al proprio
`startTimestampNs`, una pausa di registrazione resta una pausa.

```java
DataChannel ch = mgr.channelByName("Beschleunigung.X").orElseThrow();
for (DataChannel.Segment seg : ch.segments()) {
    // seg.startTimestampNs(), seg.sampleRateHz(), seg.startIndex(), seg.sampleCount()
}
double[] werte = ch.asDoubles();      // flacher Lauf über alle Segmente
long[]   zeit  = ch.timestampsNs();   // dazu passende, rekonstruierte Zeitstempel
```

### `TIMESTAMPED` — sequenze parallele

Canali numerici o GPS con timestamp espliciti: `timestampsNs()` e la
sequenza di valori procedono in parallelo. I blocchi
`bcAbsTimeStampData` finiscono direttamente qui; i delta
`bcContinuedRelStampData` di OSF4 vengono sommati in timestamp assoluti
durante il caricamento (ancora = ultimo timestamp assoluto osservato del
canale).

### `VARIABLE` — String e Binary

Canali String e Binary: sempre tramite `bcAbsTimeStampData` con un
timestamp per campione.

```java
String[] texte = ch.asStrings();     // string-Kanal
byte[][] blobs = ch.asBinaries();    // binary-Kanal
```

La gestione del terminatore null è deterministica in base alla versione:
in **OSF4** il reader ha già rimosso l'ultimo byte, in **OSF5** il
payload arriva invariato.

### Accessor tipizzati

Ogni accessor proietta i valori memorizzati in un nuovo array e genera
`OsfException.UnsupportedType` se il `dataType()` non è adatto:

| Accessor | Restituisce | Valido per |
|---|---|---|
| `asDoubles()` | `double[]` | ogni tipo numerico (bool→0/1, tutte le larghezze intere, float, double) |
| `asLongs()` | `long[]` | tipi interi `int8`…`int64`, `uint8`…`uint64`, `bool` (unsigned esteso con zeri) |
| `asBooleans()` | `boolean[]` | solo `bool` |
| `asStrings()` | `String[]` | solo `string` |
| `asBinaries()` | `byte[][]` | solo `binary` |
| `asGps()` | `GpsLocation[]` | solo `gpslocation` |

Per `uint64`, `asLongs()` restituisce i bit grezzi — per l'output di
testo usare `Long.toUnsignedString(...)`. `GpsLocation` è un record
composto da `latitude`, `longitude` (gradi) e `altitude` (metri).

### Tipi di dati {#datentypen}

`DataType` copre `bool`, `int8`…`int64`, `uint8`…`uint64`, `float`,
`double`, `string`, `binary` e `gpslocation`; `bytearray` viene
accettato in lettura come alias di `binary`. Un tipo di dati sconosciuto
ma non rimosso diventa `DataType.UNSUPPORTED` (il file si carica, il
canale viene omesso); i tipi `pair`, `triple`, `candata` e `gpsdata`,
**rimossi** dallo standard OSF, provocano deliberatamente
`OsfException.UnsupportedType` in fase di risoluzione.

## Il livello del flusso di blocchi {#die-blockstrom-ebene}

Sotto il `DataManager` un lettore interno del flusso di blocchi
(`com.optimeas.osf.internal.BlockReader`) decodifica i blocchi binari
che seguono il metablock. Questo package **non** è esportato dal modulo
JPMS — in questa versione non esiste quindi alcuna API di streaming
pubblica; lato applicazione il `DataManager` è l'unico punto di
ingresso. Conviene tuttavia conoscerne il comportamento, perché spiega
la telemetria in `ReaderStats`:

- **Troncamento best effort:** un ultimo blocco corto o corrotto
  termina silenziosamente la lettura — tutto ciò che è stato decodificato
  prima viene conservato, `stats().truncationSeen()` viene impostato. Per
  il troncamento non viene mai generata un'eccezione.
- **I blocchi saltati restano visibili — solo tramite `ReaderStats`:**
  i byte di controllo deprecati (`blocksSkippedDeprecatedType()`) o
  riservati (`blocksSkippedReservedType()`) e i blocchi `bcStatusEvent`
  (`blocksSkippedStatusEvent()`) vengono scartati in base alla loro
  lunghezza senza parsing e conteggiati. `ReaderStats` è l'*unico* punto
  in cui un simile evento è osservabile: `com.optimeas.osf.internal` non
  è esportato, quindi i valori `Block.Skipped` sottostanti non
  raggiungono mai il codice applicativo. Anche i blocchi dei canali di
  tipo `UNSUPPORTED` vengono scartati, ma **non ancora** conteggiati da
  un campo di `ReaderStats`.
- **Trailer OSF4:** il blocco info opzionale `0xFFFF` insieme al
  trailer di 40 byte viene consumato silenziosamente.
- **Integrità:** con il profilo `crc` attivo ogni blocco riporta in coda
  un CRC32C sull'intero frame; viene verificato prima del parsing
  tipizzato (fail-closed). Un esito negativo salta il blocco e
  incrementa `blocksCrcFailed()`. I blocchi di firma sul canale
  riservato `0xFFFE` vengono saltati e conteggiati, in modo che un file
  firmato resti leggibile.

Il lettore del flusso di blocchi **non** decomprime autonomamente — gli
input OSFZ vengono decompressi prima (vedere sotto).

## OSFZ trasparente

`loadFromFile("x.osfz")` funziona senza ulteriori interventi: prima del
magic header la catena di lettura verifica i primi due byte e, se
necessario, antepone un decompressore. Vengono riconosciuti gzip
(`1F 8B`) e zlib (`78` seguito da `01`/`5E`/`9C`/`DA`); un vero OSF
inizia con `O` = `0x4F` e quindi non entra mai in collisione. La
decompressione utilizza le dotazioni standard del JDK `java.util.zip`
(`GZIPInputStream` o `InflaterInputStream`) ed è in streaming. Il
rilevamento viene documentato in `stats().compressed()` e
`stats().compressionFormat()` (`"gzip"` o `"zlib"`); per i file non
compressi l'etichetta resta `"none"`.

## `ReaderStats` — telemetria

Dopo ogni caricamento tramite `mgr.stats()`:

| Campo | Significato |
|---|---|
| `blocksRead()` | Numero di blocchi decodificati per intero |
| `truncationSeen()` | Il flusso è terminato su un blocco parziale/corrotto |
| `compressed()` / `compressionFormat()` | Riconoscimento OSFZ (`"none"` / `"gzip"` / `"zlib"`) |
| `integrity()` | Profilo di integrità dichiarato dall'header (`NONE` / `CRC32C` / `ED25519`) |
| `blocksCrcFailed()` | Blocchi di dati il cui CRC32C di frame non è stato verificato (saltati) |
| `blocksSignatureSkipped()` | Blocchi di firma saltati (canale riservato `0xFFFE`) |
| `blocksSkippedZeroLength()` | Blocchi saltati a causa del campo di lunghezza `0` |
| `blocksSkippedStatusEvent()` | Blocchi `bcStatusEvent` saltati (byte di controllo 3) |
| `blocksSkippedReservedType()` | Blocchi saltati con byte di controllo riservato (0, 2, ≥9) nonché le due varianti non specificate di `bcMessageEvent` |
| `blocksSkippedDeprecatedType()` | Blocchi saltati con byte di controllo deprecato (`bcTrustedTimestamp`, byte di controllo 1) |
| `verificationStatus()` | Stato di verifica riepilogativo (vedere sotto) |

`verificationStatus()` riassume l'esito dell'integrità in una stringa:

- `"none"` — nessun profilo di integrità;
- `"crc_valid"` — livello `crc`, ogni CRC di blocco verificato;
- `"invalid"` — livello `crc`, almeno un blocco è fallito al controllo
  CRC;
- `"signature_unverifiable"` — un file firmato le cui firme questo
  lettore `crc` non è in grado di verificare.

```java
ReaderStats s = mgr.stats();
System.out.printf("%d Blöcke, Integrität=%s, komprimiert=%s (%s)%n",
        s.blocksRead(), s.verificationStatus(),
        s.compressed(), s.compressionFormat());
```

## Note sulle prestazioni

- Il `DataManager` mantiene tutti i campioni in memoria, in array
  primitivi (`double[]`, `long[]`, …) senza boxing. Come regola
  empirica, un file richiede circa la propria dimensione decompressa in
  RAM. Non esiste un'API di streaming pubblica — i grandi volumi di
  dati si elaborano quindi al meglio file per file oppure filtrando
  prima del caricamento.
- Gli accessor tipizzati (`asDoubles()`, `asLongs()`, …) **copiano** a
  ogni chiamata in un nuovo array. Nei cicli conservare il risultato una
  sola volta in una variabile locale, anziché richiamare ripetutamente
  l'accessor.
- I file reali sul campo nell'ordine di pochi MB si caricano in
  millisecondi. La decompressione OSFZ trasparente avviene in streaming
  e non richiede un secondo buffer per l'intero file.

Si prosegue con la [Scrittura](./writing.md), la
[Gestione degli errori](./error-handling.md), gli
[Strumenti](./tools.md) oppure l'[Architettura](./architecture.md); il
formato binario stesso è descritto dalla
[specifica OSF](../../osf_general.md).

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
