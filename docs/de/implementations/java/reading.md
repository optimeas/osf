---
title: Lesen
description: OSF- und OSFZ-Dateien in Java lesen — DataManager, DataChannel, der Blockstrom, ReaderStats und transparente Dekompression
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
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Lesen

Die Java-Implementierung liest **OSF4**, **OSF5** und transparent
**OSFZ** (gzip/zlib) über dieselbe API. Sie liest zusätzlich das
`crc`-Integritätsprofil und prüft dabei jede Prüfsumme
([Fehlerbehandlung](./error-handling.md)). Öffentlicher Einstiegspunkt
ist genau eine Klasse:

- **`com.optimeas.osf.DataManager`** — der Standard- und einzige
  öffentliche Leseweg. Er lädt die Datei komplett, setzt aus dem
  Block-Strom typisierte Kanäle zusammen und löst alle Blockgrenzen auf.
  Für Analyse, Export und Tooling.

Der eigentliche Blockstrom-Decoder arbeitet darunter im nicht
exportierten Paket `com.optimeas.osf.internal` — er ist ein
Implementierungsdetail des `DataManager` und keine öffentliche Fläche
(siehe Abschnitt [Die Blockstrom-Ebene](#die-blockstrom-ebene)).

## Schnellstart

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

### Laden

| Methode | Quelle | Hinweise |
|---|---|---|
| `DataManager.loadFromFile(Path)` | Datei | OSF, OSFZ; öffnet und schließt den Stream selbst |
| `DataManager.load(InputStream)` | beliebiger `InputStream` | wird vollständig konsumiert; muss am Dateianfang stehen |

Beide Wege durchlaufen dieselbe Pipeline: OSFZ-Erkennung →
Magic-Header → (bei `crc` die Metablock-Prüfsumme) → Metablock-Parser
(JSON für OSF5, XML/StAX für OSF4) → Blockstrom-Decoder bis EOF →
Kanal-Zusammenbau. Das Ergebnis ist unveränderlich und darf von
beliebig vielen Threads gleichzeitig gelesen werden.

### Zugriff

```java
mgr.version();                 // OsfVersion — OSF4 oder OSF5, aus dem Magic-Header
mgr.metadata();                // Map<String,String> — Datei-Metadaten aus dem "file"-Block
mgr.stats();                   // ReaderStats — Telemetrie des Ladevorgangs
mgr.channels();                // List<DataChannel> — Metablock-Reihenfolge
mgr.channelByName("a.b.c");    // Optional<DataChannel> — leer, wenn unbekannt
mgr.channelByIndex(7);         // Optional<DataChannel> — Index aus dem Metablock
```

`channelByName` ist die primäre Zugriffsform; `channelByIndex` ist
Komfort. Beide liefern ein leeres `Optional` statt eines Fehlers, weil
„Kanal nicht vorhanden" beim Erkunden fremder Dateien ein normaler Fall
ist. Bei doppelt vergebenem Namen gewinnt die **erste** Definition.

Die `metadata()`-Schlüssel entsprechen exakt den Wire-Feldnamen des
`file`-Blocks (etwa `creator`, `created_utc`, `tag`, `reason`).

### Was beim Laden passieren kann

- **Trunkierte Datei:** keine Exception. Alle vollständig lesbaren
  Blöcke landen in den Kanälen, `mgr.stats().truncationSeen() == true`.
- **Unbekannter (zukünftiger) Datentyp:** Der Kanal wird aus
  `channels()` **weggelassen** (seine Blöcke wurden auf Reader-Ebene
  übersprungen). Ein unbekannter *Kanaltyp* (die Datenform) lässt den
  Kanal dagegen erhalten — die Lesbarkeit hängt allein am Datentyp und
  an den Blocktypen, nie am `channeltype`.
- **Metablock-Prüfsummenfehler** (nur bei `crc`): Der Ladevorgang bricht
  **fail-closed** mit `OsfException.MetablockCrcMismatch` ab — ein
  manipulierter Metablock wird nie geparst.
- **Strukturfehler:** ein defekter Header oder Metablock, eine
  Metablock-Länge über `Integer.MAX_VALUE` oder ein E/A-Fehler brechen
  das Laden mit `OsfException.MalformedFile` ab — siehe
  [Fehlerbehandlung](./error-handling.md).

## `DataChannel` — die typisierten Kanäle

`DataChannel` ist **eine** Klasse; ihr Speicherlayout unterscheidet
`kind()` über das Enum `DataChannel.Kind`:

```java
enum Kind { EQUIDISTANT, TIMESTAMPED, VARIABLE }
```

Die On-Disk-Blockgrenzen sind aufgelöst; die Samples erscheinen als ein
flacher Lauf mit parallelen absoluten Zeitstempeln.

### Gemeinsame Metadaten

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

`timestampsNs()` gibt das kanaleigene Backing-Array zurück — nicht
verändern.

### `EQUIDISTANT` — Segmente statt Zeitstempel pro Sample

Äquidistante Kanäle speichern **keinen Zeitstempel pro Sample**.
Stattdessen tragen sie einen flachen Werte-Lauf plus eine Segmentliste.
Jeder `bcStartData`-Block der Datei öffnet ein Segment, jeder folgende
`bcContinuedData`-Block verlängert das jüngste Segment:

```java
public record Segment(long startTimestampNs, double sampleRateHz,
                      int startIndex, int sampleCount) {}
```

`timestampsNs()` **rekonstruiert** die Zeitstempel: Sample `i` eines
Segments liegt bei `startTimestampNs + (long)(i * 1e9 / sampleRateHz)`
(gegen null abgeschnitten, sättigende Addition). Lücken **zwischen**
Segmenten werden nicht interpoliert — jedes Segment beginnt bei seinem
eigenen `startTimestampNs`, eine Aufzeichnungspause bleibt eine Pause.

```java
DataChannel ch = mgr.channelByName("Beschleunigung.X").orElseThrow();
for (DataChannel.Segment seg : ch.segments()) {
    // seg.startTimestampNs(), seg.sampleRateHz(), seg.startIndex(), seg.sampleCount()
}
double[] werte = ch.asDoubles();      // flacher Lauf über alle Segmente
long[]   zeit  = ch.timestampsNs();   // dazu passende, rekonstruierte Zeitstempel
```

### `TIMESTAMPED` — parallele Läufe

Numerische bzw. GPS-Kanäle mit expliziten Zeitstempeln:
`timestampsNs()` und der Werte-Lauf laufen parallel.
`bcAbsTimeStampData`-Blöcke landen direkt hier;
OSF4-`bcContinuedRelStampData`-Deltas werden beim Laden zu absoluten
Zeitstempeln aufaddiert (Anker = zuletzt beobachteter absoluter
Zeitstempel des Kanals).

### `VARIABLE` — String und Binary

String- und Binary-Kanäle: immer per `bcAbsTimeStampData` mit einem
Zeitstempel pro Sample.

```java
String[] texte = ch.asStrings();     // string-Kanal
byte[][] blobs = ch.asBinaries();    // binary-Kanal
```

Die Null-Terminator-Behandlung ist versions-deterministisch: bei
**OSF4** hat der Reader das letzte Byte bereits abgeschnitten, bei
**OSF5** kommt die Payload unverändert an.

### Typisierte Accessoren

Jeder Accessor projiziert die gespeicherten Werte in ein frisches Array
und wirft `OsfException.UnsupportedType`, wenn der `dataType()` nicht
passt:

| Accessor | Rückgabe | gültig für |
|---|---|---|
| `asDoubles()` | `double[]` | jeden numerischen Typ (bool→0/1, alle Ganzzahlbreiten, float, double) |
| `asLongs()` | `long[]` | Ganzzahltypen `int8`…`int64`, `uint8`…`uint64`, `bool` (unsigned nullerweitert) |
| `asBooleans()` | `boolean[]` | nur `bool` |
| `asStrings()` | `String[]` | nur `string` |
| `asBinaries()` | `byte[][]` | nur `binary` |
| `asGps()` | `GpsLocation[]` | nur `gpslocation` |

Bei `uint64` liefert `asLongs()` die rohen Bits — für die Textausgabe
`Long.toUnsignedString(...)` benutzen. `GpsLocation` ist ein Record aus
`latitude`, `longitude` (Grad) und `altitude` (Meter).

### Datentypen

`DataType` deckt `bool`, `int8`…`int64`, `uint8`…`uint64`, `float`,
`double`, `string`, `binary` und `gpslocation` ab; `bytearray` wird
beim Lesen als Alias für `binary` akzeptiert. Ein unbekannter, aber
nicht entfernter Datentyp wird zu `DataType.UNSUPPORTED` (die Datei
lädt, der Kanal entfällt); die vom OSF-Standard **entfernten** Typen
`pair`, `triple`, `candata` und `gpsdata` lösen beim Auflösen bewusst
`OsfException.UnsupportedType` aus.

## Die Blockstrom-Ebene

Unter dem `DataManager` decodiert ein interner Blockstrom-Leser
(`com.optimeas.osf.internal.BlockReader`) die Binärblöcke, die auf den
Metablock folgen. Dieses Paket ist **nicht** aus dem JPMS-Modul
exportiert — es gibt in dieser Version also keine öffentliche
Streaming-API; anwendungsseitig ist der `DataManager` der einzige
Einstieg. Sein Verhalten sollte man dennoch kennen, weil es die
Telemetrie in `ReaderStats` erklärt:

- **Best-Effort-Trunkierung:** Ein kurzer oder korrupter letzter Block
  beendet das Lesen still — alles davor Dekodierte bleibt erhalten,
  `stats().truncationSeen()` wird gesetzt. Es wird nie auf Trunkierung
  geworfen.
- **Übersprungene Blöcke bleiben sichtbar:** Deprecated bzw.
  reservierte Control-Bytes und Blöcke `UNSUPPORTED`-typisierter Kanäle
  werden ohne Parsen anhand ihrer Länge verworfen und in `ReaderStats`
  gezählt.
- **OSF4-Trailer:** Der optionale `0xFFFF`-Infoblock samt
  40-Byte-Trailer wird stillschweigend konsumiert.
- **Integrität:** Bei aktivem `crc`-Profil trägt jeder Block eine
  abschließende CRC32C über den gesamten Frame; sie wird vor dem
  typisierten Parsen geprüft (fail-closed). Ein Fehlschlag überspringt
  den Block und erhöht `blocksCrcFailed()`. Signaturblöcke auf dem
  reservierten Kanal `0xFFFE` werden übersprungen und gezählt, damit
  eine signierte Datei lesbar bleibt.

Der Blockstrom-Leser dekomprimiert **nicht** selbst — OSFZ-Eingaben
werden vorher entpackt (siehe unten).

## Transparentes OSFZ

`loadFromFile("x.osfz")` funktioniert ohne weiteres Zutun: vor dem
Magic-Header prüft die Lesekette die ersten zwei Bytes und schaltet bei
Bedarf einen Dekompressor davor. Erkannt werden gzip (`1F 8B`) und zlib
(`78` gefolgt von `01`/`5E`/`9C`/`DA`); echtes OSF beginnt mit
`O` = `0x4F`, kollidiert also nie. Die Dekompression nutzt die
JDK-Bordmittel `java.util.zip` (`GZIPInputStream` bzw.
`InflaterInputStream`) und ist streamend. Der Fund wird in
`stats().compressed()` und `stats().compressionFormat()` (`"gzip"` oder
`"zlib"`) dokumentiert; für unkomprimierte Dateien bleibt das Label
`"none"`.

## `ReaderStats` — Telemetrie

Nach jedem Ladevorgang über `mgr.stats()`:

| Feld | Bedeutung |
|---|---|
| `blocksRead()` | Anzahl vollständig dekodierter Blöcke |
| `truncationSeen()` | Strom endete auf einem partiellen/korrupten Block |
| `compressed()` / `compressionFormat()` | OSFZ-Erkennung (`"none"` / `"gzip"` / `"zlib"`) |
| `integrity()` | vom Header deklariertes Integritätsprofil (`NONE` / `CRC32C` / `ED25519`) |
| `blocksCrcFailed()` | Datenblöcke, deren Frame-CRC32C nicht verifizierte (übersprungen) |
| `blocksSignatureSkipped()` | übersprungene Signaturblöcke (reservierter Kanal `0xFFFE`) |
| `verificationStatus()` | zusammenfassender Prüfstatus (siehe unten) |

`verificationStatus()` fasst den Integritätsbefund zu einem String
zusammen:

- `"none"` — kein Integritätsprofil;
- `"crc_valid"` — Level `crc`, jede Block-CRC verifizierte;
- `"invalid"` — Level `crc`, mindestens ein Block fiel bei der CRC durch;
- `"signature_unverifiable"` — eine signierte Datei, deren Signaturen
  dieser `crc`-Leser nicht prüfen kann.

```java
ReaderStats s = mgr.stats();
System.out.printf("%d Blöcke, Integrität=%s, komprimiert=%s (%s)%n",
        s.blocksRead(), s.verificationStatus(),
        s.compressed(), s.compressionFormat());
```

## Performance-Hinweise

- Der `DataManager` hält alle Samples im Speicher, in primitiven Arrays
  (`double[]`, `long[]`, …) ohne Boxing. Als Faustregel braucht eine
  Datei etwa ihre entpackte Größe an RAM. Eine öffentliche
  Streaming-API gibt es nicht — sehr große Bestände verarbeitet man
  daher am besten dateiweise oder filtert vor dem Laden.
- Die typisierten Accessoren (`asDoubles()`, `asLongs()`, …) **kopieren**
  bei jedem Aufruf in ein neues Array. In Schleifen das Ergebnis einmal
  in einer lokalen Variablen halten, statt den Accessor wiederholt
  aufzurufen.
- Reale Felddateien im einstelligen MB-Bereich laden in Millisekunden.
  Die transparente OSFZ-Dekompression läuft streamend und braucht keinen
  zweiten Puffer für die ganze Datei.

Weiter geht es beim [Schreiben](./writing.md), bei der
[Fehlerbehandlung](./error-handling.md), den [Werkzeugen](./tools.md)
oder in der [Architektur](./architecture.md); das Binärformat selbst
beschreibt die [OSF-Spezifikation](../../osf_general.md).

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
