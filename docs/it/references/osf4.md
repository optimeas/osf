---
title: OSF4 – Documentazione specifica
description: Dettagli e specifiche del formato Open Streaming Format versione 4 (OSF4)
#sidebar_label: OSF4
image: "/img/om_social_card.png"
keywords:
  - OSF4
  - Fileformat
  - XML
last_update:
  date: 2026-07-07
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# OSF4 – Documentazione specifica

Questo documento descrive tutti gli aspetti dell'**Open Streaming Format versione 4 (OSF4)** che vanno oltre la descrizione generale di OSF.  
Integra il file [`osf_general.md`](../osf_general.md), in cui sono illustrate tutte le strutture comuni a OSF4 e OSF5.

OSF4 è la versione classica del formato. Utilizza esclusivamente **XML** per il metablock e costituisce la base della retrocompatibilità in OSF5.

Il profilo di integrità (CRC32C / firma) esiste a partire da OSF5; i file OSF4 non ne sono interessati. Vedere [Profilo di integrità OSF5](osf5_integrity.md).



## Magic header in OSF4

- **Identificatori consentiti:**  
  - `OSF4`  
  - `OCEAN_STREAM_FORMAT4` — identificatore legacy, ancora scritto dai dispositivi in commercio  
  - `OCEAN_STREAMING_FORMAT4` — grafia storica meno recente

- **Formato:**  
  ```
  OSF4 173762\n
  ```

- **Caratteristiche:**  
  - Sempre **metablock XML** subito dopo l'header.  
  - Nessun supporto JSON.  
  - I parser OSF5 possono leggere integralmente i file OSF4.

---

## Metablock in OSF4 (XML)

OSF4 utilizza esclusivamente **XML** per il metablock.  
Esso contiene tutte le informazioni sul file e sui canali e inizia con un prologo standard:

```xml
<?xml version="1.0" encoding="UTF-8"?>
```

### Elemento radice `<osf>`

Esempio:

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

#### Attributi:

- **version:** versione del formato OSF4 (default: `"1"`).  
- **created_utc:** momento della creazione del file in formato ISO 8601 (UTC).  
- **creator:** identificazione del dispositivo o del programma che ha generato il file.  
- **created_at_longitude / latitude / altitude:** posizione geografica opzionale.  
- **reason:** motivo della creazione del file (`BOOT`, `SEQUENCE`, `TRIGGERED`).  
- **total_seq_no:** numero di sequenza assoluto dall'avvio del sistema.  
- **triggered_seq_no:** numero di sequenza relativo dall'ultimo trigger.  
- **namespacesep:** separatore per i nomi di canale gerarchici (default: `"."`).  
- **tag:** tag libero per la classificazione del file (default: `"preview"`).  
- **comment:** commento opzionale.  

---

## Elenco dei canali `<channels>`

Contiene tutti i canali del file.

```xml
<channels count="8">
    <channel index="0"
             name="Sensor.Temperature"
             channeltype="scalar"
             datatype="double"
             timeincrement="1000000"
             sizeoflengthvalue="2"
             physicalunit="°C"
             reference="uuid"
             physicaldimension="temperature"/>
</channels>
```

- **count:** numero di canali nel file.

---

## Descrizione del canale `<channel>`

Tutti i parametri sono quelli descritti nella documentazione generale di OSF; per OSF4 vale quanto segue:

- **Metablock:** sempre XML  
- **`channeltype` supportati:** `scalar`, `vector`, `matrix`, `binary`  
- **Parametri di vettori e matrici:** nella definizione del canale tramite `rows`, `columns` e gli attributi correlati  
- **`sizeoflengthvalue`:** campo obbligatorio (2 o 4 byte)  

---

## Tipi di dati supportati in OSF4

| Tipo di dato | Dimensione | Descrizione |
|-------------|----------|--------------|
| `bool`      | 1 byte   | Vero/Falso |
| `int8`      | 1 byte   | Numero intero con segno |
| `int16`     | 2 byte   | Numero intero con segno |
| `int32`     | 4 byte   | Numero intero con segno |
| `int64`     | 8 byte   | Numero intero con segno |
| `uint8`     | 1 byte   | Numero intero senza segno, intervallo di valori 0 … 255 |
| `uint16`    | 2 byte   | Numero intero senza segno, intervallo di valori 0 … 65 535 |
| `uint32`    | 4 byte   | Numero intero senza segno, intervallo di valori 0 … 4 294 967 295 |
| `uint64`    | 8 byte   | Numero intero senza segno, intervallo di valori 0 … 18 446 744 073 709 551 615 |
| `float`     | 4 byte   | IEEE 754 Single Precision |
| `double`    | 8 byte   | IEEE 754 Double Precision |
| `string`    | variabile | Codificato in UTF-8, lunghezza definita dalla dimensione del blocco. Su disco viene sempre completato con un byte nullo finale (`0x00`) – per le regole di scrittura e lettura dipendenti dalla versione vedere [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary). |
| `gpslocation` | 24 byte | Struttura per posizioni GPS |

