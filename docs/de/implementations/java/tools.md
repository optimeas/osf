---
title: Werkzeuge — CLI und Viewer
description: Die mitgelieferten Java-Werkzeuge osf-cli (picocli-Kommandozeile mit info/channels/dump/convert) und osf-viewer (JavaFX-Mehrkanal-Plotter mit Min/Max-pro-Pixel-Dezimierung)
sidebar_position: 5
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - osf-cli
  - osf-viewer
  - picocli
  - JavaFX
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Werkzeuge — CLI und Viewer

Neben der Bibliothek liefert der Java-Reactor zwei eigenständige
Anwendungen mit: **`osf-cli`** — eine skriptbare Kommandozeile zum
Inspizieren, Exportieren und Konvertieren von OSF-Dateien — und
**`osf-viewer`** — einen JavaFX-Betrachter, der viele Kanäle gleichzeitig
plottet. Beide bauen ausschließlich auf der öffentlichen Bibliotheks-API
auf (siehe [Lesen](./reading.md) und [Schreiben](./writing.md)) und sind
zugleich vollständige Beispiele dafür.

Baseline: **Java 21**, Maven-Reactor. Der Gesamtbau (`mvn -f
implementations/java/pom.xml package`) erzeugt beide Werkzeuge in einem
Durchgang; Details zum Reactor stehen unter [Bauen](./building.md).

## osf-cli

