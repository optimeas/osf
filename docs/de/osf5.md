---
title: OSF5 – Spezifische Dokumentation
description: Details und Spezifikationen für das Open Streaming Format Version 5 (OSF5)
#sidebar_label: OSF5
image: "/img/om_social_card.png"
keywords:
  - OSF5
  - Fileformat
  - JSON
last_update:
  date: 2026-05-04
  author: Optimeas GmbH
---

🇬🇧 [English version](../en/osf5.md)

# OSF5 – Spezifische Dokumentation

Dieses Dokument beschreibt alle Aspekte des **Open Streaming Formats Version 5 (OSF5)**, die über die allgemeine OSF-Beschreibung hinausgehen.  
Es ergänzt die Datei [`osf_general.md`](osf_general.md), in der alle für OSF4 und OSF5 gemeinsamen Strukturen erklärt sind.

OSF5 ist die Weiterentwicklung von OSF4. Es nutzt **JSON** als Standardformat für den Metablock und vereinfacht das Steuerbyte.  
Gleichzeitig bleibt OSF5 vollständig **abwärtskompatibel** zu OSF4.



## Magic Header in OSF5

- **Erlaubte Kennungen:**  
  - `OSF5`  
  - `OSF4` (für Abwärtskompatibilität)  
  - `OCEAN_STREAM_FORMAT4` (Legacy-Kennung, weiterhin von ausgelieferten Geräten geschrieben)  
  - `OCEAN_STREAMING_FORMAT4` (ältere historische Schreibweise)

- **Format:**  
  ```
  OSF5 84512\n
  ```

- **Erkennung des Metablocks:**  
  - Erstes Zeichen `<` → XML (OSF4-Metablock)  
  - Erstes Zeichen `{` → JSON (OSF5-Metablock)  

- **Eigenschaften:**  
  - Standardmäßig **JSON-Metablock**.  
  - OSF5-Parser können OSF4-XML lesen.



## Metablock in OSF5 (JSON)

OSF5 verwendet für den Metablock standardmäßig **JSON**.  
Die Struktur entspricht funktional der XML-Variante aus OSF4, ist jedoch leichter zu parsen und für Embedded-Systeme optimiert.

### Beispiel:

```json
{
  "osf": {
    "version": 5,
    "created_utc": "2025-07-27T12:00:00Z",
    "creator": "smartdevice:15002000001",
    "channels": [
      {
        "index": 0,
        "name": "Sensor/Temperature",
        "channeltype": "scalar",
        "datatype": "double",
        "timeincrement": 1000000,
        "sizeoflengthvalue": 2,
        "physicalunit": "°C"
      }
    ]
  }
}
```

- **Unterschied zu OSF4:** JSON statt XML, aber gleiche logische Inhalte.  
- **Kompatibilität:** OSF5 kann weiterhin OSF4-XML interpretieren.


## Vereinfachungen im Steuerbyte (Blocktypen)

OSF5 übernimmt die Blockstruktur aus OSF4, reduziert aber die Anzahl der genutzten Blocktypen und vereinfacht deren Interpretation.

### Änderungen gegenüber OSF4:

- `bcContinuedRelStampData` wird nicht mehr verwendet.  
- `bcStatusEvent` und `bcMessageEvent` entfallen.  
- Bit 7 für Einzel-/Mehrwert bleibt unverändert.  

### Unterstützte Blocktypen in OSF5:

| Wert | Enum               | Beschreibung |
|------|--------------------|--------------|
| 0    | `bcReserved`       | Reserviert für interne Zwecke. |
| 2    | `bcTimebaseRealign`| Anpassung der Zeitachse (selten genutzt). |
| 5    | `bcContinuedData`  | Daten mit fester Abtastrate fortsetzen. |
| 6    | `bcStartData`      | Startblock mit fester Abtastrate. |
| 8    | `bcAbsTimeStampData` | Daten mit absolutem Zeitstempel. |


## Unterstützte Datentypen in OSF5

OSF5 unterstützt dieselben Datentypen wie OSF4.

| Datentyp   | Größe    | Beschreibung |
|------------|----------|--------------|
| `bool`     | 1 Byte   | Wahr/Falsch |
| `int8`     | 1 Byte   | Ganzzahl mit Vorzeichen |
| `int16`    | 2 Byte   | Ganzzahl mit Vorzeichen |
| `int32`    | 4 Byte   | Ganzzahl mit Vorzeichen |
| `int64`    | 8 Byte   | Ganzzahl mit Vorzeichen |
| `uint8`    | 1 Byte   | Ganzzahl ohne Vorzeichen, Wertebereich 0 … 255 |
| `uint16`   | 2 Byte   | Ganzzahl ohne Vorzeichen, Wertebereich 0 … 65 535 |
| `uint32`   | 4 Byte   | Ganzzahl ohne Vorzeichen, Wertebereich 0 … 4 294 967 295 |
| `uint64`   | 8 Byte   | Ganzzahl ohne Vorzeichen, Wertebereich 0 … 18 446 744 073 709 551 615 |
| `float`    | 4 Byte   | IEEE 754 Single Precision |
| `double`   | 8 Byte   | IEEE 754 Double Precision |
| `string`   | variabel | UTF-8 kodiert, Länge durch Blockgröße definiert. Endet mit abschließender Nullbyte (`0x00`) – siehe [`osf_general.md`](osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary). |
| `binary` *(Alias: `bytearray`)* | variabel | Beliebige Bytefolgen für Bild-, Audio- oder andere Binärdaten mit MIME-Type. Maximale Länge wird durch `sizeoflengthvalue` bestimmt. Endet mit abschließender Nullbyte (`0x00`) – siehe [`osf_general.md`](osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary). |
| `gpslocation` | 24 Byte | Struktur für GPS-Positionen |

