---
title: OSF4 – Spezifische Dokumentation
description: Details und Spezifikationen für das Open Streaming Format Version 4 (OSF4)
#sidebar_label: OSF4
image: "/img/om_social_card.png"
keywords:
  - OSF4
  - Fileformat
  - XML
last_update:
  date: 2026-05-04
  author: Optimeas GmbH
---

🇬🇧 [English version](../en/osf4.md)

# OSF4 – Spezifische Dokumentation

Dieses Dokument beschreibt alle Aspekte des **Open Streaming Formats Version 4 (OSF4)**, die über die allgemeine OSF-Beschreibung hinausgehen.  
Es ergänzt die Datei [`osf_general.md`](osf_general.md), in der alle für OSF4 und OSF5 gemeinsamen Strukturen erklärt sind.

OSF4 ist die klassische Version des Formats. Sie nutzt ausschließlich **XML** für den Metablock und bildet die Basis für die Abwärtskompatibilität in OSF5.



## Magic Header in OSF4

- **Erlaubte Kennungen:**  
  - `OSF4`  
  - `OCEAN_STREAM_FORMAT4` (historische Kennung)

- **Format:**  
  ```
  OSF4 173762\n
  ```

- **Eigenschaften:**  
  - Immer **XML-Metablock** direkt nach dem Header.  
  - Keine JSON-Unterstützung.  
  - OSF5-Parser können OSF4-Dateien vollständig lesen.

---

## Metablock in OSF4 (XML)

OSF4 verwendet für den Metablock ausschließlich **XML**.  
Er enthält alle Datei- und Kanalinformationen und beginnt mit einem Standard-Prolog:

```xml
<?xml version="1.0" encoding="UTF-8"?>
```

### Wurzelelement `<osf>`

Beispiel:

```xml
<osf version="4"
     created_utc="2019-08-12T12:23:01+02:00"
     creator="smartdevice:14001000078"
     created_at_longitude="50.2"
     created_at_latitude="8.65"
     created_at_altitude="193"
     reason="BOOT"
     total_seq_no="0"
     triggered_seq_no="0"
     namespacesep="."
     tag="preview"
     comment="">
```

#### Attribute:

- **version:** Version des OSF4-Formats (Default: `"1"`).  
- **created_utc:** Zeitpunkt der Dateierstellung im ISO-8601-Format (UTC).  
- **creator:** Identifikation des erzeugenden Geräts oder Programms.  
- **created_at_longitude / latitude / altitude:** Optionale geografische Position.  
- **reason:** Grund für die Dateierstellung (`BOOT`, `SEQUENCE`, `TRIGGERED`).  
- **total_seq_no:** Absolute Sequenznummer seit Systemstart.  
- **triggered_seq_no:** Relative Sequenznummer seit letztem Trigger.  
- **namespacesep:** Separator für hierarchische Kanalnamen (Default: `"."`).  
- **tag:** Freies Tag zur Klassifizierung der Datei (Default: `"preview"`).  
- **comment:** Optionaler Kommentar.  

---

## Kanalliste `<channels>`

Enthält alle Kanäle der Datei.

```xml
<channels count="8">
    <channel index="0"
             name="Sensor/Temperature"
             channeltype="scalar"
             datatype="double"
             timeincrement="1000000"
             sizeoflengthvalue="2"
             physicalunit="°C"
             reference="uuid"
             physicaldimension="temperature"/>
</channels>
```

- **count:** Anzahl der Kanäle in der Datei.

---

## Kanalbeschreibung `<channel>`

Alle Parameter wie in der allgemeinen OSF-Doku beschrieben, für OSF4 gilt:

- **Metablock:** Immer XML  
- **Unterstützte `channeltype`:** `scalar`, `vector`, `matrix`, `binary`  
- **Vektor- und Matrix-Parameter:** In der Kanaldefinition über `rows`, `columns` und zugehörige Attribute  
- **`sizeoflengthvalue`:** Pflichtfeld (2 oder 4 Byte)  

---

## Unterstützte Datentypen in OSF4

