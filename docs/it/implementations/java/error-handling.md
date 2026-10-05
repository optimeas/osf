---
title: Gestione degli errori
description: La gerarchia di eccezioni OsfException, il catalogo completo degli errori, il modello di lettura best effort e lo stato di integrità in ReaderStats
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - OsfException
  - Best effort
  - Integrità
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Gestione degli errori

L'implementazione Java segnala gli errori tramite **eccezioni**. Tutti
gli errori della libreria sono istanze di `OsfException`, che estende
`RuntimeException` — si tratta quindi di eccezioni **non controllate**:
nessun obbligo di clausola `throws`, nessun `try/catch` forzato. Chi
desidera intercettarle cattura in modo mirato `OsfException` (o una
delle sue sottoclassi); chi non lo desidera le lascia propagare fino a
un handler centrale.

Separato da ciò è il **lettore best effort**: un singolo blocco di dati
difettoso o troncato *non* termina la lettura con un'eccezione, ma
viene saltato e conteggiato in `ReaderStats`. Solo gli errori
strutturali *prima* del flusso di blocchi (header, metablock) e le
violazioni gravi delle precondizioni generano eccezioni. Questa
separazione è il nucleo della gestione degli errori: fallire in modo
netto finché il file è in linea di principio non interpretabile —
proseguire non appena sono andati persi solo singoli blocchi finali.

## La gerarchia di eccezioni `OsfException`

```java
package com.optimeas.osf;

public class OsfException extends RuntimeException {
    // Nachricht (und optional Ursache) — die Nachricht ist nur zur Anzeige
    public static final class UnsupportedType       extends OsfException { }
    public static final class MalformedFile         extends OsfException { }
    public static final class UnknownHeaderToken    extends OsfException { }
    public static final class MetablockCrcMismatch  extends OsfException { }
}
```

Regole:

- **Diramare in base al tipo, visualizzare il messaggio soltanto.**
  `getMessage()` è un dettaglio leggibile dall'uomo e non fa parte
  dell'API — la formulazione può cambiare. Chi vuole reagire in modo
  programmatico verifica la classe (`instanceof` / ordine dei `catch`).
- Un `OsfException` **senza** una sottoclasse più specifica rappresenta
  il caso «API valida, ma usata in modo errato oppure I/O non riuscito»
  — soprattutto dai writer.
- Quando la causa è una `IOException` sottostante, questa viene
  riportata come `cause` (`getCause()`), in modo che lo stack trace
  resti completo.

## Catalogo degli errori

### In apertura e parsing (errori bloccanti)

Queste eccezioni si verificano prima ancora che venga letto un blocco
di dati utili — il file non è interpretabile come OSF e viene rifiutato
per intero.

| Eccezione | Significato | Origine tipica |
|---|---|---|
| `MalformedFile` | Difetto strutturale: magic header non ben formato, identificatore di versione sconosciuto, campo obbligatorio mancante nel metablock, numero non analizzabile, `sizeoflengthvalue` non valido (≠ 2/4), fine imprevista del flusso, nessun ritorno a capo nella finestra dell'header, JSON (OSF5) o XML (OSF4) non valido. In caso di causa di I/O riporta la `IOException` come `cause`. | Parser di header, metablock e blocchi |
| `UnknownHeaderToken` | Un token del magic header la cui chiave è sconosciuta alla libreria (regola must-understand). Volutamente separato da `MalformedFile`, affinché un token di integrità/estensione sconosciuto non appaia come un fuorviante errore di formato numerico. | Magic header |
| `MetablockCrcMismatch` | Il CRC32C dichiarato nel token di header `crc32c` non corrisponde ai byte grezzi del metablock. Con il profilo di integrità attivo il file viene rifiutato in modalità fail-closed — i metadati sono considerati compromessi. | Verifica del metablock |
| `UnsupportedType` | Il file utilizza un tipo di dati **rimosso** dalla specifica (`pair`, `triple`, `candata`, `gpsdata`). Rifiuto bloccante — il vecchio layout del payload non è riproducibile da una build attuale. Serve anche come errore in fase di **accesso**: un getter di tipo errato su `DataChannel` (ad es. valori double da un canale String). | Parser del metablock, `DataChannel` |

### In scrittura e nell'uso dell'API

I writer (`StreamingWriter`, `BlockWriter`) generano un semplice
`OsfException` a ogni precondizione violata — segnalano un errore di
programmazione, non un difetto dei dati:

| Causa | Significato di esempio |
|---|---|
| Indice del canale sconosciuto | Campione scritto su un canale non dichiarato |
| Tipo non corrispondente | Il tipo di scrittura non corrisponde al tipo di dati dichiarato del canale |
| Tipi di blocco misti | Un canale fornisce blocchi equidistanti *e* con timestamp — vietato dalla specifica |
| Fase del ciclo di vita errata | Campione scritto mentre il writer è ancora in configurazione oppure già chiuso |
| Nessun canale / lunghezza incoerente | `begin`/`writeTo` senza canali dichiarati; `timestamps.length ≠ values.length` |
| Profilo firmato richiesto | Il profilo `ed25519` (firmato) viene rifiutato in scrittura da questa libreria di livello CRC |
| Errore di I/O | File non apribile, errore di scrittura/`force` — `IOException` come `cause` |

## Lettore best effort: ciò che deliberatamente **non** è un errore

