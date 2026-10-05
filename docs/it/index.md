---
title: Formato OSF
description: Introduzione al Open Streaming Format
#hide_title: true/false
#hide_table_of_contents: true/false
#sidebar_label: Label shown in the sidebar
#sidebar_position: Position relative to its parent if needed
image: "/img/om_social_card.png"
keywords:
  - Formato di file
  - OSF4
  - OSF5
  - OSF
last_update:
  date: 2026-07-07
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

## Il formato OSF – Introduzione

OSF è concepito come formato di streaming universale per dati di misura
e di processo. Risponde alle domande essenziali dell'acquisizione dati
moderna: come si possono memorizzare senza perdite dati che scorrono in
modo continuo? Come rimane il formato aperto ed estendibile, senza
complessità inutile? E come si conciliano i requisiti dei sistemi
embedded con l'efficienza di potenti piattaforme di analisi?

Attualmente OSF è disponibile nella versione 4, mentre la versione 5 è
in fase di implementazione. Sebbene OSF4 e OSF5 differiscano solo in
pochi punti, abbiamo scelto deliberatamente un salto di versione:
soprattutto il passaggio a un nuovo formato di header (JSON come
standard) e la gestione semplificata del byte di controllo nei dati
giustificano questa nuova versione principale. Allo stesso tempo OSF5
rimane completamente retrocompatibile con OSF4 e può leggere ed
elaborare i relativi file — salvo poche differenze di dettaglio — senza
limitazioni.

### Che cosa offre OSF nell'impiego pratico

OSF è sviluppato per la realtà dei moderni sistemi di misura: flussi di
dati che scorrono in modo continuo e devono essere acquisiti
rapidamente e memorizzati in modo sicuro. Al centro c'è un obiettivo:
la registrazione senza lacune e senza perdita di dati, anche quando i
sistemi si spengono bruscamente o l'alimentazione viene interrotta.

Ma OSF non si ferma alla scrittura: è altrettanto ottimizzato per
l'**elaborazione successiva.** Anziché memorizzare i singoli valori uno
dopo l'altro, OSF archivia i dati a blocchi. Ciò consente:

-   **memorizzazione efficiente e rapida** anche con elevate velocità di trasmissione dei dati,
-   **caricamento e analisi rapidi** di grandi quantità di dati,
-   strutture coerenti che funzionano in modo performante sia su sistemi embedded sia su piattaforme di analisi.

### I vantaggi in sintesi:

-   **Registrazione dati continua e senza perdite** su dispositivi embedded.
-   **Robustezza** contro interruzioni di corrente o spegnimenti imprevisti.
-   **Struttura a blocchi efficiente** per memorizzare e caricare rapidamente.
-   **Flessibile** per tutti i dati con riferimento temporale: valori singoli, vettori, matrici, immagini e dati audio.
-   **Aperto e semplice da implementare**, con una struttura chiara.
-   **Uniforme** – un unico formato per edge, laboratorio e cloud.
-   **Profilo di integrità opzionale** (da OSF5) con tre livelli – *none*, *crc*, *signed*: rilevamento a blocchi della corruzione dei dati tramite CRC32C, una catena di firme Ed25519 nonché verificabilità da parte di terzi basata su X.509/PKI. Vedere [Profilo di integrità OSF5](references/osf5_integrity.md); documento concettuale: [doi.org/10.5281/zenodo.21227941](https://doi.org/10.5281/zenodo.21227941).

OSF non è quindi solo un formato di memorizzazione, ma uno strumento per
accompagnare **i flussi di dati in modo efficiente e senza perdite lungo
l'intero ciclo di vita** — dal primo valore misurato fino all'analisi.


## Una soluzione aperta per i dati con riferimento temporale

La risposta di OSF a questi requisiti è una struttura chiara e aperta,
valida in egual misura per **OSF4** e **OSF5**:

-   **Magic header** per l'identificazione e l'elaborazione rapida.
-   **Metablock (XML o JSON)** per descrivere canali, tipi di dati e informazioni di contesto.
-   **Blocchi di dati binari** per uno streaming continuo e robusto.
-   **Memorizzazione orientata ai blocchi**, per scrivere in modo efficiente grandi quantità di dati e ricaricarle rapidamente.

### Struttura fondamentale dei file OSF

L'architettura di base consente di archiviare tutti i dati con
riferimento temporale in un unico formato coerente. OSF è **documentato
apertamente**, facile da implementare e scalabile dai dispositivi edge
al cloud.

![Struttura dei file OSF.](media/dfa207f25f79c84ec79e2e2f6c2c42cc.jpg)

### Che cosa cambia in OSF5

-   JSON come standard per l'header (con retrocompatibilità con OSF4/XML).
-   Utilizzo semplificato del byte di controllo nel blocco di dati, per un minore onere di implementazione.
-   Eliminazione del trailer a favore di una struttura di file più chiara.
-   Magic header flessibili (`OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) con riconoscimento automatico di XML (`<`) o JSON (`{`).

Di seguito viene descritto ciò che caratterizza OSF come formato –
**valido in generale per OSF4 e OSF5**. I dettagli sulle funzioni
specifiche e sui canali vettoriali o matriciali si trovano in documenti
dedicati.

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