`osf-cli` ist eine [picocli](https://picocli.info/)-Anwendung mit vier
Unterbefehlen. Die Einstiegsklasse
[`OsfCli`](https://github.com/optimeas/osf/blob/main/implementations/java/osf-cli/src/main/java/com/optimeas/osf/cli/OsfCli.java)
registriert `info`, `channels`, `dump` und `convert`; jeder Befehl hat
`-h`/`--help` und `-V`/`--version` (aus `mixinStandardHelpOptions`).

### Bau als ausführbares Jar

Das `osf-cli`-Modul wird über das `maven-shade-plugin` zu einem
**selbst­tragenden ausführbaren Jar** gebündelt (Haupt­klasse
`com.optimeas.osf.cli.OsfCli`, `finalName` `osf-cli`):

```bash
mvn -f implementations/java/pom.xml -pl osf-cli -am package
java -jar implementations/java/osf-cli/target/osf-cli.jar --help
```

Das Shade-Jar enthält die OSF-Bibliothek und picocli, läuft also ohne
weiteren Classpath. Der Kürze halber steht im Folgenden `osf` für
`java -jar …/osf-cli.jar`.

### `info` — Metadaten und Kanalübersicht

```bash
osf info messung.osf
```

Lädt die Datei (OSF4, OSF5 oder transparent OSFZ) und gibt Format,
Datei-Metadaten, Kompressionsstatus und je eine Zeile pro Kanal aus:

```text
format: OSF5
creator: optiMEAS
created_utc: 2026-01-15T09:30:00Z
compressed: false
channels: 3
  [0] temperature  type=double  mode=equidistant  samples=10000  unit=°C
  [1] status       type=string  mode=variable     samples=42     unit=
  [2] position     type=gps_location  mode=timestamped  samples=500  unit=
```

Die `format:`-Zeile ist `OSF4` oder `OSF5`; die Metadaten kommen
unverändert aus dem Metablock (`creator`, `created_utc`, `location`, …);
bei komprimierten Eingaben erscheint `compressed: true (gzip)`.

### `channels` — Kanaltabelle

```bash
osf channels messung.osf --sort NAME
```

Gibt eine ausgerichtete Tabelle aus (Spalten *index, name, datatype,
mode, samples, unit*). `--sort` akzeptiert `INDEX` (Standard) oder
`NAME`:

```text
index   name                                      datatype      mode          samples   unit
------------------------------------------------------------------------------------------
0       temperature                               double        equidistant   10000     °C
2       position                                  gps_location  timestamped   500
1       status                                    string        variable      42
```

### `dump` — Kanaldaten nach CSV

`dump` schreibt Kanalwerte als CSV — standardmäßig **alle plottbaren
Kanäle** (numerisch + `bool`; `string`/`binary`/`gps` werden
übersprungen).

| Option | Wirkung |
|---|---|
| `--channel <name\|index>` | Kanal per Name **oder** Ganzzahl-Index wählen; wiederholbar. Ohne Angabe: alle plottbaren Kanäle |
| `--format <csv\|unified-csv>` | `csv` (Standard): pro Kanal ein Block; `unified-csv`: eine breite Tabelle |
| `--timestamp-format <…>` | `DATETIME` (Standard), `SECONDS`, `ISO8601`, `NANOSECONDS` |
| `--out <datei>` | Ausgabe in Datei statt nach stdout |

Zeitstempel-Formate: `DATETIME` = `uuuu-MM-dd HH:mm:ss.SSS` (UTC, ms),
`SECONDS` = Dezimalsekunden mit 9 Nachkommastellen, `ISO8601` =
`uuuu-MM-dd'T'HH:mm:ss'Z'`, `NANOSECONDS` = rohe Nanosekunden-Ganzzahl.
Ganzzahlige `double`-Werte werden ohne Dezimalpunkt gerendert
(`1` statt `1.0`).

**Pro-Kanal-CSV** (Standard) — jeder Block mit `# channel:`-Kopf und
`timestamp,value`-Zeilen:

```bash
osf dump messung.osf --channel temperature --timestamp-format SECONDS
```
```text
# channel: temperature
timestamp,value
0.000000000,21.5
0.001000000,21.6
```

**Vereinheitlichte CSV** — eine breite Tabelle mit einer Zeile je
eindeutigem Zeitstempel; leere Zellen, wo ein Kanal an diesem Zeitpunkt
kein Sample hat:

```bash
osf dump messung.osf --format unified-csv --channel 0 --channel 3 --out werte.csv
```
```text
timestamp,temperature,humidity
1970-01-01 00:00:00.000,21.5,48
1970-01-01 00:00:00.001,21.6,
```

Kanalnamen mit Komma, Anführungszeichen oder Zeilenumbruch werden
CSV-konform in Anführungszeichen gesetzt.

### `convert` — nach OSF5 (optional komprimiert)

`convert` liest eine beliebige Eingabe (OSF4/OSF5, ggf. komprimiert) und
schreibt sie als **OSF5**:

```bash
osf convert alt-osf4.osf neu.osf                 # OSF4 → OSF5
osf convert messung.osf messung.osfz --compress  # OSF5 → gzip (OSFZ)
```

| Option | Wirkung |
|---|---|
| `--compress` | Ausgabe in gzip verpacken (erzeugt eine OSFZ-Datei) |
| `--writer <BLOCK\|STREAMING>` | Writer-Backend; `BLOCK` (Standard) puffert im Speicher und schreibt in einem Durchgang, `STREAMING` spielt Sample für Sample durch einen `FileChannel` |

`STREAMING` unterstützt `--compress` nicht; bei der Kombination fällt der
Befehl mit einem Hinweis auf `BLOCK` zurück. Nach Erfolg meldet
`convert` `wrote <datei> (N channels)`. Damit ist der Befehl zugleich der
einfachste OSF4→OSF5-Konverter.

Vollständiger Quelltext:
[`osf-cli/src/main/java/com/optimeas/osf/cli/`](https://github.com/optimeas/osf/tree/main/implementations/java/osf-cli/src/main/java/com/optimeas/osf/cli).

## osf-viewer

`osf-viewer` ist eine **JavaFX**-Anwendung, die viele Kanäle einer
OSF-Datei gleichzeitig darstellt. Kernidee ist eine **Min/Max-pro-Pixel-
Dezimierung**: egal wie viele Millionen Samples auf eine Bildschirmspalte
fallen, es wird nie ein Ausreißer verschluckt.

### Starten

Der Betrachter läuft am einfachsten über das JavaFX-Maven-Plugin
(Haupt­klasse `com.optimeas.osf.viewer.ViewerApp`):

```bash
mvn -pl osf-viewer -f implementations/java/pom.xml javafx:run
```

Optional lässt sich eine Datei direkt beim Start öffnen — das erste
Startargument wird als Pfad interpretiert:

```bash
mvn -pl osf-viewer -f implementations/java/pom.xml javafx:run \
    -Djavafx.args="messung.osf"
```

Es öffnet sich ein 1000×700-Fenster „OSF Viewer" mit Werkzeugleiste,
Kanalliste, Plotfläche und Statuszeile.

### Bedienoberfläche

- **Werkzeugleiste** — *Open…* öffnet einen Dateidialog (Filter `*.osf`,
  `*.osfz`); *Zoom Reset* setzt den sichtbaren Bereich auf die volle
  Zeitausdehnung aller plottbaren Kanäle zurück.
- **Kanalliste** (links) — eine Tabelle mit den Spalten *Plot, Name,
  DataType, Mode, Samples, Unit*. Die Plot-Checkbox blendet einen Kanal
  in die Zeichnung ein; für nicht plottbare Kanäle (String, Binary, GPS)
  ist sie deaktiviert und trägt den Tooltip „not plotted in v1".
- **Plotfläche** (Mitte) — die eigentliche Kurvendarstellung; sie füllt
  den restlichen Platz und zeichnet bei jeder Größenänderung neu.
- **Statuszeile** (unten) — Ladezustand und Cursor-Ausleseinfo.

Geladen wird stets **im Hintergrund** (JavaFX-`Task`), damit die
Oberfläche während des Einlesens großer Dateien reagierbar bleibt.

### Dezimierung — Min/Max pro Pixel

Für jede Pixelspalte bestimmt der
[`Decimator`](https://github.com/optimeas/osf/blob/main/implementations/java/osf-viewer/src/main/java/com/optimeas/osf/viewer/Decimator.java)
das **Minimum UND Maximum** aller in dieses Zeitfenster fallenden Samples
und zeichnet einen senkrechten Strich von `minY` bis `maxY`. So bleibt
selbst bei extremer Verdichtung jede Spitze sichtbar — anders als beim
einfachen Wegwerfen von Zwischenpunkten. Die Sample-Zuordnung nutzt eine
Binärsuche (`lowerBound`) auf den aufsteigenden Zeitstempeln, ist also
auch bei Millionen Samples schnell. Jeder ausgewählte Kanal skaliert
seine Y-Achse eigenständig (Autoscale über den zwischengespeicherten
Wertebereich); die Farben rotieren durch eine Palette von sechs gut
unterscheidbaren Tönen.

### Interaktion

- **Verschieben (Pan)** — mit gedrückter Maustaste horizontal ziehen
  verschiebt das Zeitfenster.
- **Zoomen** — Mausrad; hoch scrollen zoomt hinein (Faktor 0,8), herunter
  heraus (Faktor 1,25), jeweils **um die Cursor-Position** herum, sodass
  der Zeitpunkt unter dem Cursor stehen bleibt.
- **Cursor-Ausleseinfo** — bei Mausbewegung zeigt die Statuszeile den
  Cursor-Zeitpunkt und für jeden ausgewählten Kanal den nächst­gelegenen
  Sample-Wert.

Die Koordinatenabbildung (Zeit↔X, Wert↔Y mit invertierter Y-Achse) liegt
gekapselt in `AxisTransform`, die Auslese- und Zeichenlogik in
`PlotCanvas`; beide sind bewusst von der reinen Modell­schicht
(`ViewerModel`, `Decimator`, `AxisTransform` — ohne JavaFX-Importe)
getrennt und dadurch ohne laufende JavaFX-Umgebung testbar. Details zu
diesem Zuschnitt stehen unter [Architektur](./architecture.md) und
[Interna](./internals.md).

Vollständiger Quelltext:
[`osf-viewer/src/main/java/com/optimeas/osf/viewer/`](https://github.com/optimeas/osf/tree/main/implementations/java/osf-viewer/src/main/java/com/optimeas/osf/viewer).

## Weiter

- [Lesen](./reading.md) und [Schreiben](./writing.md) — die von beiden
  Werkzeugen genutzte Bibliotheks-API.
- [Fehlerbehandlung](./error-handling.md) — wie `OsfException` bis in die
  CLI-Ausgabe durchschlägt.
- [Kochbuch](./cookbook.md) — kurze, kopierbare Rezepte.
- [Zurück zur Java-Übersicht](../java.md).

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
