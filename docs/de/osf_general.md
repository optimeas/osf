---
title: OSF Format Beschreibung
description: Description for common properties of OSF 4 and 5
#hide_title: true/false
#hide_table_of_contents: true/false
#sidebar_label: Label shown in the sidebar
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - Fileformat
  - OSF4
  - OSF5
  - OSF
last_update:
  date: 2025-07-26
  author: Optimeas GmbH
---
## Allgemeine Beschreibung des OSF Formates
- Gültig für Formatversion 4 und 5 -

Das **Open Streaming Format (OSF)** ist ein binäres, blockorientiertes Datenformat zur kontinuierlichen Aufzeichnung von zeitbezogenen Mess- und Prozessdaten. Es ist so konzipiert, dass es nicht nur für das **Streaming auf Embedded-Systemen mit begrenzten Ressourcen** optimal geeignet ist, sondern ebenso für die **blockweise, effiziente Verarbeitung großer Datenmengen** auf leistungsfähigen Analyseplattformen – egal ob auf Servern, PCs oder im Postprocessing direkt auf Embedded-Geräten.


### Grundprinzipien

* **Zeit als zentrale Achse:** Alle Daten werden mit eindeutigen Zeitinformationen gespeichert – äquidistant mit festem Zeitraster oder individuell mit Zeitstempeln.
* **Streamingfähig:** Daten können während der laufenden Messung kontinuierlich in die Datei geschrieben werden, ohne vorherige Kenntnis der Gesamtmenge.
* **Robustheit:** Selbst bei Stromausfall oder unerwartetem Abschalten bleiben alle bis zum letzten geschriebenen Block gespeicherten Daten lesbar.
* **Offene Struktur:** Kombination aus einem klaren Metablock (XML bei OSF4, JSON bei OSF5) und einem einfachen, binären Datenstrom.
* **Blockweise Speicherung:** Statt einzelne Werte sequentiell zu schreiben, werden Datenblöcke genutzt. Das reduziert Schreibvorgänge und ermöglicht schnelles Laden großer Datenmengen.
* **Flexibilität:** Unterstützung von verschiedenen Datentypen – von einfachen Skalaren über Vektoren und Matrizen bis zu Binärdaten wie Bildern oder Audiodateien.
* **Metadaten:** Jeder Kanal kann mit Namen, physikalischen Einheiten, Dimensionen und optionalen Zusatzinformationen beschrieben werden.

### Grundaufbau einer OSF-Datei

Unabhängig von Version 4 oder Version 5 folgt jede OSF-Datei demselben Grundschema:

![OSF Dateistruktur.](media/dfa207f25f79c84ec79e2e2f6c2c42cc.jpg)


1. **Magic Header**

   * Kennung des Formats (OSF4, OSF5, OCEAN\_STREAMING\_FORMAT4)
   * Angabe der Länge des folgenden Metablocks

2. **Metablock (XML oder JSON)**

   * Enthält Informationen über Kanäle, Datentypen, physikalische Einheiten und Kontextdaten
   * Definiert die Struktur der nachfolgenden Datenblöcke

3. **Binäre Datenblöcke**

   * Enthalten die eigentlichen Messwerte im Streaming-Format
   * Unterstützen äquidistante und zeitgestempelte Daten
   * Können Einzelwerte, Vektoren, Matrizen oder Binärdaten enthalten

4. **Optionaler Abschlussblock**

   * Bei OSF4 optionaler XML-Trailer mit Statistik und Kanalübersicht
   * Bei OSF5 entfällt dieser Trailer standardmäßig

### Datenorganisation

* **Kanäle:** Jeder Datenstrom wird als Kanal beschrieben, der Name, Datentyp, physikalische Einheit und optional weitere Attribute enthält.
* **Zeitbasis:** Alle Zeitangaben werden in Nanosekunden seit Epoch gespeichert und ermöglichen hochpräzise Synchronisation.
* **Blockheader:** Jeder Datenblock beginnt mit einem Kanalindex und einer Längenangabe, sodass auch bei unbekannten Kanälen oder Abbrüchen der Stream interpretierbar bleibt.
* **Steuerbyte:** Definiert den Typ und die Struktur des folgenden Datenblocks (z. B. Start, Fortsetzung, Zeitstempeltyp). In OSF5 wird die Nutzung dieses Bytes vereinfacht, bleibt aber funktional kompatibel.



## Magic Header

Jede OSF-Datei beginnt mit dem sogenannten **Magic Header**.
Er dient zwei Zwecken:

1. Eindeutige Identifikation als OSF-Datei.
2. Angabe der Länge des folgenden Metablocks, damit dieser direkt gelesen und geparst werden kann.

### Aufbau

Der Magic Header ist eine ASCII-Zeile, abgeschlossen mit Linefeed (`\n`).

**Beispiel OSF4:**

```
OSF4 173762\n
```

* **OSF4** ist die Kennung des Formats.
* **173762** gibt die Länge des Metablocks in Bytes an.

**Beispiel OSF5:**

```
OSF5 84512\n
```

* **OSF5** kennzeichnet die neue Version.
* **84512** Die Zahl gibt die Länge des Metablocks an, der bei OSF5 standardmäßig JSON ist.

### Unterstützte Kennungen

Aus Gründen der Kompatibilität erkennen OSF-Implementierungen mehrere Header:

* **OSF4** – klassische OSF4-Datei
* **OCEAN\_STREAM\_FORMAT4** – historische Kennung für OSF4-Dateien
* **OSF5** – OSF5-Datei

### Erkennung des Metablock-Formats

Ob der folgende Metablock **XML** oder **JSON** ist, wird anhand des ersten Zeichens nach dem Header bestimmt:

* `<` → XML (OSF4-Format)
* `{` → JSON (OSF5-Format)
* Anderes Zeichen → Fehler

