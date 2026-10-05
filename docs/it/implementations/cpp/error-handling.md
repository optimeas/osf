---
title: Gestione degli errori
description: Result<T> e osf::Error nel nucleo privo di eccezioni, il catalogo completo dei codici di errore e il livello opt-in osf::throwing
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - Result
  - Error
  - Eccezioni
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Gestione degli errori

Il nucleo dell'implementazione C++ è **privo di eccezioni**: ogni
operazione che può fallire restituisce `osf::Result<T>`, ovvero un
`tl::expected<T, osf::Error>`. Chi preferisce le eccezioni vi sovrappone il
sottile livello opt-in `osf::throwing`. Entrambi gli stili possono essere
combinati.

## `osf::Error` e `osf::Result<T>`

```cpp
struct Error {
    Code        code;     // stabile Kategorie — hierauf verzweigen
    std::string message;  // menschenlesbares Detail — nur zur Anzeige
};

template <typename T>
using Result = tl::expected<T, Error>;
```

Idioma di base:

```cpp
auto r = osf::DataManager::loadFromFile(pfad);
if (!r) {
    log("Laden fehlgeschlagen [{}]: {}",
        osf::errorCategoryName(r.error().code),   // stabiler Name, z. B. "io_error"
        r.error().message);
    return;
}
osf::DataManager const& mgr = *r;   // oder r.value()
```

Regole:

- **Ramificare su `code`, mostrare `message` soltanto.** Il testo del
  messaggio non fa parte dell'API e può cambiare.
- Tutti i valori restituiti `Result` sono `[[nodiscard]]`: il compilatore segnala
  gli errori ignorati.
- `errorCategoryName(code)` restituisce un identificatore stringa stabile
  per i log (statico, senza ownership).
- `Result<void>` segnala operazioni di puro successo/errore
  (`writer.start()`, `writeToFile(...)`, …): `if (!r) …`.

## Catalogo dei codici di errore

### Errori di input e di API

| Codice | Significato | Origine tipica |
|---|---|---|
| `InvalidArgument` | Precondizione dell'API violata: `ChannelDef` non valido, indice di canale sconosciuto nel writer, `count == 0`, writer in una fase del ciclo di vita errata, frequenza di campionamento non positiva | Writer |
| `IoError` | Errore di file/stream: impossibile aprire, errore di lettura/scrittura, errore di fsync | ovunque |
| `NotFound` | riservato alle API di lookup (i lookup dei canali restituiscono invece `nullptr`) | — |
| `Unknown` | Fallback senza categoria più specifica; codice di un `Error` costruito di default | — |
| `ParseError` | errore di parsing generico, se non si adatta nessuna categoria più specifica (raro) | Parser |

### Magic header

| Codice | Significato |
|---|---|
| `InvalidMagicHeader` | La prima riga non è un header OSF ben formato: per lo più «non è un file OSF» |
| `UnsupportedVersion` | Header analizzabile, ma l'identificatore non è una delle quattro grafie accettate (`OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) |
| `MagicHeaderTooLong` | Nessun ritorno a capo entro 128 byte: sicuramente non è un file OSF |

### Metablock

| Codice | Significato |
|---|---|
| `InvalidMetablock` | Errore strutturale: campo obbligatorio mancante, numero non analizzabile, `sizeOfLengthValue` ≠ 2/4 (altrimenti corromperebbe in silenzio ogni lettura di blocco), elemento radice errato |
| `JsonParseError` | OSF5: il corpo del metablock non è JSON valido (diagnostica del parser in `message`) |
| `XmlParseError` | OSF4: il corpo del metablock non è XML ben formato (diagnostica + offset in byte in `message`) |
| `RemovedInSpec` | Il file utilizza un tipo di dati **rimosso** con la revisione della specifica 2026-05-04 (`pair`, `triple`, `candata`, `gpsdata`). Viene rifiutato in modo netto: il vecchio layout del payload non è riproducibile da una build attuale; il messaggio indica il sostituto |

### Flusso di blocchi

