---
title: OSF5 – Documentazione specifica
description: Dettagli e specifiche del formato Open Streaming Format versione 5 (OSF5)
#sidebar_label: OSF5
image: "/img/om_social_card.png"
keywords:
  - OSF5
  - Fileformat
  - JSON
last_update:
  date: 2026-07-07
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# OSF5 – Documentazione specifica

Questo documento descrive tutti gli aspetti dell'**Open Streaming Format versione 5 (OSF5)** che vanno oltre la descrizione generale di OSF.  
Integra il file [`osf_general.md`](../osf_general.md), in cui sono illustrate tutte le strutture comuni a OSF4 e OSF5.

OSF5 è l'evoluzione di OSF4. Utilizza **JSON** come formato standard per il metablock e semplifica il byte di controllo.  
Allo stesso tempo OSF5 rimane pienamente **retrocompatibile** con OSF4.



## Magic header in OSF5

- **Identificatori consentiti:**  
  - `OSF5`  
  - `OSF4` (per la retrocompatibilità)  
  - `OCEAN_STREAM_FORMAT4` (identificatore legacy, ancora scritto dai dispositivi in commercio)  
  - `OCEAN_STREAMING_FORMAT4` (grafia storica meno recente)

- **Formato:**  
  ```
  OSF5 84512\n
  ```

- **Riconoscimento del metablock:**  
  - Primo carattere `<` → XML (metablock OSF4)  
  - Primo carattere `{` → JSON (metablock OSF5)  

- **Caratteristiche:**  
  - Per impostazione predefinita **metablock JSON**.  
  - I parser OSF5 possono leggere XML OSF4.



## Metablock in OSF5 (JSON)

OSF5 utilizza per impostazione predefinita **JSON** per il metablock.  
La struttura corrisponde funzionalmente alla variante XML di OSF4, ma è più semplice da analizzare ed è ottimizzata per i sistemi embedded.

### Esempio:

```json
{
  "osf": {
    "version": 5,
    "created_utc": "2025-07-27T12:00:00Z",
    "creator": "smartdevice:15002000001",
    "file_uuid": "f47ac10b-58cc-4372-a567-0e02b2c3d479",
    "channels": [
      {
        "index": 0,
        "name": "Sensor.Temperature",
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

- **Differenza rispetto a OSF4:** JSON anziché XML, ma con gli stessi contenuti logici.  
- **Compatibilità:** OSF5 può continuare a interpretare l'XML di OSF4.

#### Parametro a livello di file `file_uuid`

A livello di file, l'oggetto `osf` può contenere il parametro **`file_uuid`** — un UUID (versione 4) memorizzato come stringa JSON. Serve per l'**identità del file** ed è l'interfaccia attraverso cui il livello di sistema superiore esegue il controllo di sequenza/replay tra più file (interfaccia ai sensi della EN 50159). `file_uuid` è **obbligatorio** al livello di integrità *signed* ed è **raccomandato** a tutti gli altri livelli.


## Semplificazioni nel byte di controllo (tipi di blocco)

OSF5 riprende la struttura a blocchi di OSF4, ma riduce il numero di tipi di blocco utilizzati e ne semplifica l'interpretazione.

### Modifiche rispetto a OSF4:

- `bcContinuedRelStampData` non viene più utilizzato.  
- `bcStatusEvent` e `bcMessageEvent` non vengono più generati. Non essere più generati non significa non essere più letti: `bcContinuedRelStampData` e `bcMessageEvent` devono continuare a essere supportati dai reader **in ogni versione**, perché esistono sul campo file contenenti questi blocchi — vedere [`osf_general.md`](../osf_general.md#bcmessageevent).  
- Il bit 7 per valore singolo/multiplo rimane invariato.  

### Tipi di blocco supportati in OSF5:

| Valore | Enum               | Descrizione |
|------|--------------------|--------------|
| 0    | `bcReserved`       | Riservato per scopi interni. |
| 2    | `bcTimebaseRealign`| Adattamento dell'asse temporale (utilizzato raramente). |
| 5    | `bcContinuedData`  | Prosecuzione dei dati con frequenza di campionamento fissa. |
| 6    | `bcStartData`      | Blocco iniziale con frequenza di campionamento fissa. |
| 8    | `bcAbsTimeStampData` | Dati con timestamp assoluto. |
| 9    | `bcIntegritySignature` | Ancora di firma di integrità (solo livello *signed*, canale `0xFFFE`). Vedere [Profilo di integrità](osf5_integrity.md). Bit 7 = 0. |


## Profilo di integrità

OSF5 definisce un **profilo di integrità opzionale a tre livelli** (none ⊂ crc ⊂ signed), dichiarato mediante un token opzionale nel magic header. Il livello *crc* aggiunge a ogni blocco una checksum di frame (CRC32C); il livello *signed* aggiunge inoltre una catena di firme Ed25519 con certificati X.509 incorporati nel metablock. Senza token dichiarato, un file rimane al livello *none* e si comporta esattamente come in precedenza. La descrizione normativa completa è contenuta nella specifica [Profilo di integrità](osf5_integrity.md).


## Tipi di dati supportati in OSF5

OSF5 supporta gli stessi tipi di dati di OSF4.

| Tipo di dato | Dimensione | Descrizione |
|------------|----------|--------------|
| `bool`     | 1 byte   | Vero/Falso |
| `int8`     | 1 byte   | Numero intero con segno |
| `int16`    | 2 byte   | Numero intero con segno |
| `int32`    | 4 byte   | Numero intero con segno |
| `int64`    | 8 byte   | Numero intero con segno |
| `uint8`    | 1 byte   | Numero intero senza segno, intervallo di valori 0 … 255 |
| `uint16`   | 2 byte   | Numero intero senza segno, intervallo di valori 0 … 65 535 |
| `uint32`   | 4 byte   | Numero intero senza segno, intervallo di valori 0 … 4 294 967 295 |
| `uint64`   | 8 byte   | Numero intero senza segno, intervallo di valori 0 … 18 446 744 073 709 551 615 |
| `float`    | 4 byte   | IEEE 754 Single Precision |
| `double`   | 8 byte   | IEEE 754 Double Precision |
| `string`   | variabile | Codificato in UTF-8, lunghezza definita dalla dimensione del blocco. I writer OSF5 memorizzano il payload senza byte nullo finale; i reader OSF5 non devono rimuovere alcun byte finale – vedere [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary). |
| `binary` *(alias: `bytearray`)* | variabile | Sequenze di byte arbitrarie per dati di immagini, audio o altri dati binari con MIME type. La lunghezza massima è determinata da `sizeoflengthvalue`. I writer OSF5 memorizzano il payload senza byte nullo finale; i reader OSF5 non devono rimuovere alcun byte finale – vedere [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary). |
| `gpslocation` | 24 byte | Struttura per posizioni GPS |

## Terminazione delle stringhe

Per `bcAbsTimeStampData` con `datatype=string` o `datatype=binary` in OSF5:

- **I writer NON DEVONO** aggiungere un byte nullo finale (`0x00`). Il payload termina con l'ultimo byte di dati; `sizeoflengthvalue` definisce la lunghezza esatta.
- **I reader NON DEVONO** rimuovere alcun byte finale. Un `0x00` finale viene trattato come un normale byte di dati.

Per la motivazione vedere [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary).

### `bcStartData` con frequenza di campionamento

Come nella specifica generale e in OSF4, in OSF5 `bcStartData` contiene subito dopo il timestamp di avvio `int64` un campo `double` con la **frequenza di campionamento (Hz)**. Essa vale per tutti i successivi blocchi `bcContinuedData` dello stesso canale, finché non viene scritto un nuovo blocco `bcStartData`. Più blocchi `bcStartData` per canale, con lacune temporali intermedie, sono esplicitamente ammessi.

**Esempio di `bcStartData` in OSF5:**
`[int64 ZeitStart] [double SampleRate] [uint32 N] [double Wert1] [double Wert2] ... [double WertN]`

## Trailer e blocco info

OSF5 **non supporta più né un blocco dati info (0xFFFF) né un magic trailer.**  
La fine del file è definita esclusivamente dal raggiungimento dell'ultimo blocco dati scritto per intero.

- **Conseguenza:**  
  - I parser leggono il file fino all'ultimo blocco consistente.  
  - Alla fine del file non sono presenti statistiche o metainformazioni aggiuntive.  
  - Per ottenere informazioni sull'intervallo di tempo, il file deve essere analizzato in tutto o in parte.


### Perché OSF5 non utilizza più trailer né blocco info

In OSF4 era presente opzionalmente, alla fine del file, un **blocco dati info (indice 0xFFFF)** e un **magic trailer**, per rendere rapidamente disponibili le metainformazioni e l'intervallo di tempo del file.  
In OSF5 abbiamo rimosso deliberatamente entrambi gli elementi.

### Motivo principale: minore onere di implementazione

- La scrittura del blocco info richiede una preparazione finale di tutti i canali e di tutte le informazioni temporali.  
- Per il lato lettura significa codice aggiuntivo, che deve essere mantenuto e testato accanto al normale flusso di dati.  
- Poiché OSF è comunque progettato per leggere i file fino all'ultimo blocco completo, trailer e blocco info non offrono alcun vantaggio tecnico che giustifichi l'onere.

### Vantaggi complementari

- **Specifica più semplice:** meno casi particolari, implementazione più snella sia sul lato embedded sia sul lato PC.  
- **Nessuna duplicazione delle informazioni:** tutti i metadati rilevanti sono presenti nel metablock e negli stessi blocchi dati.  
- **Gli strumenti di analisi possono ottenere lo stesso risultato:** intervalli di tempo e statistiche possono essere ricavati dai blocchi regolari durante la lettura.

> **Conclusione:**  
> La rinuncia a trailer e blocco info in OSF5 è soprattutto una decisione a favore di un'implementazione più semplice e più facile da mantenere.  
> La robustezza del formato rimane pienamente garantita – deriva dalla struttura a blocchi e non da elementi aggiuntivi alla fine del file.


## Particolarità e limitazioni di OSF5

- **JSON** come formato principale, XML solo per la retrocompatibilità.  
- **Byte di controllo semplificato** con meno tipi di blocco.  
- Nessun trailer né blocco dati info alla fine del file.  
- `bcContinuedRelStampData`, `bcStatusEvent`, `bcMessageEvent` non vengono più generati — `bcContinuedRelStampData` e `bcMessageEvent` devono però continuare a essere letti (vedere [`osf_general.md`](../osf_general.md#bcmessageevent)).



## Esempio di file OSF5

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
        "name": "Sensor.Temperature",
        "channeltype": "scalar",
        "datatype": "double",
        "timeincrement": 1000000
      },
      {
        "index": 1,
        "name": "Sensor.Force",
        "channeltype": "scalar",
        "datatype": "double",
        "physicalunit": "N"
      },
      {
        "index": 2,
        "name": "Sensor.Path",
        "channeltype": "scalar",
        "datatype": "double",
        "physicalunit": "mm"
      }
    ]
  }
}
[BEGIN OF BINARY DATA]
```

> Questo documento è rilasciato con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