### Vorteile

* **Schneller Start:** Leser können den Metablock sofort extrahieren und dem richtigen Parser übergeben.
* **Streamingfähig:** Keine Kenntnis der Gesamtdateigröße nötig.
* **Abwärtskompatibel:** OSF5 verarbeitet OSF4-Dateien (inkl. OCEAN\_STREAM\_FORMAT4).
* **Einfache Implementierung:** Eine Zeile reicht, um Version und Parser zu bestimmen.


Hier ist der allgemeine Text zum **Metablock** für OSF4 und OSF5, ohne Spezifika zu XML/JSON und ohne Vektor-/Matrix-Parameter:


## Metablock – Kanäle und Metadaten

Direkt nach dem Magic Header folgt in jeder OSF-Datei der **Metablock**.
Er enthält alle Informationen, die nötig sind, um die nachfolgenden Datenblöcke korrekt zu interpretieren. Dazu gehören:

* **Datei-Parameter:** Kontextinformationen zur Datei und ihrer Entstehung.
* **Kanaldefinitionen:** Beschreiben jeden Datenstrom mit Namen, Datentyp und physikalischen Eigenschaften.
* **Metadaten:** Zusätzliche Informationen, die nicht direkt an einen Kanal gebunden sind (z. B. Systemstatus, Kommentare, Kalibrierdaten).

Der Metablock bildet das „Inhaltsverzeichnis“ der Datei und ist so gestaltet, dass er **eindeutig, maschinenlesbar und leicht zu erweitern** ist.


### Datei-Parameter (im Metablock)

* **created\_utc** – Zeitpunkt der Dateierstellung in UTC im ISO 8601 Format
* **creator** – `optional` Identifikation des Erzeugers (z. B. Geräteseriennummer, Programmname, UUID)
* **created\_at\_longitude** / **created\_at\_latitude** / **created\_at\_altitude** – `optional` geografische Position der Dateierstellung
* **reason** – `optional` Grund für die Dateierstellung (z. B. `BOOT`, `SEQUENCE`, `TRIGGERED`)
* **total\_seq\_no** – `veraltet` Absolute Sequenznummer seit Systemstart (beginnend bei 0)
* **triggered\_seq\_no** – `veraltet` Relative Sequenznummer seit dem letzten Triggerereignis (beginnend bei 0)
* **namespacesep** – `optional` Separator für hierarchische Kanalnamen (Standard `"."`)
* **tag** – `optional` Freies Tag zur Klassifizierung der Datei (z. B. `preview`)
* **comment** – `optional` Optionaler Kommentartext


### Kanaldefinitionen (channel)

Jeder Kanal beschreibt einen Datenstrom innerhalb der Datei.
Die Parameter werden nachfolgend beschrieben


#### **Identifikation und Organisation**

* **index**
  Eindeutiger Kanalindex innerhalb der Datei (beginnend bei 0).
* **name**
  Kanalname, optional mit hierarchischem Pfad (z. B. `Motor/Temperatur`).
* **reference**
  Optionale eindeutige Referenz oder UUID zur Identifikation des Datenursprungs.


#### **Zeitbasis**

* **timeincrement**
  Festes Zeitinkrement in Nanosekunden für äquidistante Kanäle.
  *Wert = 0 oder nicht gesetzt → Kanal verwendet individuelle Zeitstempel.*

  **Hinweis:** Das `timeincrement` im Metablock ist ein **optionaler Hinweis**. Bei hochaufgelösten oder triggerbasierten Aufzeichnungen ist die exakte Abtastrate beim Erstellen des Headers oft noch nicht bekannt. Die **tatsächlich gültige Abtastrate wird in jedem `bcStartData`-Block als `double` mitgeliefert** und gilt ab diesem Zeitpunkt für alle nachfolgenden `bcContinuedData`-Blöcke desselben Kanals, bis ein neuer `bcStartData`-Block geschrieben wird. Dies gilt sowohl für **OSF4** als auch für **OSF5**.


#### **Datentypen und Struktur**