## Terminazione delle stringhe

Per `bcAbsTimeStampData` con `datatype=string` o `datatype=binary` in OSF4:

- **I writer DEVONO** aggiungere a ogni payload un byte nullo finale (`0x00`).
- **I reader DEVONO** rimuovere incondizionatamente l'ultimo byte del payload — la sua presenza è garantita.

Per la motivazione e la regola completa valida per più versioni vedere [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary).

---

## Blocchi dati in OSF4

OSF4 utilizza le strutture dei blocchi dati descritte nella specifica generale.  
Specifico per OSF4:

- **Byte di controllo:** sono supportati tutti i tipi di blocco originali `0–8`.  
- **`bcContinuedRelStampData`:** ancora utilizzato in OSF4 (rimosso a partire da OSF5).  
- **`bcStatusEvent` e `bcMessageEvent`:** presenti; i writer NON DEVONO generarli. `bcMessageEvent` deve tuttavia essere **letto** — i dispositivi in uso sul campo scrivono ancora così i canali `string` OSF4, e un reader che salta questo tipo di blocco perde tali canali senza alcun avviso. Vedere [`osf_general.md`](../osf_general.md#bcmessageevent).  
- **Metadati:** sempre in formato XML.

### `bcStartData` con frequenza di campionamento

A partire da questa revisione della specifica, `bcStartData` in OSF4 — come in OSF5 — contiene subito dopo il timestamp di avvio `int64` un campo `double` con la **frequenza di campionamento (Hz)**. La frequenza vale per tutti i successivi blocchi `bcContinuedData` dello stesso canale, finché non viene scritto un nuovo blocco `bcStartData`.

**Esempio di `bcStartData` in OSF4:**
`[int64 ZeitStart] [double SampleRate] [uint32 N] [double Wert1] [double Wert2] ... [double WertN]`

I nuovi writer OSF4 **devono** scrivere questo campo. I reader che incontrano un file OSF4 privo di tale campo (vecchi file esistenti) falliscono con un errore di formato — non esiste alcun fallback implicito da `timeincrement`.

### Limitazioni:

- **Canali equidistanti (`bcStartData`, `bcContinuedData`):** solo tipi numerici (`int*`, `float`, `double`).  
- **Canali con timestamp (`bcAbsTimeStampData`, `bcContinuedRelStampData`):** tutti i tipi sono consentiti.

---

## Trailer XML e magic trailer

OSF4 supporta opzionalmente un blocco informativo XML con statistiche dei canali e un magic trailer.

### Esempio di trailer XML:

```xml
<trailer finalized_utc="2019-08-12T12:23:01+02:00" reason="fileStartGrid_min">
    <channels count="8">
        <channel index="0" samples="29452" last_ns="1384899599997800000"/>
        <channel index="1" samples="29452" last_ns="1384899599997800000"/>
    </channels>
</trailer>
```

### Magic trailer:

```
OSF_STREAM_END 321316454==============
```

- Lungo 40 byte, il numero indica la posizione del blocco `0xFFFF`.

---

## Particolarità e limitazioni di OSF4

- Solo metablock XML, nessun supporto JSON.  
- Byte di controllo più complesso, più tipi di blocco rispetto a OSF5.  
- Canali vettoriali e matriciali tramite parametri XML (`rows`, `columns`).  
- `bcContinuedRelStampData` è supportato, ma non è più presente in OSF5.  

---

## Esempio di file OSF4

```text
OSF4 30269
<?xml version="1.0" encoding="UTF-8"?>
<osf version="4" created_utc="2019-08-12T12:23:01+02:00" creator="smartdevice:14001000021">
  <channels count="2">
    <channel index="0" name="Sensor.Temperature" channeltype="scalar" datatype="double" timeincrement="1000000"/>
    <channel index="1" name="Sensor.Pressure" channeltype="scalar" datatype="double" timeincrement="1000000"/>
  </channels>
</osf>
[BEGIN OF BINARY DATA]
```

> Questo documento è rilasciato con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
