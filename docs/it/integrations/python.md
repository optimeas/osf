---
title: Integrazione Python
description: Binding Python per il Open Streaming Format (OSF) — lettura e scrittura con prestazioni native tramite osfdata
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Python
  - osfdata
  - PyO3
  - NumPy
  - pandas
last_update:
  date: 2026-07-26
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Integrazione Python

Il pacchetto **`osfdata`** mette a disposizione un collegamento Python
completo al Open Streaming Format. Legge e scrive file OSF4 e OSF5,
riconosce automaticamente i file OSFZ compressi e si integra senza
soluzione di continuità nell'ecosistema scientifico Python basato su
NumPy.

A differenza delle implementazioni puramente Python, il lavoro vero e
proprio viene svolto in una libreria Rust. Python vede e utilizza solo
il sottile wrapper sovrastante. Il risultato: tempi di caricamento
nell'ordine dei millisecondi anche con diverse centinaia di migliaia di
campioni, overhead di memoria molto ridotto e dati numerici trasferiti
senza copia in array NumPy.

## A che cosa serve `osfdata`

`osfdata` risolve compiti tipici relativi ai dati OSF in Python:

- **Portare i dati nelle pipeline di analisi.** I file OSF vengono
  caricati, i singoli canali ottenuti direttamente come array NumPy e
  passati a librerie come SciPy, scikit-learn o PyTorch.
- **Riunire dati da fonti diverse.** I file OSF dal campo vengono letti,
  filtrati o combinati e scritti come nuovo file OSF.
- **Portare i dati esistenti allo stato attuale.** I file OSF4 vengono
  letti e riscritti come OSF5 — una comoda migrazione senza uno
  strumento separato.
- **Elaborare in modo trasparente i file compressi.** I file OSFZ (zlib
  o gzip) vengono riconosciuti e decompressi automaticamente, senza che
  l'utente debba configurare nulla.

Il pacchetto si rivolge ad analisti di dati, ingegneri e scienziati che
desiderano integrare i dati OSF nei flussi di lavoro Python. Non si
rivolge agli sviluppatori embedded che *generano* dati — per questo
esistono implementazioni di scrittura separate e snelle direttamente sui
dispositivi.

## Rapporto con `python-osf`

