---
title: Schreiben
description: OSF5 schreiben mit osf-java — StreamingWriter (embedded, ausfallsicher), BlockWriter (analystenfreundlich), das crc-Integritätsprofil und Round-Trip
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
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Schreiben

Die Bibliothek schreibt **ausschließlich OSF5** — auch dann, wenn die Quelle
eine OSF4- oder OSFZ-Datei war. Zwei Writer-Klassen im Paket
`com.optimeas.osf` decken zwei sehr unterschiedliche Einsatzprofile ab:

| | `StreamingWriter` | `BlockWriter` |
|---|---|---|
| Einsatz | Embedded-Aufzeichnung | Analyse, Konvertierung, Export |
| Speicher | beschränkt (ein Block-Puffer je Kanal, nach Emit verworfen) | sammelt alle Samples im RAM |
| Durabilität | `FileChannel.force(true)` pro Block — ausfallsicher bei Stromverlust | kein `force`; Datei entsteht am Ende |
| Senke | Dateipfad (`Path`) | `Path` **oder** beliebiger `OutputStream` (Memory, Socket) |
| `sizeOfLengthValue` | fix ab Kanaldeklaration (Metablock liegt auf Platte) | automatischer Bump 2 → 4 bei variablen Kanälen |
| Lebenszyklus | `create()` → Kanäle → `begin()` → Schreiben → `close()` | `new` → Sammeln → `writeToFile()` / `writeTo()` |
| Mehrfach-Emission | nein (eine Datei pro Instanz, `Closeable`) | ja (`writeTo*` beliebig oft aufrufbar) |

Beide teilen dieselben Kanaltypen, dieselbe Chunking-Arithmetik und dieselben
Schreibfamilien (äquidistant, timestamped numerisch, GPS, String/Binary). Für
dieselben Kanäle, Samples, `sizeOfLengthValue` und `created_utc` erzeugen sie
**byte-identische** OSF5-Dateien.

## Kanäle deklarieren — `ChannelDef`

Ein Kanal wird nicht direkt konstruiert, sondern über eine `add…Channel`-Methode
des Writers angemeldet, die intern eine `ChannelDef` erzeugt und den **Kanalindex**
zurückgibt (sequenziell ab 0), den alle Schreibaufrufe verwenden. Die
`ChannelDef` (ein `record`) beschreibt den Kanal so, wie er im Metablock steht:

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

Zwei Deklarationsarten je Writer:

```java
// Timestamped-Kanal (jedes Sample trägt seinen eigenen Zeitstempel):
int rpm = writer.addTimestampedChannel("motor.drehzahl", DataType.DOUBLE, 2);
int evt = writer.addTimestampedChannel("ereignis", DataType.STRING, 2,
                                       "1/min", Map.of("displayname", "Ereignis"));

// Äquidistanter Kanal (feste Rate; nur der Segmentstart trägt einen Zeitstempel):
int sig = writer.addEquidistantChannel("beschleunigung", DataType.DOUBLE, 4,
                                       1000.0 /* Hz */);
```

Abgelehnt werden mit `IllegalArgumentException`: leerer Name, `null`- oder
`UNSUPPORTED`-Datentyp, `sizeOfLengthValue` ≠ 2/4, ein äquidistanter Kanal mit
einem anderen Typ als `FLOAT`/`DOUBLE` sowie eine nicht positive oder nicht
endliche Abtastrate. Beim `StreamingWriter` löst jeder `add…Channel`-Aufruf nach
`begin()` eine `OsfException` aus (die Konfigurationsphase ist dann vorbei). Der
`channeltype` wird beim Schreiben stets auf `scalar` normalisiert — die
Äquidistanz trägt allein das `timeincrement`, nicht der `channeltype`.

### `sizeOfLengthValue` richtig wählen

Das Längenfeld jedes Blocks ist 2 oder 4 Bytes breit und begrenzt die
Blockgröße (~64 KB bzw. ~2 GB). Praktische Regeln:

- **String/Binary-Kanäle mit großen Samples** (Bilder, Audio, Blobs): beim
  `StreamingWriter` zwingend `4` deklarieren — er kann den Wert nach `begin()`
  nicht mehr ändern. Der `BlockWriter` hebt bei einem variablen Kanal, den er
  selbst dimensionieren darf, automatisch von 2 auf 4 an (Auto-Bump beim
  Emittieren), dort ist `2` als Startwert immer in Ordnung.