## Stringterminierung

Für `bcAbsTimeStampData` mit `datatype=string` oder `datatype=binary` gilt die in [`osf_general.md`](osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary) beschriebene Regel: abschließende Nullbyte (`0x00`) am Ende des Datenfelds. Diese Regel ist mit OSF4 identisch.

### `bcStartData` mit Abtastrate

Wie in der allgemeinen Spezifikation und in OSF4 trägt `bcStartData` in OSF5 direkt nach dem `int64`-Startzeitstempel ein `double`-Feld mit der **Abtastrate (Hz)**. Sie gilt für alle nachfolgenden `bcContinuedData`-Blöcke desselben Kanals, bis ein neuer `bcStartData`-Block geschrieben wird. Mehrere `bcStartData`-Blöcke pro Kanal mit Zeitlücken dazwischen sind explizit zulässig.

**Beispiel `bcStartData` in OSF5:**
`[int64 ZeitStart] [double SampleRate] [uint32 N] [double Wert1] [double Wert2] ... [double WertN]`

## Trailer und Info-Block

OSF5 **unterstützt keinen Info-Datenblock (0xFFFF) und keinen Magic Trailer mehr.**  
Das Dateiende wird ausschließlich durch das Erreichen des letzten vollständig geschriebenen Datenblocks definiert.

- **Folge:**  
  - Parser lesen die Datei bis zum letzten konsistenten Block.  
  - Es gibt keine zusätzliche Statistik- oder Metainformation am Dateiende.  
  - Für Zeitintervall-Informationen muss die Datei komplett oder teilweise analysiert werden.


### Warum OSF5 keinen Trailer und keinen Info-Block mehr verwendet

In OSF4 gab es am Dateiende optional einen **Info-Datenblock (Index 0xFFFF)** und einen **Magic Trailer**, um Metainformationen und das Zeitintervall der Datei schnell verfügbar zu machen.  
In OSF5 haben wir beide Elemente bewusst entfernt.

### Hauptgrund: Reduzierter Implementierungsaufwand

- Das Schreiben des Info-Blocks erfordert eine abschließende Aufbereitung aller Kanäle und Zeitinformationen.  
- Für die Leseseite bedeutet es zusätzlichen Code, der neben dem regulären Datenstrom gepflegt und getestet werden muss.  
- Da OSF ohnehin darauf ausgelegt ist, Dateien bis zum letzten vollständigen Block zu lesen, liefern Trailer und Info-Block keinen technischen Mehrwert, der den Aufwand rechtfertigt.

### Ergänzende Vorteile

- **Einfachere Spezifikation:** Weniger Sonderfälle, schlankere Implementierung auf Embedded- und PC-Seite.  
- **Keine doppelte Informationshaltung:** Alle relevanten Metadaten sind im Metablock und in den Datenblöcken selbst vorhanden.  
- **Analyse-Tools können dasselbe leisten:** Zeitintervalle und Statistiken lassen sich beim Einlesen aus den regulären Blöcken ableiten.

> **Fazit:**  
> Der Verzicht auf Trailer und Info-Block in OSF5 ist vor allem eine Entscheidung zugunsten einer einfacheren, leichter wartbaren Implementierung.  
> Die Robustheit des Formats bleibt vollständig erhalten – sie ergibt sich aus der Blockstruktur und nicht aus zusätzlichen Elementen am Dateiende.


## Besonderheiten und Limitierungen von OSF5

- **JSON** als Hauptformat, XML nur für Abwärtskompatibilität.  
- **Vereinfachtes Steuerbyte** mit weniger Blocktypen.  
- Keine Trailer oder Info-Datenblöcke am Dateiende.  
- `bcContinuedRelStampData`, `bcStatusEvent`, `bcMessageEvent` werden nicht mehr erzeugt.



## Beispiel einer OSF5-Datei

```text
OSF5 2048
{
  "osf": {
    "version": 5,
    "created_utc": "2025-07-27T12:00:00Z",
    "creator": "smartdevice:15002000001",
    "channels": [
      {
        "index": 0,
        "name": "Sensor/Temperature",
        "channeltype": "scalar",
        "datatype": "double",
        "timeincrement": 1000000
      },
      {
        "index": 1,
        "name": "Sensor/Force",
        "channeltype": "scalar",
        "datatype": "double",
        "physicalunit": "N"
      },
      {
        "index": 2,
        "name": "Sensor/Path",
        "channeltype": "scalar",
        "datatype": "double",
        "physicalunit": "mm"
      }
    ]
  }
}
[BEGIN OF BINARY DATA]
```
