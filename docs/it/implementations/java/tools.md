---
title: Strumenti — CLI e viewer
description: Gli strumenti Java forniti osf-cli (riga di comando picocli con info/channels/dump/convert) e osf-viewer (plotter multicanale JavaFX con decimazione min/max per pixel)
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
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Strumenti — CLI e viewer

Oltre alla libreria, il reactor Java include due applicazioni
autonome: **`osf-cli`** — una riga di comando scriptabile per
ispezionare, esportare e convertire file OSF — e **`osf-viewer`** — un
viewer JavaFX che traccia molti canali contemporaneamente. Entrambi si
basano esclusivamente sull'API pubblica della libreria (vedere
[Lettura](./reading.md) e [Scrittura](./writing.md)) e ne sono al
tempo stesso esempi completi.

Baseline: **Java 21**, reactor Maven. La build complessiva (`mvn -f
implementations/java/pom.xml package`) genera entrambi gli strumenti in
un unico passaggio; i dettagli sul reactor si trovano in
[Build](./building.md).

## osf-cli

`osf-cli` è un'applicazione [picocli](https://picocli.info/) con
quattro sottocomandi. La classe di ingresso
[`OsfCli`](https://github.com/optimeas/osf/blob/main/implementations/java/osf-cli/src/main/java/com/optimeas/osf/cli/OsfCli.java)
registra `info`, `channels`, `dump` e `convert`; ogni comando dispone
di `-h`/`--help` e `-V`/`--version` (da `mixinStandardHelpOptions`).

### Build come JAR eseguibile

Il modulo `osf-cli` viene riunito tramite il `maven-shade-plugin` in un
**JAR eseguibile autosufficiente** (classe principale
`com.optimeas.osf.cli.OsfCli`, `finalName` `osf-cli`):

```bash
mvn -f implementations/java/pom.xml -pl osf-cli -am package
java -jar implementations/java/osf-cli/target/osf-cli.jar --help
```

Lo shade JAR contiene la libreria OSF e picocli e funziona quindi senza
ulteriore classpath. Per brevità, di seguito `osf` sta per
`java -jar …/osf-cli.jar`.

### `info` — metadati e panoramica dei canali

```bash
osf info messung.osf
```

Carica il file (OSF4, OSF5 o OSFZ in modo trasparente) e visualizza
formato, metadati del file, stato di compressione e una riga per ogni
canale:

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

La riga `format:` è `OSF4` oppure `OSF5`; i metadati provengono
invariati dal metablock (`creator`, `created_utc`, `location`, …); per
gli input compressi compare `compressed: true (gzip)`.

### `channels` — tabella dei canali

```bash
osf channels messung.osf --sort NAME
```

Visualizza una tabella allineata (colonne *index, name, datatype,
mode, samples, unit*). `--sort` accetta `INDEX` (predefinito) oppure
`NAME`:

```text
index   name                                      datatype      mode          samples   unit
------------------------------------------------------------------------------------------
0       temperature                               double        equidistant   10000     °C
2       position                                  gps_location  timestamped   500
1       status                                    string        variable      42
```

### `dump` — dati dei canali in CSV

`dump` scrive i valori dei canali come CSV — per impostazione
predefinita **tutti i canali tracciabili** (numerici + `bool`; i canali
`string`/`binary`/`gps` vengono saltati).

| Opzione | Effetto |
|---|---|
| `--channel <name\|index>` | Seleziona il canale per nome **oppure** per indice intero; ripetibile. Se omessa: tutti i canali tracciabili |
| `--format <csv\|unified-csv>` | `csv` (predefinito): un blocco per canale; `unified-csv`: un'unica tabella larga |
| `--timestamp-format <…>` | `DATETIME` (predefinito), `SECONDS`, `ISO8601`, `NANOSECONDS` |
| `--out <datei>` | Output su file anziché su stdout |

Formati dei timestamp: `DATETIME` = `uuuu-MM-dd HH:mm:ss.SSS` (UTC, ms),
`SECONDS` = secondi decimali con 9 cifre decimali, `ISO8601` =
`uuuu-MM-dd'T'HH:mm:ss'Z'`, `NANOSECONDS` = numero intero grezzo di
nanosecondi. I valori `double` interi vengono resi senza punto decimale
(`1` anziché `1.0`).

**CSV per canale** (predefinito) — ogni blocco con intestazione
`# channel:` e righe `timestamp,value`:

```bash
osf dump messung.osf --channel temperature --timestamp-format SECONDS
```
```text
# channel: temperature
timestamp,value
0.000000000,21.5
0.001000000,21.6
```

**CSV unificato** — un'unica tabella larga con una riga per ogni
timestamp univoco; celle vuote dove un canale non ha alcun campione in
quell'istante:

```bash
osf dump messung.osf --format unified-csv --channel 0 --channel 3 --out werte.csv
```
```text
timestamp,temperature,humidity
1970-01-01 00:00:00.000,21.5,48
1970-01-01 00:00:00.001,21.6,
```

I nomi dei canali contenenti virgola, virgolette o ritorno a capo
vengono racchiusi tra virgolette in conformità al formato CSV.

### `convert` — verso OSF5 (opzionalmente compresso)

`convert` legge un input qualsiasi (OSF4/OSF5, eventualmente compresso)
e lo scrive come **OSF5**:

```bash
osf convert alt-osf4.osf neu.osf                 # OSF4 → OSF5
osf convert messung.osf messung.osfz --compress  # OSF5 → gzip (OSFZ)
```

| Opzione | Effetto |
|---|---|
| `--compress` | Comprime l'output in gzip (genera un file OSFZ) |
| `--writer <BLOCK\|STREAMING>` | Backend del writer; `BLOCK` (predefinito) bufferizza in memoria e scrive in un unico passaggio, `STREAMING` riproduce campione per campione tramite un `FileChannel` |

`STREAMING` non supporta `--compress`; con questa combinazione il
comando ripiega su `BLOCK` con un avviso. In caso di successo
`convert` riporta `wrote <datei> (N channels)`. Il comando è quindi
anche il più semplice convertitore OSF4→OSF5.

Codice sorgente completo:
[`osf-cli/src/main/java/com/optimeas/osf/cli/`](https://github.com/optimeas/osf/tree/main/implementations/java/osf-cli/src/main/java/com/optimeas/osf/cli).

## osf-viewer

`osf-viewer` è un'applicazione **JavaFX** che visualizza
contemporaneamente molti canali di un file OSF. L'idea centrale è una
**decimazione min/max per pixel**: indipendentemente da quanti milioni
di campioni cadano su una colonna dello schermo, nessun valore anomalo
viene mai inghiottito.

### Avvio

Il modo più semplice per eseguire il viewer è il plugin Maven JavaFX
(classe principale `com.optimeas.osf.viewer.ViewerApp`):

```bash
mvn -pl osf-viewer -f implementations/java/pom.xml javafx:run
```

Facoltativamente è possibile aprire un file direttamente all'avvio — il
primo argomento di avvio viene interpretato come percorso:

```bash
mvn -pl osf-viewer -f implementations/java/pom.xml javafx:run \
    -Djavafx.args="messung.osf"
```

Si apre una finestra 1000×700 «OSF Viewer» con barra degli strumenti,
elenco dei canali, area di tracciamento e barra di stato.

### Interfaccia utente

- **Barra degli strumenti** — *Open…* apre una finestra di dialogo dei
  file (filtro `*.osf`, `*.osfz`); *Zoom Reset* riporta l'area visibile
  all'intera estensione temporale di tutti i canali tracciabili.
- **Elenco dei canali** (a sinistra) — una tabella con le colonne
  *Plot, Name, DataType, Mode, Samples, Unit*. La casella di controllo
  Plot inserisce un canale nel disegno; per i canali non tracciabili
  (String, Binary, GPS) è disattivata e riporta il tooltip «not plotted
  in v1».
- **Area di tracciamento** (al centro) — la vera e propria
  visualizzazione delle curve; occupa lo spazio restante e viene
  ridisegnata a ogni modifica delle dimensioni.
- **Barra di stato** (in basso) — stato di caricamento e informazioni di
  lettura del cursore.

Il caricamento avviene sempre **in background** (`Task` JavaFX), in modo
che l'interfaccia resti reattiva durante la lettura di file di grandi
dimensioni.

### Decimazione — min/max per pixel

Per ogni colonna di pixel il
[`Decimator`](https://github.com/optimeas/osf/blob/main/implementations/java/osf-viewer/src/main/java/com/optimeas/osf/viewer/Decimator.java)
determina il **minimo E il massimo** di tutti i campioni che cadono in
quella finestra temporale e disegna un tratto verticale da `minY` a
`maxY`. In questo modo, anche con una compressione estrema, ogni picco
resta visibile — a differenza del semplice scarto dei punti intermedi.
L'assegnazione dei campioni utilizza una ricerca binaria (`lowerBound`)
sui timestamp crescenti ed è quindi veloce anche con milioni di
campioni. Ogni canale selezionato scala autonomamente il proprio asse Y
(autoscale sull'intervallo di valori memorizzato nella cache); i colori
ruotano attraverso una tavolozza di sei tonalità ben distinguibili.

### Interazione

- **Spostamento (pan)** — trascinando in orizzontale con il pulsante del
  mouse premuto si sposta la finestra temporale.
- **Zoom** — rotellina del mouse; scorrendo verso l'alto si ingrandisce
  (fattore 0,8), verso il basso si riduce (fattore 1,25), sempre
  **attorno alla posizione del cursore**, in modo che l'istante sotto il
  cursore resti fermo.
- **Lettura del cursore** — al movimento del mouse la barra di stato
  mostra l'istante del cursore e, per ogni canale selezionato, il valore
  del campione più vicino.

La mappatura delle coordinate (tempo↔X, valore↔Y con asse Y invertito) è
incapsulata in `AxisTransform`, la logica di lettura e di disegno in
`PlotCanvas`; entrambe sono deliberatamente separate dal puro livello
del modello (`ViewerModel`, `Decimator`, `AxisTransform` — senza import
JavaFX) e quindi testabili senza un ambiente JavaFX in esecuzione. I
dettagli su questa suddivisione si trovano in
[Architettura](./architecture.md) e [Interni](./internals.md).

Codice sorgente completo:
[`osf-viewer/src/main/java/com/optimeas/osf/viewer/`](https://github.com/optimeas/osf/tree/main/implementations/java/osf-viewer/src/main/java/com/optimeas/osf/viewer).

## Proseguire

- [Lettura](./reading.md) e [Scrittura](./writing.md) — l'API della
  libreria utilizzata da entrambi gli strumenti.
- [Gestione degli errori](./error-handling.md) — come `OsfException` si
  riflette fino all'output della CLI.
- [Cookbook](./cookbook.md) — ricette brevi e copiabili.
- [Torna alla panoramica Java](../java.md).

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