- **Numerische Kanäle** werden nie umgestellt — sie zerfallen stattdessen in
  mehr Blöcke. Am `StreamingWriter` erspart `4` bei hochratigen Kanälen das
  Chunking in viele kleine, einzeln fsync'te Blöcke.
- Sonst beim Standard `2` bleiben (kompaktere Blöcke).

## `StreamingWriter` — embedded, ausfallsicher

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

Garantien und Verhalten:

- **Jedes zurückgekehrte `writeSample` ist als vollständiger Block auf Platte**
  (`FileChannel.force(true)` = fsync). Nach einem Stromausfall bleibt die Datei
  bis zum letzten bestätigten Block lesbar; der Best-Effort-Reader stellt jeden
  Block vor dem Schnitt wieder her und markiert einen angeschnittenen Rest über
  `ReaderStats.truncationSeen()` (siehe [Lesen](./reading.md)).
- **Preamble lazy oder eager:** `begin()` schreibt die Magic-Header-Zeile
  (`OSF5 <len>\n`) und den JSON-Metablock einmal und fsync't sie; wird es nicht
  explizit gerufen, geschieht das automatisch beim ersten Sample. Danach wird der
  Metablock nie wieder angefasst — **Metadaten-Setter wirken nur vorher**.
- **`created_utc`** wird beim `begin()` automatisch gestempelt, falls nicht
  gesetzt (ISO-8601 UTC, z. B. `2026-07-11T08:30:00Z`).
- **Kanäle sind auf eine Blockfamilie festgelegt:** wer denselben Kanal einmal
  timestamped und einmal äquidistant beschreibt, bekommt eine `OsfException`.
- `close()` ist idempotent und emittiert eventuell gepufferte Restblöcke; als
  `Closeable` gehört der Writer in ein try-with-resources. Er ist **nicht
  thread-safe** — Zugriffe extern serialisieren.

### Schreibfamilien

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

`writeSample(int, long, long)` bedient jeden Integer-Kanal (`int8…int64`,
`uint8…uint64`); der Wert wird beim Kodieren auf die Kanalbreite verengt.
Jeder Batch- und jeder Segment-Aufruf wird automatisch auf die Blockkapazität
des Kanals gechunkt — ein fsync pro emittiertem Block. Ein neues
`startEquidistantSegment` schließt das vorige und öffnet bewusst ein **neues**
Segment; Lücken zwischen Segmenten sind das spec-gemäße Mittel für
Aufzeichnungspausen.

## `BlockWriter` — sammeln und emittieren

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

- Die `writeSample`- / `startEquidistantSegment`-Familie spiegelt die des
  Streaming-Writers (gleiche Typen, gleiche Validierung), sammelt aber nur im
  Speicher; das Chunking in spec-konforme Blöcke passiert erst beim Emittieren.
- `writeToFile` / `writeTo` dürfen **mehrfach** aufgerufen werden — dieselbe
  Instanz kann in Datei und Netzwerk zugleich geschrieben werden.
- **Auto-Bump:** Ein mit der zweiargumentigen `addTimestampedChannel(name, type)`
  angemeldeter variabler Kanal startet bei `sizeOfLengthValue = 2` und wird für
  die Emission auf `4` angehoben, falls sein größtes Sample das u16-Längenfeld
  sprengt. Die dreiargumentige Form mit explizitem Wert wird respektiert und
  lehnt ein zu großes Sample stattdessen ab (wie der StreamingWriter, der nicht
  anheben kann). Numerische Kanäle werden nie angehoben.
- Kein fsync — Durabilität liegt beim Aufrufer.
- `channelCount()` und `channelIndex("name")` helfen, wenn die Indizes nicht
  mitgeführt werden (`channelIndex` liefert `-1` bei Unbekanntem).

## Automatische Metadaten-Defaults

Beide Writer schreiben die per `setMetadata(key, value)` gesetzten Einträge
**verbatim** in das `osf.file`-Objekt des Metablocks. Der einzige automatische
Eingriff:

