---
title: Cookbook
description: Ricette per attività tipiche con la libreria OSF Java — dall'ispezione dei file all'esportazione CSV fino al ciclo di registrazione embedded a prova di interruzione
sidebar_position: 7
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - Esempi
  - Cookbook
  - Ricette
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Cookbook

Ricette compatte e pronte per la copia per la libreria Java. Tutte
presuppongono il modulo JPMS `com.optimeas.osf` sul module path
(Java 21) e importano i tipi pubblici dal package `com.optimeas.osf`:

```java
import com.optimeas.osf.*;
import java.nio.file.Path;
```

La gestione degli errori è ridotta al minimo. Le API di lettura e
scrittura segnalano gli errori tramite l'eccezione non controllata
`OsfException` (vedere [Gestione degli errori](./error-handling.md));
per la panoramica complessiva delle classi vedere
[Architettura](./architecture.md), [Lettura](./reading.md) e
[Scrittura](./writing.md).

## Ispezionare un file (metadati + elenco dei canali)

`loadFromFile` riconosce in modo trasparente OSF4, OSF5 e **OSFZ**
compresso (gzip/zlib) dai primi byte — la stessa chiamata per `.osf` e
`.osfz`.

```java
DataManager mgr = DataManager.loadFromFile(Path.of(pfad));   // .osf oder .osfz

System.out.println("version:  " + mgr.version());            // OSF4 / OSF5
System.out.println("creator:  " + mgr.metadata().getOrDefault("creator", "-"));
System.out.println("created:  " + mgr.metadata().getOrDefault("created_utc", "-"));
System.out.println("channels: " + mgr.channels().size());

for (DataChannel ch : mgr.channels()) {
    System.out.printf("  [%d] %s  (%d Samples, Einheit: %s)%n",
            ch.index(), ch.name(), ch.sampleCount(),
            ch.physicalUnit() == null ? "-" : ch.physicalUnit());
}
```

## Ottenere un canale come valori `double` con timestamp

`asDoubles()` estende ogni tipo di dati numerico a `double`;
`timestampsNs()` restituisce l'array parallelo dei timestamp —
ricostruito da ora di inizio e frequenza di campionamento nei canali
equidistanti, esplicito nei canali timestamped. Entrambi gli array hanno
la stessa lunghezza.

```java
DataChannel ch = mgr.channelByName("Motor.Drehzahl").orElseThrow();

long[]   ts = ch.timestampsNs();
double[] v  = ch.asDoubles();          // wirft OsfException.UnsupportedType, wenn nicht numerisch

for (int i = 0; i < v.length; i++) {
    long   tNs   = ts[i];
    double wert  = v[i];
    // … tNs, wert verarbeiten
}
```

`channelByName` restituisce un `Optional<DataChannel>`; in alternativa
`channelByIndex(int)` accede tramite l'indice del metablock.

## Iterare in modo generico su tipi di dati misti

Uno `switch` su `dataType()` rende gli strumenti di esportazione
indipendenti dal tipo di dati. Ogni accesso `as…()` genera
`OsfException.UnsupportedType` se non è adatto al canale — lo `switch`
lo evita.

```java
long[] ts = ch.timestampsNs();
switch (ch.dataType()) {
    case DOUBLE, FLOAT, INT8, INT16, INT32, INT64,
         UINT8, UINT16, UINT32, UINT64, BOOL -> {
        double[] v = ch.asDoubles();
        for (int i = 0; i < v.length; i++) row(ts[i], v[i]);
    }
    case GPS_LOCATION -> {
        GpsLocation[] g = ch.asGps();
        for (int i = 0; i < g.length; i++) row(ts[i], g[i].latitude(), g[i].longitude());
    }
    case STRING -> {
        String[] s = ch.asStrings();
        for (int i = 0; i < s.length; i++) row(ts[i], s[i]);
    }
    case BINARY -> {
        byte[][] b = ch.asBinaries();
        for (int i = 0; i < b.length; i++) row(ts[i], b[i].length + " Bytes");
    }
    default -> { /* UNSUPPORTED: Kanal geladen, aber Werte nicht projizierbar */ }
}
```

## Esportazione CSV minimale

```java
import java.io.BufferedWriter;
import java.nio.file.Files;

DataChannel ch = mgr.channelByName(name).orElseThrow();
long[]   ts = ch.timestampsNs();
double[] v  = ch.asDoubles();

try (BufferedWriter w = Files.newBufferedWriter(Path.of("kanal.csv"))) {
    w.write("timestamp_ns,value\n");
    for (int i = 0; i < v.length; i++) {
        w.write(ts[i] + "," + v[i] + "\n");
    }
}
```

Una tabella larga (una colonna per canale, una riga per timestamp) viene
generata dallo strumento `osf-cli` con `osf convert --to csv` — vedere
[Strumenti](./tools.md).

## Convertire OSF4 → OSF5 (anche con input OSFZ)

`BlockWriter.fromManager` ricostruisce un writer da un file caricato —
metadati, definizioni dei canali e tutti i campioni. L'output è
**sempre OSF5**, indipendentemente dal formato di origine; i campioni
restano identici bit per bit.

```java
DataManager mgr = DataManager.loadFromFile(Path.of("alt_osf4.osf")); // oder .osfz
BlockWriter.fromManager(mgr).writeToFile(Path.of("neu_osf5.osf"));
```

Per un **output OSFZ** scrivere il `BlockWriter` in un
`GZIPOutputStream`:

```java
import java.util.zip.GZIPOutputStream;

try (var os = new GZIPOutputStream(Files.newOutputStream(Path.of("neu.osfz")))) {
    BlockWriter.fromManager(mgr).writeTo(os);
}
```

## Scrivere un nuovo file con dati di analisi