| Datentyp    | Größe    | Beschreibung |
|-------------|----------|--------------|
| `bool`      | 1 Byte   | Wahr/Falsch |
| `int8`      | 1 Byte   | Ganzzahl mit Vorzeichen |
| `int16`     | 2 Byte   | Ganzzahl mit Vorzeichen |
| `int32`     | 4 Byte   | Ganzzahl mit Vorzeichen |
| `int64`     | 8 Byte   | Ganzzahl mit Vorzeichen |
| `uint8`     | 1 Byte   | Ganzzahl ohne Vorzeichen, Wertebereich 0 … 255 |
| `uint16`    | 2 Byte   | Ganzzahl ohne Vorzeichen, Wertebereich 0 … 65 535 |
| `uint32`    | 4 Byte   | Ganzzahl ohne Vorzeichen, Wertebereich 0 … 4 294 967 295 |
| `uint64`    | 8 Byte   | Ganzzahl ohne Vorzeichen, Wertebereich 0 … 18 446 744 073 709 551 615 |
| `float`     | 4 Byte   | IEEE 754 Single Precision |
| `double`    | 8 Byte   | IEEE 754 Double Precision |
| `string`    | variabel | UTF-8 kodiert, Länge durch Blockgröße definiert. Endet mit abschließender Nullbyte (`0x00`) – siehe [`osf_general.md`](osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary). |
| `gpslocation` | 24 Byte | Struktur für GPS-Positionen |

## Stringterminierung

Für `bcAbsTimeStampData` mit `datatype=string` oder `datatype=binary` gilt die in [`osf_general.md`](osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary) beschriebene Regel: abschließende Nullbyte (`0x00`) am Ende des Datenfelds. Dies ist Bestandsverhalten in OSF4 und bleibt unverändert.

---

## Datenblöcke in OSF4

OSF4 nutzt die in der allgemeinen Spezifikation beschriebenen Datenblockstrukturen.  
Spezifisch für OSF4:

- **Steuerbyte:** Alle ursprünglichen Blocktypen `0–8` werden unterstützt.  
- **`bcContinuedRelStampData`:** Wird in OSF4 noch genutzt (ab OSF5 entfernt).  
- **`bcStatusEvent` und `bcMessageEvent`:** Vorhanden, werden aber in neuen Implementierungen nicht mehr empfohlen.  
- **Metadaten:** Immer im XML-Format.

### `bcStartData` mit Abtastrate

Ab dieser Spec-Revision trägt `bcStartData` in OSF4 — wie in OSF5 — direkt nach dem `int64`-Startzeitstempel ein `double`-Feld mit der **Abtastrate (Hz)**. Die Rate gilt für alle nachfolgenden `bcContinuedData`-Blöcke desselben Kanals, bis ein neuer `bcStartData`-Block geschrieben wird.

**Beispiel `bcStartData` in OSF4:**
`[int64 ZeitStart] [double SampleRate] [uint32 N] [double Wert1] [double Wert2] ... [double WertN]`

Neue OSF4-Schreiber **müssen** dieses Feld schreiben. Reader, die einer OSF4-Datei ohne dieses Feld begegnen (alte Bestandsdateien), schlagen mit einem Format-Fehler fehl — es gibt keinen impliziten Fallback aus `timeincrement`.

### Einschränkungen:

- **Äquidistante Kanäle (`bcStartData`, `bcContinuedData`):** Nur numerische Typen (`int*`, `float`, `double`).  
- **Zeitgestempelte Kanäle (`bcAbsTimeStampData`, `bcContinuedRelStampData`):** Alle Typen erlaubt.

---

## XML-Trailer und Magic Trailer

OSF4 unterstützt optional einen XML-Info-Block mit Kanalstatistiken und einen Magic Trailer.

### XML-Trailer-Beispiel:

```xml
<trailer finalized_utc="2019-08-12T12:23:01+02:00" reason="fileStartGrid_min">
    <channels count="8">
        <channel index="0" samples="29452" last_ns="1384899599997800000"/>
        <channel index="1" samples="29452" last_ns="1384899599997800000"/>
    </channels>
</trailer>
```

### Magic Trailer:

```
OSF_STREAM_END 321316454==============
```

- 40 Byte lang, Zahl = Position des `0xFFFF`-Blocks.

---

## Besonderheiten und Limitierungen von OSF4

- Nur XML-Metablock, keine JSON-Unterstützung.  
- Steuerbyte komplexer, mehr Blocktypen als OSF5.  
- Vektor- und Matrixkanäle über XML-Parameter (`rows`, `columns`).  
- `bcContinuedRelStampData` wird unterstützt, ist aber in OSF5 nicht mehr enthalten.  

---

## Beispiel einer OSF4-Datei

```text
OSF4 30269
<?xml version="1.0" encoding="UTF-8"?>
<osf version="4" created_utc="2019-08-12T12:23:01+02:00" creator="smartdevice:14001000021">
  <channels count="2">
    <channel index="0" name="Sensor/Temperature" channeltype="scalar" datatype="double" timeincrement="1000000"/>
    <channel index="1" name="Sensor/Pressure" channeltype="scalar" datatype="double" timeincrement="1000000"/>
  </channels>
</osf>
[BEGIN OF BINARY DATA]
```