`osfdata` è il moderno successore del pacchetto esistente
[`python-osf`](https://github.com/optimeas/python-osf). `python-osf` è
un'implementazione Python pura, in grado di leggere esclusivamente OSF4
e nettamente più lenta con file di grandi dimensioni. `osfdata` offre
inoltre:

- supporto completo di OSF4 e OSF5 (lettura *e* scrittura),
- prestazioni notevolmente superiori grazie al fondamento Rust,
- copertura completa di tutti i tipi di dati, inclusi `binary`,
  `gpslocation` e gli interi senza segno,
- conformità con l'attuale revisione della specifica (2026-05-04),
- decompressione OSFZ trasparente sia per file compressi con zlib sia
  con gzip.

`python-osf` verrà contrassegnato come obsoleto in un momento
successivo, non appena `osfdata` coprirà nella pratica tutti i casi
d'uso.

## Piattaforme supportate

`osfdata` viene distribuito come pacchetto binario precompilato (wheel)
per le seguenti piattaforme:

| Piattaforma | Architettura | Versioni di Python |
|---|---|---|
| Linux | x86_64 | 3.9 – 3.13 |
| Linux | aarch64 (ARM 64 bit) | 3.9 – 3.13 |
| macOS | arm64 (Apple Silicon) | 3.9 – 3.13 |
| Windows | x86_64 | 3.9 – 3.13 |

Su tutte le piattaforme supportate l'installazione avviene senza
compilatore né altri strumenti. Per macOS Intel non viene fornita alcuna
wheel; l'installazione dalla source distribution è possibile, ma
richiede in quel caso una toolchain Rust locale. Lo stesso vale per
altre piattaforme esotiche (FreeBSD, Windows-on-ARM, distribuzioni
Linux più datate senza compatibilità manylinux).

## Installazione

`osfdata` può essere installato con entrambi i gestori di pacchetti
Python più diffusi.

### Con pip

```bash
pip install osfdata
```

### Con uv

```bash
uv pip install osfdata
```

`uv` è un gestore di pacchetti più recente e nettamente più veloce, che
combina `pip` e `venv`. Per chi non lo conoscesse ancora: installazione
e documentazione su [docs.astral.sh/uv](https://docs.astral.sh/uv).

## Import e primo esempio

Dopo l'installazione il pacchetto viene importato con il nome breve
`osf` (il nome di distribuzione `osfdata` viene usato su PyPI, il nome
di import ne è indipendente — un modello consueto in Python, paragonabile
a `scikit-learn` ↔ `import sklearn`).

```python
import osf

mgr = osf.load("messung.osf")
print(f"Datei enthält {len(mgr)} Kanäle")

temp = mgr.channel("Sensor.Temperatur")
samples = temp.samples()      # NumPy-Array, dtype passt zum OSF-Datentyp
zeitstempel = temp.timestamps_ns()
```

Con ciò l'essenziale è fatto: caricare il file, indirizzare un canale
per nome, ottenere i valori come array NumPy.

## Panoramica dell'API

La libreria è composta da pochi componenti pubblici e chiari. Tutti gli
ulteriori dettagli — ad esempio come viene gestito un canale con più
segmenti o come si distinguono i dati con timestamp da quelli
equidistanti — emergono lavorando con gli oggetti qui elencati.

### Funzioni a livello di modulo

| Funzione | Scopo |
|---|---|
| `osf.load(path)` | Carica un file OSF o OSFZ e restituisce un `DataManager`. Riconosce automaticamente il formato. |
| `osf.save(manager, path)` | Scrive un `DataManager` come file OSF5. |

### Classe `DataManager`

Rappresenta un file OSF caricato con tutti i canali e i metadati.

| Attributo / metodo | Descrizione |
|---|---|
| `len(mgr)` | Numero di canali. |
| `mgr.channels` | Elenco di tutti i canali (`list[Channel]`). |
| `mgr.channel(name)` | Indirizza un canale per nome. Restituisce `None` se non presente. |
| `mgr.channel_by_index(i)` | Indirizza un canale per indice numerico. |
| `mgr.stats` | Oggetto `ReaderStats` con statistiche sull'operazione di lettura. |

### Classe `Channel`

Un singolo canale con metadati e valori. Tre varianti — equidistante,
numerico con timestamp, variabile con timestamp (per stringhe e dati
binari) — vengono indirizzate tramite la stessa API.

| Attributo / metodo | Descrizione |
|---|---|
| `ch.name` | Nome del canale (spesso gerarchico, ad es. `"Motor.Drehzahl"`). |
| `ch.index` | Indice numerico del canale nel file. |
| `ch.data_type` | Tipo di dati come stringa (`"double"`, `"int32"`, `"string"` …). |
| `ch.channel_type` | `"equidistant"`, `"timestamped"` oppure `"variable"`. |
| `ch.sample_count` | Numero di campioni. |
| `ch.physical_unit` | Unità fisica (se indicata). |
| `ch.is_empty` | True se il canale non contiene dati. |
| `ch.samples()` | Valori come array NumPy. |
| `ch.timestamps_ns()` | Timestamp in nanosecondi dall'epoch come array NumPy `int64`. |
| `ch.segments` | Elenco dei segmenti (significativo solo per i canali equidistanti). |

### Classe `Segment`

Descrive una sezione di un canale equidistante — ad esempio quando una
registrazione è suddivisa in più fasi da eventi di trigger o correzioni
di deriva.

| Attributo | Descrizione |
|---|---|
| `seg.start_timestamp_ns` | Istante iniziale del segmento in nanosecondi dall'epoch. |
| `seg.sample_rate_hz` | Frequenza di campionamento all'interno di questo segmento in hertz. |
| `seg.sample_count` | Numero di campioni in questo segmento. |

### Classe `ReaderStats`

Informazioni diagnostiche sull'ultima operazione di caricamento.

| Attributo | Descrizione |
|---|---|
| `stats.compressed` | True se il file era compresso OSFZ. |
| `stats.compression_format` | `"gzip"`, `"zlib"` oppure `None`. |
| `stats.channels_total` | Numero di canali nel file. |
| `stats.blocks_total` | Numero di blocchi di dati letti. |
| `stats.elapsed_ms` | Tempo di caricamento in millisecondi. |
| `stats.file_size_bytes` | Dimensione del file sul disco. |

### Classe `WriterBuilder`

Costruisce file OSF5 a partire da definizioni di canali e dati dei
campioni. Il builder accetta i canali uno dopo l'altro; ogni canale può
accogliere più segmenti o blocchi di campioni.

```python
b = osf.WriterBuilder().creator("messsystem-v1").tag("vortest")

idx = b.add_channel(
    name="Sensor.Druck",
    data_type="double",
    channel_type="scalar",
    physical_unit="bar",
)

import numpy as np
werte = np.array([1.013, 1.015, 1.014, 1.012], dtype=np.float64)
b.add_equidistant_segment(
    idx,
    start_ns=1_700_000_000_000_000_000,
    sample_rate_hz=1.0,
    values=werte,
)

b.write_to_file("ausgabe.osf")
```

I metodi più importanti:

| Metodo | Scopo |
|---|---|
| `b.creator(s)` / `b.tag(s)` / `b.reason(s)` | Impostare i metadati del file. |
| `b.location(lat, lon, alt)` | Posizione geografica della registrazione. |
| `b.add_channel(...)` | Definire un nuovo canale; restituisce l'indice del canale. |
| `b.add_equidistant_segment(idx, start_ns, sample_rate_hz, values)` | Aggiungere un segmento equidistante (solo `float`/`double`). |
| `b.add_timestamped_samples(idx, ts_ns, values)` | Aggiungere campioni numerici con timestamp. |
| `b.add_string_samples(idx, ts_ns, values)` | Aggiungere campioni stringa con timestamp. |
| `b.add_binary_samples(idx, ts_ns, values)` | Aggiungere campioni di dati binari (ad es. immagini, audio). |
| `b.write_to_file(path)` | Scrivere in un file i dati raccolti. |

## Note d'uso

**Timestamp.** Tutte le indicazioni temporali sono interi a 64 bit in
nanosecondi dall'epoch Unix (UTC). Questa precisione copre sia le
misurazioni di vibrazioni ad alta frequenza sia i dati di processo
lenti, senza richiedere tipi di dati temporali diversi. Uno strato
opzionale di conversione verso il tipo `datetime` di Python è previsto
per una versione successiva.

**Tipi di dati NumPy.** Il dtype dell'array NumPy restituito corrisponde
direttamente al tipo di dati OSF: `double` → `float64`, `int32` →
`int32`, `bool` → `bool` e così via. Non avviene alcuna conversione
implicita — se il canale contiene `int16`, anche l'array NumPy è
`int16`.

**Gestione della memoria.** Gli array numerici di campioni vengono
trasferiti tra Rust e Python senza copia. Questo rende molto rapida la
lettura di canali di grandi dimensioni (diversi milioni di campioni)
anche su hardware modesto.

**Gestione degli errori.** Tutte le funzioni generano `osf.OsfError`
(una sottoclasse di `Exception`) quando qualcosa va storto — file non
trovato, formato non valido, tipo di dati sconosciuto. Il comportamento
in presenza di blocchi di dati sconosciuti o danneggiati segue il
principio best effort: i dati vengono forniti fino all'ultimo blocco
ben leggibile, dopodiché l'operazione si interrompe in modo pulito.

## Esempi applicativi

Notebook di esempio e script dettagliati seguiranno in una sezione
separata. Argomenti pianificati:

- Esplorazione rapida di un file OSF sconosciuto
- Migrazione da OSF4 a OSF5
- Filtraggio e unione di più registrazioni
- Passaggio a pandas per l'analisi tabellare
- Integrazione in dataset PyTorch

## Codice sorgente e ulteriori informazioni

- Il pacchetto su PyPI: [pypi.org/project/osfdata](https://pypi.org/project/osfdata/)
- Codice sorgente su GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf), directory `implementations/python/`
- Processo di build e di release: vedere [`BUILD.md`](https://github.com/optimeas/osf/blob/main/implementations/python/BUILD.md) nel repository
- Specifica del formato: vedere il capitolo [Formato OSF](../osf_general.md) in questa documentazione

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