Il `BlockWriter` accumula tutti i campioni in memoria e scrive il file
in un unico passaggio. I canali vengono prima dichiarati (il valore di
ritorno è l'indice del canale) e poi riempiti.

```java
BlockWriter w = new BlockWriter();
w.setMetadata("creator", "mein-tool/1.0");

int fftPeak = w.addTimestampedChannel("ergebnis.fft_peak", DataType.DOUBLE);

for (var e : ergebnisse) {
    w.writeSample(fftPeak, e.timestampNs(), e.peakHz());
}

w.writeToFile(Path.of("ergebnis.osf"));   // created_utc wird automatisch gesetzt
```

Per serie di analisi equidistanti (frequenza fissa, solo
`float`/`double`) anziché timestamped:

```java
int spektrum = w.addEquidistantChannel("ergebnis.psd", DataType.DOUBLE, 2, 1000.0); // 1 kHz
w.startEquidistantSegment(spektrum, startNs, block1);   // double[]
w.appendEquidistantSamples(spektrum, block2);           // hängt an dasselbe Segment an
```

## Ciclo di registrazione embedded a prova di interruzione

Lo `StreamingWriter` scrive immediatamente il preambolo e ogni blocco
completato e dopo ogni blocco chiama `force(true)` (fsync). La
durabilità è quindi **per blocco**: dopo un'interruzione
dell'alimentazione il `DataManager` legge il file fino all'ultimo
blocco completo e imposta `stats().truncationSeen()` per i byte residui
troncati. Implementa `Closeable` — il `try`-with-resources scrive i
blocchi residui, esegue il force e chiude.

```java
try (StreamingWriter w = StreamingWriter.create(Path.of("/data/rec_0001.osf"))) {
    w.setMetadata("creator", "logger-fw/3.2");

    int temp = w.addTimestampedChannel("temp", DataType.DOUBLE, 2, "degC", null);
    int tuer = w.addTimestampedChannel("tuer", DataType.BOOL, 2);   // Event-Kanal
    w.begin();   // Präambel früh festschreiben (sonst lazy beim ersten Sample)

    while (laeuft) {
        long now = jetztNs();

        if (neuerMesswert)  w.writeSample(temp, now, wert);
        if (tuerGeaendert)  w.writeSample(tuer, now, offen);

        // Optional: w.flush() erzwingt die noch offenen Teilblöcke sofort.
        warteAufNaechstenTick();
    }
}   // close(): Restblöcke schreiben, force, schließen
```

Lo `StreamingWriter` fissa `sizeoflengthvalue` per canale e non può
aumentarlo in un secondo momento — i canali con grandi campioni
variabili (vedere sotto) vanno quindi dichiarati subito con `4`.

## Immagini/blob come canale Binary

I grandi campioni variabili (JPEG, buffer grezzi) superano facilmente il
campo di lunghezza di 2 byte; per questo il canale va creato con
`sizeoflengthvalue = 4`. Tramite la mappa degli attributi è possibile
scrivere un `mimetype` nel metablock. I campioni variabili non vengono
mai raggruppati — un blocco per campione.

```java
// Schreiben (StreamingWriter — sov=4 wegen Sample-Größe):
int kamera = w.addTimestampedChannel(
        "kamera.snapshots", DataType.BINARY, 4,
        null, java.util.Map.of("mimetype", "image/jpeg"));

w.writeSample(kamera, tsNs, jpegBytes);   // byte[]
```

```java
// Lesen:
DataChannel ch = mgr.channelByName("kamera.snapshots").orElseThrow();
long[]   ts    = ch.timestampsNs();
byte[][] blobs = ch.asBinaries();

for (int i = 0; i < blobs.length; i++) {
    long   t    = ts[i];
    byte[] jpeg = blobs[i];
    // … jpeg speichern/decodieren
}
```

Il `BlockWriter` conosce in anticipo la dimensione massima dei campioni:
qui è sufficiente la forma abbreviata
`addTimestampedChannel(name, DataType.BINARY)`, che all'occorrenza
porta automaticamente `sizeoflengthvalue` da 2 a 4.

## Scrivere e verificare il profilo di integrità `crc`

Entrambi i writer possono generare il livello `crc`: un CRC32C sul
metablock nel magic header più un CRC32C di frame per ogni blocco.

```java
BlockWriter w = new BlockWriter();
w.setIntegrity(IntegrityProfile.CRC32C);
// … Kanäle + Samples …
w.writeToFile(Path.of("gesichert.osf"));
```

In lettura il `DataManager` verifica in modalità fail-closed: un CRC del
metablock errato genera `OsfException.MetablockCrcMismatch`, mentre i
blocchi difettosi vengono saltati e conteggiati.

```java
DataManager mgr = DataManager.loadFromFile(Path.of("gesichert.osf"));
ReaderStats s = mgr.stats();

System.out.println(s.verificationStatus());   // "crc_valid" oder "invalid"
if (s.blocksCrcFailed() > 0) {
    System.out.println("ACHTUNG: " + s.blocksCrcFailed() + " Blöcke mit CRC-Fehler");
}
```

## Visualizzare le statistiche di lettura

```java
ReaderStats s = mgr.stats();
System.out.printf("Blöcke gelesen: %d%n", s.blocksRead());
if (s.truncationSeen()) {
    System.out.println("ACHTUNG: Datei war abgeschnitten");
}
if (s.compressed()) {
    System.out.println("Quelle war OSFZ (" + s.compressionFormat() + ")");
}
```

I dettagli sugli aspetti interni (flusso di blocchi, assembler,
chunking) sono in [Interni](./internals.md); build e coordinate Maven in
[Build](./building.md). La panoramica di tutti gli strumenti è
disponibile nella [pagina iniziale Java](../java.md).

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
