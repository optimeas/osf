---
title: Implementazione Delphi
description: L'implementazione di riferimento Delphi del Open Streaming Format — libreria, demo e riga di comando osftool
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Delphi
  - osftool
  - RAD Studio
  - Implementazione di riferimento
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Implementazione Delphi

L'implementazione Delphi è l'**implementazione di riferimento** del Open
Streaming Format: percorso completo di lettura e scrittura per OSF4 e
OSF5, un insieme di demo e la riga di comando `osftool`. Genera inoltre
l'insieme di [file di riferimento](../examples/osf_file_examples.md)
rispetto al quale vengono verificate le altre implementazioni.
Sviluppata con RAD Studio 23.0 (Delphi 12).

## Libreria

Le unit della libreria si trovano in `implementations/delphi/src/`. I
componenti pubblici più importanti:

| Unit | Contenuto |
|---|---|
| `OSF.Types` | Tipi di dati e tipi di canale (`TOSFDataType`, `TOSFVersion`, `TOSFGpsLocation`) e funzioni di supporto |
| `OSF.Filer` | `TOSFFile` — reader/writer in streaming per OSF4 e OSF5; decompressione OSFZ trasparente; parsing XML OSF4 tramite OmniXML (nessuna dipendenza da MSXML) |
| `OSF.Data.Manager` | `TOSFDataManager` — lettura di alto livello, popola canali tipizzati; accesso per nome |
| `OSF.Data.Channels` | Canali tipizzati in memoria, inclusi segmenti equidistanti (ogni `bcStartData` apre un segmento) |
| `OSF.Log` | Dispatcher di logging/avanzamento a livello di processo (`Logger: TOSFLog`) con pattern listener (DECISIONS §22) |
| `OSF.Export.CSV` / `OSF.Export.CSV.Unified` | Esportazione CSV — per canale (XY) o con asse temporale comune |
| `OSF.Export.HDF5` | Esportazione HDF5 (Windows; un dataset compound compresso per canale) |
| `OSF.Merger` | `TOSFMerger` — riunisce più file OSF di un intervallo di tempo in un unico file di output |
| `OSF.Meta.Cache` | Cache `.json` sidecar per ogni file OSF (intervalli di tempo, numero di campioni) per metadati rapidi |

Un programma senza form `OSFCompileCheck.dpr` nella directory radice
dell'implementazione copre ogni unit della libreria tramite `uses` e,
dopo i refactoring, fornisce un chiaro segnale di compilazione.

### Architettura di logging

Ogni unit della libreria scrive eventi di log e di avanzamento sul
`Logger: TOSFLog` globale. Le applicazioni registrano un
`TLoggerListener` (con proprio `MinLevel` e callback di evento); il
singleton si occupa della distribuzione. Dettagli in
[DECISIONS §22](https://github.com/optimeas/osf/blob/main/DECISIONS.md).

## Demo

In `implementations/delphi/demos/`:

| Demo | Scopo |
|---|---|
| `osfviewer/` | Caricare un file OSF, elencare i canali, visualizzare un canale come TeeChart (solo nell'IDE) |
| `osfgenerator/` | Scrive il set di riferimento di 17 file; la variante console `OSFGeneratorCLI` lo genera in modo non interattivo |
| `osfcsvexport/` | Demo di pipeline `TOSFFile` → `TOSFDataManager` → esportazione CSV |
| `osfmerger/` | Interfaccia VCL per `TOSFMerger` (Win64) |

## Riga di comando: `osftool`

`osftool` è lo strumento a riga di comando basato su verbi
dell'implementazione Delphi (Win64) con nove verbi — `merge`, `export`,
`info`, `channels`, `stat`, `cache`, `config`, `convert`, `verify`.

La descrizione completa si trova nel capitolo dedicato
**[osftool](../tools/osftool.md)**.

## Build

La libreria e la maggior parte delle demo vengono compilate con `dcc32`
(Win32) o `dcc64` (Win64) dalla rispettiva directory di progetto —
`osfmerger` e `osftool` come Win64. Esempio (compile check):

```powershell
cd implementations\delphi
dcc32 -B -Q OSFCompileCheck.dpr
```

L'esportazione HDF5 richiede in fase di esecuzione `hdf5.dll`; il
binding (`src/hdf5/`) è un'interfaccia Delphi autonoma e indipendente da
OSF verso la libreria C HDF5.

## Codice sorgente e ulteriori informazioni

- Codice sorgente su GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  directory `implementations/delphi/`
- Riga di comando: capitolo [osftool](../tools/osftool.md)
- Specifica del formato: capitolo [Formato OSF](../osf_general.md)

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
