---
title: osftool — Strumento da riga di comando
description: Lo strumento da riga di comando osftool per unire, esportare, ispezionare e convertire file OSF
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - osftool
  - CLI
  - Riga di comando
  - merge
  - export
last_update:
  date: 2026-05-22
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# osftool — Strumento da riga di comando

**osftool** è uno strumento da riga di comando basato su verbi per lavorare con file nell'Open Streaming Format. Legge e scrive OSF4 e OSF5, elabora in modo trasparente i file compressi OSFZ e riunisce in un unico eseguibile le attività quotidiane relative ai dati OSF: unire file su un intervallo di tempo, esportare canali in CSV o HDF5, ispezionare i metadati, calcolare statistiche, convertire tra versioni del formato e verificare l'integrità dei file.

osftool si basa sulla libreria OSF per Delphi (`implementations/delphi/src/`). Il target di build primario è Windows a 64 bit; il progetto contiene inoltre configurazioni di build per macOS (Intel e Apple Silicon) e Linux a 64 bit. La versione attuale è la **1.1.0**.

## A cosa serve osftool

osftool copre le attività ricorrenti di un flusso di lavoro basato su OSF da uno script o da un terminale — senza IDE e senza programmazione:

- **Unire i dati di campo.** Molti file OSF/OSFZ distribuiti in un albero di directory vengono letti e uniti in un unico file per un intervallo di tempo e una selezione di canali scelti.
- **Portare i dati nei programmi di analisi.** I canali vengono esportati in CSV — come un blocco XY per canale oppure come asse temporale comune con una colonna per canale — oppure, nella build per Windows, in un file HDF5.
- **Ispezionare rapidamente i file.** Metadati, intervallo di tempo, elenchi dei canali e statistiche per canale sono disponibili senza aprire il file in un'applicazione.
- **Migrare tra versioni del formato.** I file OSF4 vengono riscritti come OSF5 e viceversa.
- **Verificare l'integrità.** I file vengono percorsi blocco per blocco e verificati per la consistenza strutturale.
- **Automazione.** Ogni comando dispone di una modalità `--json` leggibile da macchina e di un insieme uniforme di codici di uscita, in modo che osftool si integri in modo pulito in script e pipeline CI.

## Installazione

### Installer per Windows

Il modo più rapido per ottenere osftool su Windows è l'installer precompilato:

**[Scaricare osftool per Windows (64 bit)](https://www.optimeas.com/hubfs/Software%20Download/osftool-setup-latest.exe)**

L'installer configura `OsfTool.exe` insieme all'ambiente di runtime HDF5, aggiunge la directory del programma al `PATH` e consente di scegliere tra un'installazione per tutti gli utenti e una solo per l'utente corrente. Al termine `osftool` è disponibile in qualsiasi terminale.

### Compilazione dal codice sorgente

osftool fa parte del repository OSF e viene compilato con Embarcadero Delphi 12 (RAD Studio 23) o versioni successive.

```
cd implementations/delphi/tools/osftool
dcc64 -B -Q OsfTool.dpr
```

Questo genera `OsfTool.exe` per Windows a 64 bit. Il file di progetto `OsfTool.dproj` contiene inoltre le configurazioni di build `OSX64`, `OSXARM64` e `Linux64` per l'utilizzo dall'IDE di RAD Studio; il codice sorgente è privo di conflitti di configurazione della compilazione condizionale per questi target.

### Aggiungere osftool al PATH

Per poter richiamare `osftool` da qualsiasi directory, se ne aggiunge il percorso al percorso di ricerca:

```
osftool config install-path
```

Su Windows questo comando inserisce la directory dell'eseguibile nel `PATH` specifico dell'utente (`HKCU\Environment`) — non sono necessari diritti di amministratore. Su macOS e Linux il comando stampa lo snippet di shell da aggiungere a `~/.zshrc` o `~/.bashrc`. La modifica si annulla con `osftool config uninstall-path`.

## Utilizzo generale

```
osftool <befehl> [optionen] [argumente]
osftool <befehl> --help
```

`osftool` senza argomenti o `osftool --help` stampa la guida globale con l'elenco dei comandi. Se `--help` viene aggiunto a un comando, questo stampa la propria guida dettagliata senza eseguire alcuna azione. `osftool --version` (oppure `-V`) stampa versione e timestamp di build; con `--short` solo il numero di versione.

### Comandi

| Comando    | Scopo |
|------------|-------|
| `merge`    | Unire file OSF di una directory su un intervallo di tempo |
| `export`   | Esportare canali OSF in CSV o in altri formati |
| `info`     | Visualizzare metadati e intervallo di tempo di un file |
| `channels` | Elencare tutti i canali di un file |
| `stat`     | Calcolare statistiche (minimo, massimo, media, …) per canale |
| `cache`    | Gestire i file di cache sidecar `.json` |
| `config`   | Visualizzare e modificare le impostazioni predefinite |
| `convert`  | Convertire tra OSF4 e OSF5 |
| `verify`   | Verificare l'integrità del file e la consistenza dei blocchi |

### Opzioni globali

Queste opzioni sono comprese da ogni comando:

| Opzione     | Effetto |
|-------------|---------|
| `--json`    | Stampa su stdout un risultato JSON leggibile da macchina anziché testo leggibile da persone |
| `--quiet`   | Sopprime le uscite informative; avvisi ed errori continuano a essere visualizzati |
| `--verbose` | Stampa su stderr messaggi di debug |

I risultati vanno su **stdout**, i messaggi di log e gli errori su **stderr**, in modo che possano essere reindirizzati separatamente. In modalità `--json` stdout contiene esclusivamente il documento JSON.

### Codici di uscita

| Codice | Significato |
|------|-----------|
| `0`  | Successo |
| `1`  | Argomenti non validi |
| `2`  | File o directory non trovati |
| `3`  | Errore di I/O |
| `4`  | Errore di formato (file OSF non valido o danneggiato) |

## La cache sidecar

Più comandi possono utilizzare un **file sidecar** `.json`, collocato accanto al rispettivo file OSF. Il file sidecar contiene metadati per canale — numero di valori, primo e ultimo timestamp, intervallo di tempo globale — che il solo metablock OSF non contiene. Se è presente un file sidecar valido, comandi come `info` e `channels` rispondono immediatamente, anziché percorrere ogni blocco.

Il file sidecar viene utilizzato automaticamente; con `--no-cache` viene ignorato e il file sorgente viene analizzato direttamente. Il comando `cache` crea, rinnova, verifica e rimuove i file sidecar in grandi quantità.

## Riferimento dei comandi

### merge

Cerca ricorsivamente in una directory i file `.osf` e `.osfz`, seleziona i file che si sovrappongono a un intervallo di tempo e unisce i canali scelti in un unico file di output.

```
osftool merge <rootdir> <outputfile> [kanal ...] [optionen]
```

| Argomento    | Descrizione |
|--------------|--------------|
| `rootdir`    | Directory radice; viene cercata ricorsivamente per `.osf` e `.osfz` |
| `outputfile` | Percorso del file di output (`.osf`) |
| `kanal`      | Nomi di canale opzionali; omettere per unire tutti i canali |

| Opzione        | Descrizione |
|----------------|--------------|
| `--start <ts>` | Inizio dell'intervallo, ISO 8601 (predefinito: `1970-01-01T00:00:00`) |
| `--end <ts>`   | Fine dell'intervallo, ISO 8601 (predefinito: data e ora correnti) |
| `--osf4`       | Scrivere output OSF4 (predefinito: dalla configurazione `output.format`) |
| `--overwrite`  | Sovrascrivere i timestamp sovrapposti (predefinito: dalla configurazione `output.overlap`) |
| `--no-cache`   | Non leggere né scrivere file sidecar `.json` |
| `-q`, `--quiet`   | Sopprimere la visualizzazione live; solo errori, su stderr |
| `-v`, `--verbose` | Stampare ogni riga di log (classico output a scorrimento, nessuna barra live) |
| `--json`          | Stampare su stdout un flusso di eventi JSON Lines leggibile da macchina |
| `--log <pfad>`    | Scrivere in un file il log diagnostico completo (tutti i livelli) |

`--start` e `--end` sono opzionali e indipendenti: ogni limite ha un proprio valore predefinito e un'opzione specificata sovrascrive solo quel singolo limite. Senza entrambe le opzioni `merge` copre l'intero intervallo disponibile.

```
# Alles unter ./feld-daten in eine Datei zusammenführen
osftool merge ./feld-daten zusammen.osf

# Nur zwei Kanäle innerhalb eines Zeitfensters zusammenführen
osftool merge ./feld-daten fenster.osf Sensor/Temperatur Sensor/Druck \
  --start 2026-05-05T10:00:00 --end 2026-05-05T12:00:00
```

Per impostazione predefinita `merge` mostra un **indicatore di avanzamento live**: un breve titolo annuncia ogni fase — ricerca nella directory, lettura dei metadati dei file, lettura dei file, scrittura dell'output — e sotto viene ridisegnata sul posto un'unica riga con barra di avanzamento, che mostra la percentuale, il contatore dei file e il nome del file in elaborazione. Avvisi ed errori vengono emessi sopra la barra non appena si verificano; i messaggi informativi per canale di basso livello restano soppressi.

```
Reading files...
  [████████████████████░░░░░░░░░░░░░░░░░░░░] 50% (173/346) - 20260517_192802.osfz
```

Le opzioni di output modificano questa visualizzazione e si escludono a vicenda: `-v` / `--verbose` stampa invece il log completo a scorrimento; `-q` / `--quiet` stampa solo gli errori su stderr ed è altrimenti silenzioso; `--json` fornisce un flusso di eventi JSON Lines per le pipeline. `--log <pfad>` è ortogonale — scrive in un file il log diagnostico completo in ogni modalità. Se stdout viene reindirizzato a una pipe o a un file, osftool sostituisce automaticamente la visualizzazione live con semplici righe di avanzamento periodiche.

### export

Esporta i canali di un singolo file OSF/OSFZ in CSV — oppure, nella build per Windows, in HDF5.

```
osftool export <inputfile> <outputfile> [kanal ...] [optionen]
```

| Argomento    | Descrizione |
|--------------|--------------|
| `inputfile`  | File sorgente `.osf` o `.osfz` |
| `outputfile` | Percorso del file di output |
| `kanal`      | Nomi di canale opzionali; omettere per esportare tutti i canali |

| Opzione                    | Descrizione |
|----------------------------|--------------|
| `--format <fmt>`           | `csv` (predefinito) — un blocco XY per canale; `unified-csv` — un asse temporale comune con una colonna per canale; `hdf5` — un file HDF5, un dataset per canale (solo build per Windows) |
| `--timestamp-format <fmt>` | Formato del timestamp per `unified-csv`: `datetime` (predefinito), `seconds`, `iso8601`, `nanoseconds` |
| `--start <zeit>`           | Esportare solo i valori a partire da questo orario UTC (ISO 8601) |
| `--end <zeit>`             | Esportare solo i valori fino a questo orario UTC (ISO 8601) |
| `--decimal-sep <c>`        | Separatore decimale CSV: `comma` (predefinito) oppure `dot` |
| `--encoding <enc>`         | Codifica CSV: `iso-8859-1` (predefinito) oppure `utf-8` |
| `--chunk-size <n>`         | HDF5: valori per chunk del dataset (predefinito `8192`) |
| `--deflate-level <n>`      | HDF5: livello di compressione gzip `0`–`9` (predefinito `4`) |
| `--no-shuffle`             | HDF5: disattivare il prefiltro shuffle |
| `--namespace-sep <c>`      | HDF5: carattere che scompone un nome di canale in un percorso di gruppo HDF5 (predefinito `.`) |
| `--hdf5-lib-dir <pfad>`    | HDF5: directory in cui cercare `hdf5.dll` |
| `--exclude-empty`          | Saltare i canali senza valori |

`--start` e `--end` devono essere specificati insieme — un'opzione senza l'altra viene rifiutata. I valori predefiniti di `--decimal-sep` e `--encoding` provengono dal file di configurazione.

Il formato `hdf5` è disponibile solo nella build per Windows. Ogni canale diventa un dataset 1-D suddiviso in chunk e compresso con deflate, composto da record `{int64 timestamp_ns; value}`; il nome del canale viene scomposto in una gerarchia di gruppi HDF5 in corrispondenza di `--namespace-sep`, e i metadati a livello di file e di canale vengono scritti come attributi HDF5. L'esportazione richiede la libreria di runtime HDF5: `hdf5.dll` deve essere raggiungibile tramite il percorso di ricerca di sistema oppure tramite la directory indicata con `--hdf5-lib-dir`. Le opzioni `--chunk-size`, `--deflate-level`, `--no-shuffle` e `--namespace-sep` agiscono solo su `hdf5`; `--decimal-sep` e `--encoding` agiscono solo sui formati CSV.

```
osftool export motorbike.osf motorbike.csv --format unified-csv \
  --timestamp-format iso8601 --decimal-sep dot

# HDF5-Export (Windows-Build), stärkere Kompression
osftool export motorbike.osf motorbike.h5 --format hdf5 --deflate-level 6
```

### info

Visualizza i metadati e l'intervallo di tempo globale di un file.

```
osftool info <file> [optionen]
```

| Opzione      | Descrizione |
|--------------|--------------|
| `--no-cache` | Non utilizzare il file sidecar `.json`; analizzare direttamente il file OSF |
| `--json`     | Output in formato JSON |

Il rapporto contiene la versione del formato, il creatore e l'ora di creazione, tag, reason, commento, numero di canali, il primo e l'ultimo timestamp dei dati nonché la durata che ne risulta.

```
osftool info motorbike.osf
```

### channels

Elenca ogni canale del metablock.

```
osftool channels <file> [optionen]
```

| Opzione              | Descrizione |
|----------------------|--------------|
| `--filter <muster>`  | Filtro con caratteri jolly sul nome del canale, ad es. `GPS.*` |
| `--no-cache`         | Non utilizzare il file sidecar `.json` |
| `--json`             | Output come array JSON |

Ogni riga mostra indice del canale, nome, tipo di dato, unità fisica e — se è presente un file sidecar valido — il numero di valori nonché il primo/ultimo timestamp. Senza file sidecar queste colonne mostrano `?`.

```
osftool channels motorbike.osf --filter "Sensor/*"
```

### stat

Calcola minimo, massimo, media e deviazione standard per ogni canale numerico con l'algoritmo online a singolo passaggio di Welford.

```
osftool stat <file> [kanal ...] [optionen]
```

| Opzione          | Descrizione |
|------------------|--------------|
| `--start <zeit>` | Considerare solo i valori a partire da questo orario UTC (ISO 8601) |
| `--end <zeit>`   | Considerare solo i valori fino a questo orario UTC (ISO 8601) |
| `--json`         | Output in formato JSON |

`--start` e `--end` sono ciascuno opzionale in modo indipendente. I canali string, binary e gpslocation vengono elencati, ma contrassegnati come *non numerici*. Per i canali Int64/UInt64 viene segnalata la possibile perdita di precisione nella conversione in `Double` per il calcolo.

```
osftool stat motorbike.osf --start 2026-05-05T10:00:00
```

### cache

Gestisce i file di cache sidecar `.json` per ogni file `.osf`/`.osfz` al di sotto di una directory.

```
osftool cache <subbefehl> <rootdir> [optionen]
```

| Sottocomando | Descrizione |
|-----------|--------------|
| `build`   | Creare i file sidecar mancanti; saltare i file con un file sidecar già valido |
| `rebuild` | Ricreare forzatamente ogni file sidecar |
| `clean`   | Eliminare tutti i file sidecar al di sotto di `rootdir` |
| `status`  | Mostrare quali file non hanno un file sidecar valido |

| Opzione          | Descrizione |
|------------------|--------------|
| `--no-recursive` | Limitare alla sola directory radice (predefinito: includere le sottodirectory) |
| `--json`         | Output in formato JSON |

```
osftool cache build ./feld-daten
osftool cache status ./feld-daten
```

### config

Ispeziona e modifica le impostazioni persistenti che gli altri comandi utilizzano come valori predefiniti, e gestisce la voce `PATH`.

```
osftool config                    Alle aktuellen Einstellungen anzeigen
osftool config set <key> <wert>   Einen Wert setzen
osftool config reset              Alle Einstellungen auf Vorgaben zurücksetzen
osftool config install-path       osftool zum Benutzer-PATH hinzufügen
osftool config uninstall-path     osftool aus dem Benutzer-PATH entfernen
osftool config --json             Einstellungen als JSON anzeigen
```

Le chiavi disponibili sono descritte in [Configurazione](#konfiguration).

### convert

Converte un singolo file tra OSF4 e OSF5 riscrivendolo tramite il merger.

```
osftool convert <inputfile> <outputfile> [optionen]
```

| Opzione  | Descrizione |
|----------|--------------|
| `--osf4` | Scrivere l'output come OSF4 |
| `--osf5` | Scrivere l'output come OSF5 |
| `--json` | Output in formato JSON |

`--osf4` e `--osf5` si escludono a vicenda. Se non viene indicata nessuna delle due opzioni, la versione di destinazione deriva dalla chiave di configurazione `output.format`. Si noti che il convertitore scrive sempre blocchi dati con timestamp assoluto, indipendentemente dalla struttura del file sorgente.

```
osftool convert legacy.osf modern.osf --osf5
```

### verify

Percorre un file dall'inizio alla fine e ne verifica l'integrità strutturale.

```
osftool verify <file> [optionen]
```

Vengono eseguiti i seguenti controlli:

1. Magic header leggibile e versione riconosciuta.
2. Metablock elaborabile (XML o JSON valido).
3. L'indice del canale di ogni blocco è presente nel metablock.
4. Nessun blocco indica una lunghezza che supera la dimensione del file.
5. I timestamp aumentano in modo monotono per ciascun canale.
6. Il file termina correttamente — l'ultimo blocco non è troncato.

| Opzione    | Descrizione |
|------------|--------------|
| `--strict` | Trattare gli avvisi come errori (influisce sul codice di uscita) |
| `--json`   | Output in formato JSON |

I problemi che violano l'integrità vengono segnalati come errori e portano al codice di uscita `4`; le anomalie risolvibili (ad esempio un ultimo blocco troncato) sono avvisi. Con `--strict` anche gli avvisi portano al codice di uscita `4`.

```
osftool verify motorbike.osf --strict
```

## Configurazione {#konfiguration}

osftool memorizza le impostazioni persistenti in un file JSON. Gli altri comandi vi leggono i propri valori predefiniti, così che la configurazione agisca come criterio specifico dell'utente.

| Piattaforma | Percorso |
|-----------|-------------|
| Windows   | `%APPDATA%\osftool\config.json` |
| macOS / Linux | `~/.config/osftool/config.json` |

Il file è opzionale — se manca, ogni impostazione ricade sul proprio valore predefinito integrato.

| Chiave               | Predefinito  | Significato |
|----------------------|--------------|-----------|
| `output.format`      | `osf5`       | Formato di output predefinito per `merge` e `convert` |
| `output.overlap`     | `skip`       | Strategia di sovrapposizione: `skip` oppure `overwrite` |
| `export.decimal_sep` | `,`          | Separatore decimale predefinito per CSV |
| `export.encoding`    | `iso-8859-1` | Codifica predefinita per CSV |
| `cache.enabled`      | `true`       | Utilizzare i file sidecar `.json` |
| `cache.auto_build`   | `true`       | Creare automaticamente la cache durante una scansione |

I valori vengono letti e scritti con il comando `config`:

```
osftool config                          # alle Einstellungen anzeigen
osftool config set output.format osf4   # eine Vorgabe ändern
osftool config reset                    # Vorgaben wiederherstellen
```

Un'opzione della riga di comando sovrascrive il valore predefinito configurato sempre e solo per quella singola chiamata.

> Questo documento è rilasciato con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