* **datatype**
  Datentyp der gespeicherten Werte (z. B. `bool`, `int32`, `double`, `string`, `candata`, `gpsdata`).
  → *Eine vollständige Beschreibung aller Datentypen und ihrer Kodierung befindet sich im Kapitel [Datentypen](#datentypen).*

* **channeltype**
  Struktureller Typ des Kanals:

  * `scalar` – Einzelwerte über der Zeit
  * `binary` – Beliebige Binärblöcke (z. B. Bilder)
    *(Vektor und Matrix werden in einem separaten Dokument beschrieben)*
    → *Eine detaillierte Erklärung der Kanaltypen befindet sich im Kapitel [Kanaltypen](#kanaltypen).*

* **sizeoflengthvalue**
  Größe der Längenangabe für jeden Datenblock:

  * `2` → 2 Byte (uint16, max. Blockgröße \~64 kB)
  * `4` → 4 Byte (uint32, max. Blockgröße \~4 GB)
    Wird verwendet, um die Blockgröße zu bestimmen und Datenblöcke im Stream korrekt zu lesen.
    *→ Eine ausführliche Erklärung findet sich im Kapitel [sizeoflengthvalue](#sizeoflengthvalue).*

* **mimetype**
  Optional, MIME-Type für binäre Kanäle (z. B. `image/jpeg`, `audio/wav`).

* **spectrumtype**
  Optional, Typ der Spektraldaten:

  * `amplitude` (Standard)
  * `realImag`
  * `ampPhaseRad`
  * `ampPhaseDeg`


#### **Physikalische Eigenschaften**

* **physicalunit**
  Optional, physikalische Einheit (SI-konform, z. B. `V`, `°C`).
* **physicaldimension**
  Optional, Beschreibung der physikalischen Dimension (`temperature`, `pressure`, …).


#### **Darstellung und Zusatzinfos**

* **displayname**
  Optionaler Anzeigename für Visualisierung oder GUI.
* **comment**
  Optionaler Kommentar zum Kanal.


### Metadaten (info)

Metadaten ergänzen die Datei mit zusätzlichen Informationen, die nicht an einen Kanal gebunden sind.
Typische Parameter:

* **name** – Name der Information
* **value** – Wert (als String oder typisiert)
* **datatype** – Typ des Wertes (`string`, `int32`, `float`, `bytearray` etc.)
* **physicalunit** – Optional, physikalische Einheit des Wertes

Metadaten sind frei definierbar und eignen sich für:

* System- oder Gerätedaten
* Kommentare und Statusmeldungen
* Kalibrierwerte
* Benutzerdefinierte Zusatzinformationen

> **Hinweis:** `bytearray` ist ein Alias für `binary`. Beide Bezeichnungen sind in OSF4 und OSF5 gültig und werden von Lesern identisch interpretiert. Schreiber **sollen** beim Schreiben einheitlich `binary` verwenden; `bytearray` bleibt zur Abwärtskompatibilität lesbar.


### Vorteile der Struktur

* **Klare Trennung von Daten und Beschreibung:** Der Metablock definiert, wie Daten interpretiert werden, ohne selbst Messwerte zu enthalten.
* **Selbstbeschreibend:** Dateien können ohne externe Definitionen gelesen und interpretiert werden.
* **Erweiterbar:** Neue Kanäle, Datentypen oder Metainformationen können hinzugefügt werden, ohne das Grundformat zu ändern.
* **Robust:** Durch feste Indizes und Längenangaben bleibt die Datei interpretierbar, selbst wenn nicht alle Kanäle bekannt sind.



## Kernparameter der Kanalbeschreibung

Die folgenden Parameter definieren die grundlegende Struktur eines Kanals in OSF und bestimmen, wie Daten im Streaming-Format gespeichert und interpretiert werden. Sie sind für alle Kanäle relevant und bilden das Fundament der Kanalbeschreibung.

<a name="datentypen"></a>
### Datentypen (datatype) 

Der Parameter `datatype` legt das Datenformat der Werte eines Kanals fest. Jeder Wert wird in einem genau definierten binären Format gespeichert.

#### Unterstützte Datentypen und Kodierung:

| Datentyp  | Größe (Bytes) | Beschreibung                                                                                                                                         |
| --------- | ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `bool`    | 1             | Wahr/Falsch (0 = false, 1 = true)                                                                                                                    |
| `int8`    | 1             | Ganzzahl mit Vorzeichen                                                                                                                              |
| `int16`   | 2             | Ganzzahl mit Vorzeichen                                                                                                                              |
| `int32`   | 4             | Ganzzahl mit Vorzeichen                                                                                                                              |
| `int64`   | 8             | Ganzzahl mit Vorzeichen                                                                                                                              |
| `float`   | 4             | IEEE 754 Single Precision                                                                                                                            |
| `double`  | 8             | IEEE 754 Double Precision                                                                                                                            |
| `string`  | variabel      | UTF-8 kodiert, Länge durch Blockgröße definiert. Endet mit abschließender Nullbyte (`0x00`) – siehe Hinweisblock unten. |
| `binary` *(Alias: `bytearray`)* | variabel | Beliebige Bytefolgen für Bild-, Audio- oder andere Binärdaten mit MIME-Type. Die maximale Länge des Blocks wird durch das `sizeoflengthvalue`-Feld des Kanals bestimmt. Endet mit abschließender Nullbyte (`0x00`) – siehe Hinweisblock unten. |
| `candata` | 16            | Struktur für CAN-Frames (siehe unten)                                                                                                                |
| `gpsdata` | 24            | Struktur für GPS-Positionen (siehe unten)                                                                                                            |

> **Hinweis zu Integer-Typen:** Integer-Werte (`int8`, `int16`, `int32`, `int64`) werden in OSF-Dateien typischerweise für **Zustände, Statusinformationen oder Zählerwerte** verwendet, nicht als skalierte Rohwerte einer physikalischen Größe. Aus diesem Grund kennt OSF bewusst **keine** `scale`/`offset`-Parameter zur Umrechnung in physikalische Werte – physikalische Größen werden direkt als `float` oder `double` gespeichert.

<a name="hinweis-zur-nullterminierung-von-string-und-binary"></a>
> **Hinweis zur Nullterminierung von `string` und `binary`:**
> Datenwerte vom Typ `string` oder `binary` enthalten **immer** eine abschließende Nullbyte (`0x00`) als letztes Byte des Datenblocks. Dies gilt für **OSF4 und OSF5** gleichermaßen.
>
> - **Schreiber** müssen beim Erzeugen eines Blocks ein zusätzliches `0x00` an die Nutzdaten anhängen. Die im Block angegebene Länge schließt dieses Byte ein.
> - **Leser** müssen das letzte Byte des Datenfelds als Nullterminator interpretieren und **explizit entfernen**, bevor die Nutzdaten weiterverarbeitet werden. Geschieht dies nicht, erscheint das `0x00` als zusätzliches Zeichen am Ende eines Strings oder als zusätzliches Byte am Ende eines Binär-Payloads (z. B. einer JPEG-Datei mit angehängtem Nullbyte → ungültige Datei).
> - Die effektive Nutzlänge ist daher: **Blocklänge des Datenfelds − 1 Byte**.

#### Struktur `candata`

```c
struct can_frame {
    uint32 can_id;   // 32-Bit CAN-ID + Flags
    uint8  can_dlc;  // Länge der Payload (0..8 Byte)
    uint8  data[8];  // CAN-Nutzdaten
} __attribute__((aligned(8)));
```

#### Struktur `gpsdata`

```c
struct gps_location {
    double longitude;  // Längengrad
    double latitude;   // Breitengrad
    double altitude;   // Höhe
};
```

> **Hinweis:** Für Kanäle mit `datatype="binary"` wird empfohlen, den MIME-Type (`mimetype`) im Kanal zu definieren (z. B. `image/jpeg`, `audio/wav`), um die Daten eindeutig interpretieren zu können. Die maximale Blockgröße wird durch den Parameter [`sizeoflengthvalue`](#sizeoflengthvalue) des Kanals bestimmt.


<a name="kanaltypen"></a>
### Kanaltypen (channeltype)

Der Parameter `channeltype` definiert die **logische Organisation der Werte** eines Kanals. Er legt fest, wie viele Werte pro Datenblock gespeichert werden und welche Struktur diese Werte haben.

OSF kennt drei grundlegende Kanaltypen:


#### scalar

* **Beschreibung:**
  Ein Kanal mit genau einem Wert pro Zeitpunkt.
  Typische Anwendung: kontinuierliche physikalische Größen (Temperatur, Spannung, Druck) oder digitale Signale (z. B. Türstatus).

* **Eigenschaften:**

  * Jeder Datenblock enthält eine oder mehrere Abtastungen mit einem einzelnen Wert.
  * Unterstützt äquidistante Abtastung über `timeincrement` oder individuelle Zeitstempel pro Wert.
  * Einfachster und am häufigsten genutzter Kanaltyp.

* **XML-Beispiel (OSF4):**

  ```xml
  <channel 
      index="0"
      name="Sensor/Temperature"
      channeltype="scalar"
      datatype="double"
      physicalunit="°C"/>
  ```

* **JSON-Beispiel (OSF5):**

  ```json
  {
    "index": 0,
    "name": "Sensor/Temperature",
    "channeltype": "scalar",
    "datatype": "double",
    "physicalunit": "°C"
  }
  ```



#### vector

* **Beschreibung:**
  Ein Kanal, bei dem jeder Datenblock eine **Folge von mehreren Werten** enthält, die logisch zusammengehören.
  Typische Anwendung: Frequenzspektren (FFT), Zeitserien-Segmente, Mehrkanalaufzeichnungen in einem Block.

* **Eigenschaften:**

  * Vektorlänge kann pro Block variieren.
  * Spart Overhead bei hoher Abtastrate, da mehrere Werte in einem Block geschrieben werden.
  * Kann sowohl mit Zeitstempeln pro Block als auch mit festem Zeitinkrement arbeiten.
  * Benötigt zusätzliche Parameter für Achseninformationen (separates Dokument).

* **XML-Beispiel (OSF4):**

  ```xml
  <channel 
      index="2"
      name="FFT/Magnitude"
      channeltype="vector"
      datatype="float"
      physicalunit="dB"/>
  ```

* **JSON-Beispiel (OSF5):**

  ```json
  {
    "index": 2,
    "name": "FFT/Magnitude",
    "channeltype": "vector",
    "datatype": "float",
    "physicalunit": "dB"
  }
  ```



#### matrix

* **Beschreibung:**
  Ein Kanal, der pro Zeitstempel eine **zweidimensionale Datenstruktur** speichert.
  Typische Anwendung: Rainflow-Klassierungen, Heatmaps, 2D-Sensorarrays, Bilddaten.

* **Eigenschaften:**

  * Matrixgröße kann pro Block variieren.
  * Ermöglicht komplexe Datenstrukturen in einer einheitlichen Zeitbasis.
  * Benötigt zusätzliche Parameter für Zeilen- und Spaltenbeschreibung (separates Dokument).

* **XML-Beispiel (OSF4):**

  ```xml
  <channel 
      index="5"
      name="Rainflow/Matrix"
      channeltype="matrix"
      datatype="int32"
      physicalunit="counts"/>
  ```

* **JSON-Beispiel (OSF5):**

  ```json
  {
    "index": 5,
    "name": "Rainflow/Matrix",
    "channeltype": "matrix",
    "datatype": "int32",
    "physicalunit": "counts"
  }
  ```



#### Hinweise zu Vector und Matrix

* **Zusätzliche Parameter:** Beide Typen benötigen Metainformationen zu Dimensionen, Achsen, physikalischen Einheiten und ggf. Labels. Diese werden in eigenen Dokumenten detailliert beschrieben.
* **Effizienz:** Vektor- und Matrixkanäle reduzieren Schreiboperationen und eignen sich besonders für Daten mit hoher Abtastrate oder komplexer Struktur.
* **Flexibilität:** Größe und Struktur der Blöcke können variieren, was eine Anpassung an unterschiedliche Messszenarien erlaubt.
* **Synchronisation:** Sie teilen sich dieselbe Zeitbasis wie Scalar-Kanäle, sodass verschiedene Datentypen in einer Datei exakt synchronisiert abgelegt werden können.

---

#### Zusammenfassung

* **`scalar`** – Einfacher Kanaltyp, ein Wert pro Zeitpunkt. Ideal für kontinuierliche Messgrößen.
* **`vector`** – Mehrere Werte in einem Block, optimiert für Frequenzspektren und hochfrequente Daten.
* **`matrix`** – Mehrdimensionale Blöcke, geeignet für Klassierungen, Bild- und Arraydaten.

> **Hinweis:** Durch die Kombination dieser Kanaltypen deckt OSF sowohl einfache Signale als auch komplexe Datensätze ab und bleibt gleichzeitig leicht implementierbar.


<a name="sizeoflengthvalue"></a>
### Blockgrößenfeld (sizeoflengthvalue)

Der Parameter `sizeoflengthvalue` definiert, wie groß das Längenfeld ist, das jedem Datenblock eines Kanals vorangestellt wird. Er bestimmt also, wie viele Bytes zur Angabe der Blockgröße verwendet werden und damit, wie groß ein einzelner Datenblock maximal sein kann.

#### Zweck

OSF ist ein Streaming-Format. Jeder Datenblock kann unterschiedlich groß sein und enthält eine variable Anzahl an Messwerten. Um diese Blöcke korrekt lesen zu können, muss ihre Länge bekannt sein. Das Feld `sizeoflengthvalue` gibt an, ob für die Längenangabe **2 Byte** oder **4 Byte** verwendet werden.

#### Werte

* **2** – Längenfeld ist 2 Byte (uint16).

  * Maximaler Wert: 65.535 Bytes pro Datenblock.
  * Standardwert für typische Messkanäle mit moderaten Blockgrößen.
  * Geringerer Speicherverbrauch und Overhead.
* **4** – Längenfeld ist 4 Byte (uint32).

  * Maximaler Wert: \~4 GB pro Datenblock.
  * Für Kanäle mit sehr großen Datenpaketen geeignet, z. B. Bild-, Audio- oder Binärdaten.

#### Standardwert

Falls nicht explizit angegeben, wird `sizeoflengthvalue="2"` verwendet.

#### Auswirkungen

* **Speicherbedarf:** 2 Byte sparen Platz bei kleinen Blöcken, 4 Byte ermöglichen große Daten.
* **Lesbarkeit:** Der Leser muss vor dem Interpretieren jedes Blocks die Längenangabe einlesen und die nächsten `N` Bytes als Block behandeln.
* **Fehlerresistenz:** Selbst bei unterbrochenem Schreiben kann der Leser Blöcke korrekt überspringen und den nächsten validen Block finden.

#### Empfehlungen

* Für kontinuierliche Signale und Kanäle mit skalaren Werten → `2` verwenden.
* Für Binärkanäle mit Bildern, Audio oder großen Datenpaketen → `4` wählen.
* Einheitliche Wahl pro Kanal; kann pro Kanal in der Kanaldefinition gesetzt werden.


## Datenblöcke

**Datenblöcke** bilden das Herzstück des OSF-Formats. Sie enthalten die eigentlichen Messwerte und sind so strukturiert, dass sie sowohl im kontinuierlichen Streaming auf Embedded-Systemen als auch bei der blockweisen Verarbeitung auf Servern und PCs effizient geschrieben und gelesen werden können. Jeder Datenblock ist in sich abgeschlossen und bleibt auch bei einem Abbruch der Aufzeichnung interpretierbar.

### Einführung

In OSF werden alle Messwerte in **Datenblöcken** gespeichert. Jeder Block ist eine abgeschlossene Einheit, die einen oder mehrere Werte eines Kanals enthält und mit Zeitinformationen verknüpft ist.  
Das Blockkonzept ermöglicht zwei zentrale Eigenschaften des Formats:

- **Kontinuierliches Streaming**: Werte können während der Aufzeichnung fortlaufend geschrieben werden, ohne die gesamte Dateistruktur zu kennen.  
- **Effiziente Verarbeitung**: Durch blockweise Speicherung lassen sich große Datenmengen auf Servern, PCs oder im Postprocessing schnell laden und verarbeiten.

Jeder Datenblock ist so gestaltet, dass er auch bei einem plötzlichen Abbruch der Messung (z. B. Stromausfall) bis zur letzten vollständig geschriebenen Einheit lesbar bleibt.  
Die Struktur der Datenblöcke ist für OSF4 und OSF5 identisch und bildet die Grundlage für eine robuste, verlustfreie Aufzeichnung zeitbezogener Messdaten.


### Allgemeiner Aufbau eines Datenblocks

Ein Datenblock in OSF besteht aus einer festen Kopfstruktur, gefolgt von optionalen Metadaten und den eigentlichen Messwerten.  
Der Aufbau ist so gestaltet, dass jeder Block unabhängig interpretiert werden kann und auch bei Streaming oder Dateiabbruch gültig bleibt.

**Grundstruktur:**

1. **Kanalindex (`uint16`)**  
   - Identifiziert, zu welchem Kanal die Daten gehören.  
   - Entspricht dem `index`-Attribut im Metablock.  

2. **Längenfeld (`uint16` oder `uint32`)**  
   - Größe des nachfolgenden Datenbereichs in Bytes.  
   - Die Länge des Feldes wird durch den Kanalparameter `sizeoflengthvalue` definiert.  
   - Ermöglicht es, Blöcke zu überspringen oder bei Fehlern korrekt zur nächsten Einheit zu springen.  

3. **Steuerbyte (`uint8`)**  
   - Definiert den Typ des Datenblocks und enthält Informationen über die Struktur der folgenden Daten.  
   - Das höchstwertige Bit (Bit 7) zeigt an, ob der Block **einen einzelnen Wert** (0) oder **mehrere Werte/Wertepaare** (1) enthält.  
   - Eine vollständige Übersicht der Steuerbyte-Werte befindet sich im Abschnitt *Das Steuerbyte*.  

4. **Datenbereich**  
   - Die eigentlichen Messwerte oder Datenblöcke.  
   - Format und Größe richten sich nach dem Kanaltyp (typischerweise scalar) und dem Datentyp.
<br/>

### Das Steuerbyte

Jeder Datenblock in OSF enthält ein **Steuerbyte (`blockContent`)**, das den Typ des Blocks und die Struktur der enthaltenen Daten bestimmt.  
Zusätzlich enthält das höchstwertige Bit (Bit 7) die Information, ob der Block **nur einen einzelnen Wert** oder **mehrere Werte bzw. Wertepaaren** enthält:

- **Bit 7 = 0** → Ein einzelner Wert oder Wertepaar im Block.  
- **Bit 7 = 1** → Mehrere Werte oder Wertepaare im Block (N > 1).

Das Steuerbyte wird als 8-Bit-Wert interpretiert. Die unteren 7 Bits definieren den **Blocktyp**, das oberste Bit die Anzahl der Werte.

<br/>

#### Übersicht der Blocktypen

| Wert (0–6) | Enum                | Bedeutung                                                                                  | Datenblock-Inhalt |
|------------|--------------------|-------------------------------------------------------------------------------------------|-------------------|
| **0**      | `bcReserved`       | Reserviert für zukünftige Nutzung. Ursprünglich *bcMetaData*, bisher nicht genutzt.        | Variabel, interne Sonderfunktionen |
| **1**      | `bcTrustedTimestamp` | `Entfällt` Ursprünglich für konstante Werte mit „gültig bis“-Zeitstempel gedacht. Empfehlung: Stützstellen durch die Anwendung setzen. | `int64`: Absoluter Zeitstempel (ns since Epoch) |
| **2**      | `bcTimebaseRealign` | `Entfällt`Anpassung der Zeitachse. Kann bei Bedarf durch Schreiben eines neuen Blocks mit absolutem Startzeitpunkt ersetzt werden. | `int64`: Absoluter Zeitstempel<br/>`int64`: Zeitverschiebung (ns) |
| **3**      | `bcStatusEvent`    | `Entfällt` Diente zur Mitführung von Statusinformationen pro Kanal. Wird nicht mehr genutzt. | `int64`: Absoluter Zeitstempel<br/>`uint32`: Status-Wort |
| **4**      | `bcMessageEvent`   | `Entfällt` Kann vollständig durch `bcAbsTimeStampData` mit `datatype=string` ersetzt werden. | `int64`: Absoluter Zeitstempel<br/>`string`: Text ohne 0-Terminierung |
| **5**      | `bcContinuedData`  | Daten mit fester Abtastrate fortsetzen. Bei gesetztem Bit 7 mehrere Werte im Block.        | `[uint32 N]`: Anzahl der Samples (nur wenn Bit 7 gesetzt)<br/>`N` × Datenwerte |
| **6**      | `bcStartData`      | Erster Datenblock mit fester Abtastrate; trägt zusätzlich die ab diesem Block gültige Abtastrate (z. B. bei Trigger). Enthält immer einen absoluten Startzeitstempel. | `int64`: Absoluter Zeitstempel<br/>`double`: Abtastrate (Hz)<br/>`[uint32 N]`: Anzahl der Samples (nur wenn Bit 7 gesetzt)<br/>`N` × Datenwerte |
| **7**      | `bcContinuedRelStampData` | `Entfällt`, `In OSF5 beim Lesen unterstützt` Ursprünglich zur Einsparung von 4 Byte pro Sample mit relativen Zeitstempeln. | `[uint32 N]`: Anzahl der Samples (nur wenn Bit 7 gesetzt)<br/>`N` × (`uint32` Relativzeit + Datenwert) |
| **8**      | `bcAbsTimeStampData` | Datenblöcke mit absolutem Zeitstempel pro Wert. Unterstützt nun auch Strings und Binärdaten in Verbindung mit `datatype` und `mimetype`. | `[uint32 N]`: Anzahl der Samples (nur wenn Bit 7 gesetzt)<br/>`N` × (`int64` Absolutzeit + Datenwert) |


Einschränkung der Blockarten im Hinblick auf die Kanalinformation:

| ENUM-Typ      | Äquidistante Daten | Zeitgestempelte Daten |
| ------------- |-------------| -----|
| bcStartData            | erlaubt | nicht erlaubt |
| bcContinuedData        | erlaubt | nicht erlaubt |
| bcContinuedRelStampData | nicht erlaubt | erlaubt|
| bcAbsTimeStampData | nicht erlaubt | erlaubt|

<br/>
### Datenstruktur je Steuer-Typ

Die Struktur der Nutzdaten in einem Block hängt direkt vom Steuer-Typ ab.  
Die folgenden Abschnitte beschreiben, wie Werte für verschiedene Datentypen gespeichert sind und welche Einschränkungen gelten.

#### bcStartData (äquidistante Daten, Startblock)

- **Einsatz:**  
  - Beginn einer Datenserie mit fester Abtastrate.  
  - Enthält immer den absoluten Startzeitpunkt der Serie.  
  - Nur für numerische `datatype` (`int*`, `float`, `double`) erlaubt.  

- **Blockaufbau:**  
  1. `int64` – Absoluter Startzeitstempel (ns since Epoch).  
  2. `double` – Abtastrate in Hz (gültig ab diesem Block, bis zum nächsten `bcStartData`).  
  3. **[uint32 N]** – Anzahl der Samples (nur wenn Bit 7 gesetzt, sonst 1).  
  4. **N × Datenwerte** – Rohdaten entsprechend `datatype`.  

- **Beispiel `datatype=double`:** [int64 ZeitStart] [double SampleRate] [uint32 N] [double Wert1] [double Wert2] ... [double WertN]


- **Hinweise:**
  - `bcStartData` darf **mehrfach pro Datei und Kanal** auftreten. Er wird geschrieben:
    - zu Beginn einer äquidistanten Aufzeichnung,
    - bei jedem Trigger oder Ereignis, das eine neue Datensequenz auslöst,
    - bei einer notwendigen Korrektur der Zeitspur (Driftausgleich).
  - **Konsequenz für Leser:** Daten eines äquidistanten Kanals entstehen **block- bzw. ereignisweise**. Zwischen aufeinanderfolgenden Sequenzen desselben Kanals können **Zeitlücken** liegen. Leser müssen die effektive Abtastrate aus dem aktuell gültigen `bcStartData`-Block übernehmen und dürfen **nicht** annehmen, dass `timeincrement` aus dem Metablock immer korrekt ist.

#### bcContinuedData (äquidistante Daten, Fortsetzung)

- **Einsatz:**  
  - Setzt eine mit `bcStartData` begonnene Serie ohne neuen Zeitstempel fort.  
  - Der erste Wert schließt direkt an den letzten Wert des vorherigen Blocks an.  
  - Nur für numerische `datatype` (`int*`, `float`, `double`) erlaubt.  

- **Blockaufbau:**  
    1. **[uint32 N]** – Anzahl der Samples (nur wenn Bit 7 gesetzt, sonst 1).  
    2. **N × Datenwerte** – Rohdaten entsprechend `datatype`.  

- **Beispiel `datatype=int16`:**[uint32 N] [int16 Wert1] [int16 Wert2] ... [int16 WertN]

- **Hinweis:** Die Zeit pro Sample in einem `bcContinuedData`-Block ergibt sich aus `1 / SampleRate` des zuletzt gelesenen `bcStartData`-Blocks desselben Kanals.



#### bcAbsTimeStampData (zeitgestempelte Daten)

- **Einsatz:**  
  - Für Kanäle mit individuellen Zeitstempeln pro Wert.  
  - Unterstützt alle `datatype`, inkl. `string` und `binary`.

- **Blockaufbau:**  
  1. **[uint32 N]** – Anzahl der Samples (nur wenn Bit 7 gesetzt, sonst 1).  
  2. **N × (int64 Zeit + Datenwert)** – Absolutzeitstempel + Wert.  

- **Beispiel `datatype=int16`:**[uint32 N] [int64 Zeit1] [int16 Wert1] [int64 Zeit2] [int16 Wert2] ...
- **Beispiel `datatype=double`:**[uint32 N] [int64 Zeit1] [double Wert1] [int64 Zeit2] [double Wert2] ...
- **Beispiel `datatype=string`:**
  - Strings werden **mit abschließender Nullbyte (`0x00`)** gespeichert. Die effektive Stringlänge ergibt sich aus der Nutzlänge des Datenfelds abzüglich des Nullbytes.
  - `Anzahl der Samples` bestimmt die Länge. Bit 7 muss gesetzt sein.
  - [uint32 N] [int64 Zeit] [UTF-8 Bytes des Strings] [0x00]

- **Beispiel `datatype=binary`:**
  - Binärdaten werden als Rohbytes mit **abschließender Nullbyte (`0x00`)** geschrieben. Der `mimetype` im Kanal definiert die Interpretation. Die effektive Payload-Länge ist die Nutzlänge des Datenfelds abzüglich des Nullbytes.
  - `Anzahl der Samples` (N) bestimmt die Länge. Bit 7 muss gesetzt sein.
  - [uint32 N] [int64 Zeit] [Byte1] [Byte2] ... [Byte M] [0x00]


- **Hinweis:** Bei mehreren Samples pro Block (`N>1`) müssen Strings oder Binärdaten in gleich langen Segmenten vorliegen oder als einzelne Blöcke geschrieben werden.



#### bcContinuedRelStampData (zeitgestempelt, relativ)

- **Einsatz:**  
  - Für Kanäle mit individuellen Zeitstempeln und relativen Abständen.  
  - Wird ab **OSF5 nicht mehr verwendet**, bleibt für OSF4-Leser erhalten.

- **Blockaufbau:**  
  1. **[uint32 N]** – Anzahl der Samples (nur wenn Bit 7 gesetzt, sonst 1).  
  2. **N × (uint32 Δt + Datenwert)** – Relativer Zeitabstand in ns + Wert.  

- **Beispiel `datatype=int16`:**[uint32 N] [uint32 Δt1] [int16 Wert1] [uint32 Δt2] [int16 Wert2] ...
- Weitere Beispiele unter bcAbsTimeStampData


- **Hinweis:**  
- Ursprünglich entwickelt, um 4 Byte pro Sample zu sparen.  
- In OSF5 entfällt dieser Typ zugunsten der einfacheren Implementierung.



#### Einschränkungen

- **Äquidistante Kanäle (bcStartData, bcContinuedData):**
  - Nur direkte numerische Datentypen (`int*`, `float`, `double`).  
  - Keine Strings, keine Binärdaten, keine komplexen Strukturen.  

- **Zeitgestempelte Kanäle (bcAbsTimeStampData, bcContinuedRelStampData):**
  - Unterstützen alle Datentypen.
  - Strings und Binärdaten werden mit abschließender Nullbyte (`0x00`) gespeichert. Die Nutzlänge ergibt sich aus der Blocklänge minus dem Nullbyte.

#### Wichtige Punkte

- **Kompatibilität:**  
  - OSF5 kann alle OSF4-Blocktypen lesen.  
  - Ab OSF5 werden `bcContinuedRelStampData`, `bcStatusEvent` und `bcMessageEvent` nicht mehr erzeugt.  
  - `bcTrustedTimestamp` wird ignoriert, und ist als *deprecated* gekennzeichnet.

- **Implementierung:**  
  - Leser müssen Bit 7 immer prüfen, um Einzel- vs. Mehrwertblöcke korrekt zu interpretieren.  
  - Nicht erkannte Blocktypen können anhand der Längenangabe übersprungen werden.

- **Strings und Binärdaten:**
  - Für `bcAbsTimeStampData` mit `datatype=string` oder `datatype=binary` wird **immer** eine abschließende Nullbyte (`0x00`) geschrieben. Dies gilt für OSF4 und OSF5. Leser müssen das Nullbyte vor der Weiterverarbeitung entfernen.
  - Die Blocklänge ergibt sich aus `sizeoflengthvalue`.
  - Binärdaten verwenden `datatype=binary` plus `mimetype`.
<br/>



## Dateiabschluss und Magic Trailer

Am Ende einer OSF-Datei kann optional ein **Info-Datenblock** mit dem speziellen Kanalindex `0xFFFF` geschrieben werden.  
Dieser Block liefert Metainformationen zum abgeschlossenen Datenstrom und markiert den regulären Abschluss der Datei.  

### Info-Datenblock (Kanalindex 0xFFFF)

- **Zweck:**  
  Liefert einen schnellen Überblick über das Zeitintervall und die Segmentierung der Datei, ohne die gesamten Datenblöcke lesen zu müssen.  
  Nützlich für Analyse- und Indexierungswerkzeuge.

- **Aufbau:**  
    1. `uint16` – Kanalindex (`0xFFFF`)  
    2. `uint32` – Länge des folgenden Optionsblocks  
    3. `uint8` – Steuerbyte (immer `bcReserved` / 0)  
    4. `string` – UTF-8-kodierter Info-Block (Format abhängig von OSF-Version)

#### Beispiel (OSF4, XML):

```xml
<trailer finalized_utc="2019-08-12T12:23:01+02:00" reason="fileStartGrid_min">
    <channels count="8">
        <channel index="0" samples="29452" last_ns="1384899599997800000" last_utc="2019-11-19T23:19:59"/>
        <channel index="1" samples="29452" last_ns="1384899599997800000" last_utc="2019-11-19T23:19:59"/>
        <channel index="2" samples="29452" last_ns="1384899599997800000" last_utc="2019-11-19T23:19:59"/>
        <channel index="3" samples="29452" last_ns="1384899599997800000" last_utc="2019-11-19T23:19:59"/>
    </channels>
</trailer>
````

#### Beispiel (OSF5, JSON):

```json
{
  "trailer": {
    "finalized_utc": "2019-08-12T12:23:01+02:00",
    "reason": "fileStartGrid_min",
    "channels": [
      {
        "index": 0,
        "samples": 29452,
        "last_ns": 1384899599997800000,
        "last_utc": "2019-11-19T23:19:59"
      },
      {
        "index": 1,
        "samples": 29452,
        "last_ns": 1384899599997800000,
        "last_utc": "2019-11-19T23:19:59"
      }
    ]
  }
}
```

* **Anmerkung:**

  * OSF4 verwendet standardmäßig XML für den Info-Block.
  * OSF5 verwendet JSON, kann aber aus Kompatibilitätsgründen auch XML lesen aber nicht schreiben.

<br/>

### Magic Trailer

Optional kann nach dem Info-Datenblock ein **Magic Trailer** folgen.
Dieser dient als feste Markierung für den Dateischluss und zeigt an, wo der `0xFFFF`-Block beginnt.

* **Format:**

```
OSF_STREAM_END 321316454==============
```

* Die Zahl gibt die Position in der Datei an, an der der `0xFFFF`-Block beginnt.
* Das Trailer-Tag wird auf **40 Byte** aufgefüllt, wobei nach der Zahl `=`-Zeichen folgen, bis die Länge erreicht ist.

#### Zweck des Magic Trailers

* Erlaubt es, den Info-Datenblock am Dateiende ohne Durchsuchen der gesamten Datei zu finden.
* Erleichtert Random-Access-Implementierungen und Indexierung großer Dateien.
* Bietet eine klare Markierung für den regulären Abschluss einer OSF-Datei.

<br/>


### Optionalität und Implementierungsaufwand

* **Schreiben:**

  * Der Info-Block und der Magic Trailer sind nicht zwingend erforderlich.
  * Wenn sie geschrieben werden, ermöglichen sie eine schnelle Indexierung und die Bestimmung des Zeitintervalls der Datei.
  * Bei Embedded-Systemen mit engen Ressourcen können sie weggelassen werden.

* **Lesen:**

  * Parser dürfen nicht vom Vorhandensein des Blocks ausgehen.
  * Dateien ohne Trailer werden bis zum letzten vollständig lesbaren Block interpretiert.
  * Bei hartem Abbruch kann der letzte Block kürzer sein als seine Längenangabe – in diesem Fall muss der Leser am Dateiende abbrechen.

<br/>


**Vorteile**

* Schnelle Ermittlung des Zeitintervalls und von Statistiken ohne komplettes Einlesen der Datei.
* Nützlich für lange Messungen und automatisierte Analyse.
* Ermöglicht Random Access für Analyse-Tools.

**Nachteile**

* Erhöhter Implementierungsaufwand für Schreiben und Lesen.
* Bei Dateiabbruch kann der Trailer fehlen oder unvollständig sein.
* Für einfaches Streaming ist er nicht zwingend notwendig.

<br/>

## Nächste Schritte

Das bisherige Kapitel beschreibt den allgemeinen Aufbau des **Open Streaming Formats (OSF)** und alle Komponenten, die für **OSF4 und OSF5 gleichermaßen** gelten.  
Für eine vollständige Implementierung oder tiefere Integration bieten sich folgende weiterführende Themen an:

- **Spezifika von OSF4 und OSF5:**  
  - Details zu den jeweiligen Header-Formaten (XML vs. JSON)  
  - Unterschiede beim Steuerbyte und im Trailer  
  - Abwärtskompatibilität und Implementierungshinweise
  - Weiter mit [OSF4](osf4.md) oder [OSF5](osf5.md)

- **Vectoren und Matrizen:**  
  - Erweiterte Kanaltypen für mehrdimensionale Daten  
  - Zusätzliche Parameter für Achsen, Dimensionen und physikalische Einheiten  
  - Beispiele für FFTs, Klassierungen und Bilddaten  

- **Beispiele:**  
  - Vollständige OSF-Dateien (OSF4/XML und OSF5/JSON) mit Header, Metablock und Datenblöcken  
  - Hex-Dumps und kommentierte Strukturen  

- **Zugang zu Quellcode und Open Source:**  
  - Referenzimplementierungen für OSF4 und OSF5  
  - Parser- und Writer-Bibliotheken für verschiedene Plattformen  
  - Beispielcode für Embedded-Systeme und PC-Analyse


