---
title: Kochbuch
description: Rezepte für typische Aufgaben mit der OSF-Java-Bibliothek — von der Datei-Inspektion über CSV-Export bis zur ausfallsicheren Embedded-Aufzeichnungsschleife
sidebar_position: 7
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - Beispiele
  - Kochbuch
  - Rezepte
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Kochbuch

Kompakte, kopierfertige Rezepte für die Java-Bibliothek. Alle setzen das
JPMS-Modul `com.optimeas.osf` auf dem Modulpfad voraus (Java 21) und
importieren die öffentlichen Typen aus dem Paket `com.optimeas.osf`:

```java
import com.optimeas.osf.*;
import java.nio.file.Path;
```

Fehlerbehandlung ist auf das Minimum gekürzt. Die Lese- und Schreib-APIs
melden Fehler über die ungeprüfte `OsfException` (siehe
[Fehlerbehandlung](./error-handling.md)); für den Gesamtüberblick über die
Klassen siehe [Architektur](./architecture.md), [Lesen](./reading.md) und
[Schreiben](./writing.md).

## Datei inspizieren (Metadaten + Kanalliste)

`loadFromFile` erkennt OSF4, OSF5 und komprimiertes **OSFZ** (gzip/zlib)
transparent an den ersten Bytes — derselbe Aufruf für `.osf` und `.osfz`.

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

## Einen Kanal als `double`-Werte mit Zeitstempeln holen

`asDoubles()` verbreitert jeden numerischen Datentyp auf `double`;
`timestampsNs()` liefert das parallele Zeitstempel-Array — bei äquidistanten
Kanälen aus Startzeit und Abtastrate rekonstruiert, bei Timestamped-Kanälen
explizit. Beide Arrays sind gleich lang.

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

`channelByName` liefert ein `Optional<DataChannel>`; alternativ greift
`channelByIndex(int)` über den Metablock-Index zu.

## Über gemischte Datentypen generisch iterieren

Ein `switch` über `dataType()` macht Export-Werkzeuge datentyp-agnostisch.
Jeder `as…()`-Zugriff wirft `OsfException.UnsupportedType`, wenn er nicht zum
Kanal passt — der `switch` verhindert das.

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

## Minimaler CSV-Export

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

Eine breite Tabelle (eine Spalte je Kanal, eine Zeile je Zeitstempel)
erzeugt das `osf-cli`-Werkzeug mit `osf convert --to csv` — siehe
[Werkzeuge](./tools.md).

## OSF4 → OSF5 konvertieren (auch OSFZ-Eingabe)

`BlockWriter.fromManager` rekonstruiert einen Writer aus einer geladenen
Datei — Metadaten, Kanaldefinitionen und alle Samples. Die Ausgabe ist
**immer OSF5**, unabhängig vom Quellformat; die Samples bleiben bitgenau.

```java
DataManager mgr = DataManager.loadFromFile(Path.of("alt_osf4.osf")); // oder .osfz
BlockWriter.fromManager(mgr).writeToFile(Path.of("neu_osf5.osf"));
```

Für **OSFZ-Ausgabe** den `BlockWriter` in einen `GZIPOutputStream` schreiben:

```java
import java.util.zip.GZIPOutputStream;

try (var os = new GZIPOutputStream(Files.newOutputStream(Path.of("neu.osfz")))) {
    BlockWriter.fromManager(mgr).writeTo(os);
}
```

## Neue Datei mit Analyse-Daten schreiben

Der `BlockWriter` sammelt alle Samples im Speicher und schreibt die Datei in
einem Durchgang. Kanäle werden zuerst deklariert (der Rückgabewert ist der
Kanal-Index), dann gefüllt.

```java
BlockWriter w = new BlockWriter();
w.setMetadata("creator", "mein-tool/1.0");

int fftPeak = w.addTimestampedChannel("ergebnis.fft_peak", DataType.DOUBLE);

for (var e : ergebnisse) {
    w.writeSample(fftPeak, e.timestampNs(), e.peakHz());
}

w.writeToFile(Path.of("ergebnis.osf"));   // created_utc wird automatisch gesetzt
```

Für äquidistante Analyse-Reihen (feste Rate, nur `float`/`double`) statt
Timestamped:

```java
int spektrum = w.addEquidistantChannel("ergebnis.psd", DataType.DOUBLE, 2, 1000.0); // 1 kHz
w.startEquidistantSegment(spektrum, startNs, block1);   // double[]
w.appendEquidistantSamples(spektrum, block2);           // hängt an dasselbe Segment an
```

## Ausfallsichere Embedded-Aufzeichnungsschleife

Der `StreamingWriter` schreibt Präambel und jeden fertigen Block sofort und
ruft nach jedem Block `force(true)` (fsync). Durabilität ist damit
**pro Block**: Nach einem Stromausfall liest der `DataManager` die Datei bis
zum letzten vollständigen Block und setzt `stats().truncationSeen()` für die
angeschnittenen Reststbytes. Er implementiert `Closeable` — `try`-with-resources
schreibt Restblöcke, forced und schließt.

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

Der `StreamingWriter` fixiert `sizeoflengthvalue` je Kanal und kann es nicht
nachträglich anheben — Kanäle mit großen variablen Samples (siehe unten) also
gleich mit `4` deklarieren.

## Bilder/Blobs als Binary-Kanal

Große variable Samples (JPEGs, Rohpuffer) überschreiten leicht das 2-Byte-
Längenfeld, deshalb den Kanal mit `sizeoflengthvalue = 4` anlegen. Über die
Attribute-Map lässt sich ein `mimetype` in den Metablock schreiben. Variable
Samples werden nie gebündelt — ein Block pro Sample.

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

Der `BlockWriter` kennt die maximale Sample-Größe vorab: dort genügt die
Kurzform `addTimestampedChannel(name, DataType.BINARY)`, die `sizeoflengthvalue`
bei Bedarf automatisch von 2 auf 4 anhebt.

## Integritätsprofil `crc` schreiben und prüfen

Beide Writer können das Level `crc` erzeugen: eine CRC32C über den Metablock
im Magic-Header plus eine Frame-CRC32C je Block.

```java
BlockWriter w = new BlockWriter();
w.setIntegrity(IntegrityProfile.CRC32C);
// … Kanäle + Samples …
w.writeToFile(Path.of("gesichert.osf"));
```

Beim Lesen verifiziert der `DataManager` fail-closed: eine falsche
Metablock-CRC wirft `OsfException.MetablockCrcMismatch`, defekte Blöcke
werden übersprungen und gezählt.

```java
DataManager mgr = DataManager.loadFromFile(Path.of("gesichert.osf"));
ReaderStats s = mgr.stats();

System.out.println(s.verificationStatus());   // "crc_valid" oder "invalid"
if (s.blocksCrcFailed() > 0) {
    System.out.println("ACHTUNG: " + s.blocksCrcFailed() + " Blöcke mit CRC-Fehler");
}
```

## Lese-Statistik ausgeben

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

Details zu den Interna (Blockstrom, Assembler, Chunking) stehen unter
[Interna](./internals.md); Bau und Maven-Koordinaten unter
[Bauen](./building.md). Die Übersicht aller Werkzeuge liefert die
[Java-Startseite](../java.md).

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