Il lettore del flusso di blocchi si ferma dove un file è *in linea di
principio* ancora leggibile, anziché generare un'eccezione. Il risultato
viene registrato in `ReaderStats`, consultabile tramite
`manager.stats()`:

| Situazione | Comportamento |
|---|---|
| Il file termina a metà di un blocco (interruzione dell'alimentazione, troncamento) | Vengono forniti tutti i blocchi completi, `stats.truncationSeen()` diventa `true`, l'iterazione termina in modo pulito |
| Corpo del blocco difettoso/alterato | Arresto best effort proprio in quel punto: `truncationSeen()` = `true`, i blocchi restanti già letti restano validi |
| Indice di canale sconosciuto nel flusso di blocchi | Senza definizione la larghezza del campo di lunghezza è ignota → arresto (`truncationSeen()`) anziché tentativo di congettura |
| Tipo di dati futuro sconosciuto | Il canale è `UNSUPPORTED`; i suoi blocchi vengono saltati in base alla lunghezza, tutti gli altri canali si caricano normalmente |
| Il CRC di frame di un blocco non corrisponde | Il blocco viene scartato, `stats.blocksCrcFailed()` aumenta, la lettura prosegue |
| Blocco di firma (canale riservato `0xFFFE`) | Non verificabile da questa libreria di livello CRC → saltato, `stats.blocksSignatureSkipped()` aumenta |
| Tipi di blocco riservati/vuoti | Consumati come saltati; nessun errore |

Solo gli errori *prima* del flusso di blocchi (header, CRC del
metablock, parsing del metablock) generano eccezioni — in quei punti
non è accettabile alcuna interpretazione parziale.

## Stato di integrità — `ReaderStats.verificationStatus()`

Dopo il caricamento, `stats.verificationStatus()` riassume il risultato
dell'integrità in una stringa stabile (vocabolario dalla specifica):

| Valore | Significato | Reazione consigliata |
|---|---|---|
| `"none"` | Il file non riporta alcun profilo di integrità | Nessuna — elaborazione normale |
| `"crc_valid"` | Profilo `crc`, **ogni** CRC di blocco verificato | Trattare i dati come integri |
| `"invalid"` | Profilo `crc`, almeno un CRC di blocco non è riuscito (`blocksCrcFailed() > 0`) | Avvisare/rifiutare; i blocchi interessati mancano nei dati |
| `"signature_unverifiable"` | File firmato (`ed25519`) che questa libreria di livello CRC **non** è in grado di verificare | Non indicarlo come «firmato e attendibile»; i dati utili sono comunque leggibili |

`verificationStatus()` deriva esclusivamente dal profilo dichiarato e
dai contatori — un `"invalid"` significa concretamente che i blocchi con
CRC errato sono stati saltati (non forniti).

## `try/catch` nella pratica

```java
import com.optimeas.osf.*;

try {
    DataManager mgr = DataManager.loadFromFile(Path.of("messung.osf"));

    ReaderStats st = mgr.stats();
    if (st.truncationSeen()) {
        log.warn("Datei am Ende abgeschnitten — {} Blöcke gelesen", st.blocksRead());
    }
    switch (st.verificationStatus()) {
        case "invalid" -> log.error("CRC-Fehler: {} Blöcke verworfen", st.blocksCrcFailed());
        case "signature_unverifiable" -> log.warn("Signatur nicht prüfbar");
        default -> { /* none / crc_valid — ok */ }
    }

    double[] werte = mgr.channelByName("temperatur")
                        .orElseThrow()
                        .asDoubles();          // wirft UnsupportedType bei Typ-Mismatch
} catch (OsfException.MetablockCrcMismatch e) {
    // Metadaten kompromittiert — Datei ablehnen
} catch (OsfException e) {
    // MalformedFile, UnknownHeaderToken, UnsupportedType, I/O …
    log.error("OSF konnte nicht geladen werden: {}", e.getMessage(), e);
}
```

Regole pratiche:

- **Catturare il caso specifico prima di quello generale.** Prima le
  sottoclassi specifiche (`MetablockCrcMismatch`, `UnknownHeaderToken`),
  poi `OsfException` come rete di sicurezza.
- **Dopo il caricamento controllare `stats()`** — un `load` riuscito non
  significa che ogni blocco sia arrivato. Troncamento e errori CRC sono
  silenziosi e compaiono solo nei contatori.
- **Le chiamate ai getter su `DataChannel` possono generare
  `UnsupportedType`** se il tipo richiesto non corrisponde al canale —
  verificare `dataType()` in anticipo oppure catturare in modo mirato.

## Ciclo di vita del writer

Lo `StreamingWriter` impone una macchina a stati `CONFIGURE →
STREAMING → CLOSED`. Una chiamata nella fase errata — campione prima di
`begin()`, scrittura dopo `close()` — genera `OsfException`. `close()`
è idempotente (una seconda chiamata è un no-op) e passa in modo
affidabile a `CLOSED` anche in caso di errore di I/O durante il flush
finale, così che `try-with-resources` (il writer è `AutoCloseable`) non
lasci il file aperto.

Per ulteriori dettagli vedere [Lettura](./reading.md),
[Scrittura](./writing.md), [Architettura](./architecture.md) e il
[manuale del formato OSF](../../osf_general.md). Torna alla
[panoramica Java](../java.md).

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