| Codice | Significato |
|---|---|
| `UnknownChannelIndex` | Il blocco fa riferimento a un indice di canale senza definizione nel metablock. Senza definizione la larghezza del campo di lunghezza è sconosciuta → segnale di corruzione, interruzione netta |
| `InvalidBlock` | Payload strutturalmente difettoso (lunghezza errata per il tipo di dati, blocco equidistante su canale stringa, campione che supera la capacità di blocco dello streaming writer, …) |
| `ChannelMixedBlockTypes` | Un canale fornisce sia blocchi equidistanti (`bcStartData`/`bcContinuedData`) sia blocchi timestamped: vietato dalla specifica |
| `ContinuedDataWithoutStart` | `bcContinuedData` senza un `bcStartData` precedente: senza un segmento aperto la continuazione non ha base temporale |
| `RelStampWithoutAnchor` | `bcContinuedRelStampData` senza un precedente timestamp assoluto: i delta non hanno ancoraggio |
| `DataTypeMismatch` | Tipo richiesto ≠ tipo memorizzato (ad es. `asDoublesFlat` su un canale `int32`, `asStrings` su un canale binario) |

### Ciò che volutamente **non** è un errore

| Situazione | Comportamento |
|---|---|
| Il file termina a metà di un blocco (interruzione di corrente) | Best effort: vengono restituiti tutti i blocchi completi, `stats.blocksTruncated = 1`, l'iterazione termina in modo pulito |
| Tipo di dati/tipo di canale futuro sconosciuto | Il canale viene analizzato come `Unsupported`, i blocchi vengono consumati come `Skipped` mantenendo l'allineamento, i restanti canali si caricano normalmente |
| Control byte deprecati/riservati (vecchi file di campo) | `BlockKind::Skipped` con `SkipReason`, contatore in `stats` |
| Campi di canale deprecati (`scale`, `offset`, `physicalunit1..3`, …) | tollerati e ignorati in silenzio (i file di campo reali li contengono tutti) |
| Lookup di un canale senza corrispondenza | `nullptr`, nessun `Result` |

## Il livello che lancia eccezioni — `osf::throwing`

Header-only, opt-in (`#include <osf/throwing.h>`), volutamente **non**
incluso nell'header umbrella `<osf/osf.h>` e non compilato
nella libreria. Chi non lo include mai, non introduce alcun
meccanismo di eccezioni.

```cpp
#include <osf/throwing.h>

try {
    auto mgr = osf::throwing::load("messung.osf");      // DataManager oder wirft
    osf::throwing::writeToFile(mgr, "kopie.osf");

    osf::StreamingWriter w{pfad};
    auto ch = osf::throwing::unwrap(w.addChannel(def)); // Result<T> -> T oder wirft
    osf::throwing::unwrap(w.start());
    osf::throwing::unwrap(w.writeTimestampedSample<double>(ch, ts, wert));
    osf::throwing::unwrap(w.close());
} catch (osf::Exception const& e) {
    // e.what()  — Message (oder Kategorie-Name, wenn Message leer)
    // e.code()  — Error::Code für programmatisches Verzweigen
    // e.error() — der vollständige strukturierte osf::Error
}
```

Il livello è composto esattamente da tre elementi:

| Elemento | Scopo |
|---|---|
| `osf::Exception : std::runtime_error` | contiene l'`osf::Error` completo; risiede in `osf`, non in `osf::throwing` |
| `osf::throwing::unwrap(Result<T>)` | adattatore universale: estrarre il valore oppure lanciare un'eccezione. Funziona con **qualsiasi** `Result` del nucleo, anche dai metodi dei writer: per questo non servono wrapper che lanciano eccezioni per ogni metodo |
| `osf::throwing::load / writeToFile / writeTo` | controparti che lanciano eccezioni delle operazioni di alto livello più frequenti |

## Scelta dello stile nella pratica

- **Codice di libreria/embedded, hot path, codebase con
  `-fno-exceptions`:** restare con il nucleo `Result`.
- **Codice applicativo con una strategia di eccezioni esistente:** usare `throwing`
  sul bordo esterno; internamente resta tutto `Result`.
- **Misto:** `unwrap` in modo puntuale dove un errore verrebbe comunque
  solo propagato, ad es. in un `main` CLI che in alto ha un
  `try/catch`.

## Caso particolare dei writer: sticky error

Lo `StreamingWriter` memorizza il primo errore di I/O (stato «Broken»)
e lo restituisce a **ogni** chiamata successiva, inclusa
`close()`. Nei cicli di scrittura è quindi sufficiente un controllo degli errori per
iterazione; la causa non va persa neppure se la valutazione avviene solo alla
fine.

> Questo documento è rilasciato con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