| Feld | Verhalten wenn nicht gesetzt |
|---|---|
| `created_utc` | **immer** gestempelt (aktuelle UTC-Zeit, `YYYY-MM-DDTHH:MM:SSZ`) — per `putIfAbsent`, ein bereits vorhandener Wert bleibt unangetastet |
| alle übrigen (`creator`, `tag`, …) | nur geschrieben, wenn gesetzt; nie als `null` |

Es gibt also keinen erzwungenen `creator`- oder `tag`-Default — was nicht
gesetzt wird, erscheint nicht im Metablock.

## Integritätsprofil beim Schreiben (`crc`)

Beide Writer können optional das OSF5-Integritätsprofil auf Stufe `crc`
erzeugen — standardmäßig ist es aus (`IntegrityProfile.NONE`):

```java
w.setIntegrity(IntegrityProfile.CRC32C);   // vor begin() bzw. writeTo
```

Eingeschaltet schreibt der Writer

- ein `crc32c`-Token in die Magic-Header-Zeile, das die **Metablock-CRC** trägt, und
- je Datenblock eine angehängte **Frame-CRC32C** (4 Bytes, im Längenfeld des
  Blocks mitgezählt). Beim `StreamingWriter` wird die Frame-CRC mit demselben
  `force(true)` wie der Block selbst durablisiert.

Die Prüfsumme ist `java.util.zip.CRC32C` (JDK-nativ). Die 4 Frame-CRC-Bytes
zehren vom Payload-Budget jedes Blocks, sodass etwas weniger Samples pro Block
passen — die Chunking-Arithmetik berücksichtigt das automatisch. Wie der Reader
diese Prüfwerte fail-closed verifiziert, steht unter
[Lesen](./reading.md) und [Fehlerbehandlung](./error-handling.md).

Die Signaturstufe (`IntegrityProfile.ED25519`) wird vom Writer **nicht**
unterstützt und beim Schreiben der Preamble mit einer `OsfException` abgelehnt.

## Round-Trip und Konvertierung

Einen geladenen `DataManager` wieder herausschreiben — zugleich die
OSF4 → OSF5- bzw. OSFZ → OSF5-Konvertierung:

```java
DataManager mgr = DataManager.loadFromFile(Path.of("alt.osf"));  // auch OSF4 / OSFZ
BlockWriter.fromManager(mgr).writeToFile(Path.of("neu.osf"));     // immer OSF5
```

`BlockWriter.fromManager(mgr)` baut einen Writer aus den typisierten Kanälen und
Samples des Managers; wer vor dem Schreiben filtern oder umbenennen will,
arbeitet auf dem zurückgegebenen Writer weiter.

Erhalten bleiben: Kanalnamen, Datentypen, Sample-Werte (bitgenau),
Segmentgrenzen und die Datei-Metadaten — **einschließlich `created_utc`**, denn
ein geladener Wert ist bereits gesetzt und wird nicht neu gestempelt. **Nicht**
übernommen wird das `sizeOfLengthValue` (der Writer beginnt bei 2 und hebt
variable Kanäle bei Bedarf an); der Kanalindex wird nach Listenposition neu
vergeben.

## Was die Writer bewusst nicht tun

- **Kein OSF4-Output** — OSF5 ist das einzige Schreibformat.
- **Kein OSFZ-Output** — die Bibliothek liest gzip-verpackte Dateien
  transparent, komprimiert aber selbst nicht; Kompression ist ein
  nachgelagerter Schritt.
- **Keine relativen Zeitstempel** — das relative Zeitformat ist OSF4-Lesealtbestand;
  Writer emittieren absolute Zeitstempel.
- **Keine Zeitstempel-Validierung** — Monotonie ist laut Spezifikation nicht
  gefordert und wird nicht erzwungen.
- **Keine Signatur** — die Ed25519-Stufe wird abgelehnt.
- **Keine Thread-Sicherheit** — Zugriffe auf eine Writer-Instanz extern
  serialisieren.

Die Framing- und Chunking-Details, auf denen beide Writer aufsetzen, beschreibt
das Kapitel [Interna](./internals.md); die Gesamtarchitektur und die
Werkzeuge stehen unter [Architektur](./architecture.md), [Werkzeuge](./tools.md)
und [Bauen](./building.md). Einsteigerbeispiele sammelt das
[Kochbuch](./cookbook.md); die verbindliche Formatdefinition liefert das Kapitel
[OSF-Format](../../osf_general.md), der Überblick die
[Java-Implementierung](../java.md).

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
