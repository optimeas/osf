---
title: C-ABI (osf-c)
description: L'interfaccia C99 pura della libreria C++ OSF — regole di ownership, catalogo delle funzioni e integrazione da C, C# e OCX
sidebar_position: 5
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C-ABI
  - osf-c
  - C99
  - P/Invoke
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# C-ABI — la libreria `osf-c`

`osf-c` è una shared library separata (DLL / `.so` / `.dylib`) con
un'**interfaccia C99 pura** sopra il nucleo C++, pensata per
i consumatori che non usano C++: programmi C, C#/P-Invoke,
ActiveX/OCX e futuri binding per altri linguaggi.

```bash
cmake -B build -D OSF_BUILD_C_API=ON
cmake --build build
```

L'unico header è `osf/capi.h`: dipende solo da `<stdint.h>` /
`<stddef.h>` ed è protetto con `extern "C"` per i consumatori C++.
**Nessuna eccezione C++ attraversa mai il confine ABI** (ogni
punto di ingresso è incapsulato in try/catch).

## Regole di ownership e di ciclo di vita

Tre regole sono sufficienti per l'intera API:

1. **`osf_manager` è posseduto sull'heap**: va rilasciato con `osf_manager_free()`
   (NULL è un no-op).
2. **Gli handle `osf_channel` e tutti i `const char*` restituiti dai getter
   sono presi in prestito**: validi solo fino a
   `osf_manager_free()` del manager proprietario. Copiare se
   il valore deve sopravvivere all'handle.
3. **I reader di campioni/timestamp copiano** in un buffer
   fornito dal chiamante (copy-out) e restituiscono il
   numero di elementi scritti.

## Gestione degli errori

Le chiamate che possono fallire restituiscono `osf_status` (`OSF_OK == 0`); i codici
rispecchiano `osf::Error::Code` (`OSF_ERR_IO`, `OSF_ERR_INVALID_METABLOCK`,
`OSF_ERR_REMOVED_IN_SPEC`, …, append-only). In caso di errore il
messaggio di dettaglio è disponibile **thread-locale**:

```c
osf_manager* m = NULL;
if (osf_load_file("messung.osf", &m) != OSF_OK) {
    fprintf(stderr, "laden: %s\n", osf_last_error_message());
    return 1;
}
```

`osf_last_error_message()` non è mai NULL ed è valido fino alla chiamata
`osf_*` successiva dello stesso thread.

## Catalogo delle funzioni

### Manager (lettura)

| Funzione | Scopo |
|---|---|
| `osf_load_file(path, &m)` | Caricare completamente OSF/OSFZ (decompressione trasparente inclusa) |
| `osf_manager_free(m)` | Rilasciare il manager + tutto ciò che è preso in prestito |
| `osf_manager_channel_count(m)` | Numero di canali (ordine del metablock) |
| `osf_manager_channel_at(m, i)` | handle di canale preso in prestito per posizione `[0, count)` |
| `osf_manager_channel_by_name(m, name)` | handle di canale preso in prestito per nome; NULL se sconosciuto |
| `osf_manager_is_compressed(m)` / `osf_manager_compression_format(m)` | Riconoscimento OSFZ (`OSF_COMPRESSION_NONE/ZLIB/GZIP`) |
| `osf_manager_creator(m)` / `osf_manager_created_utc(m)` | Metadati del file (`""` se non impostati) |
| `osf_version()` | Stringa di versione della libreria (statica) |

### Canale (lettura)

| Funzione | Scopo |
|---|---|
| `osf_channel_name(c)` / `osf_channel_index(c)` | Identità |
| `osf_channel_data_type(c)` | `osf_data_type` (`OSF_DT_DOUBLE`, `OSF_DT_STRING`, …) |
| `osf_channel_physical_unit(c)` | Unità (`""` se non impostata) |
| `osf_channel_sample_count(c)` | Numero di campioni |
| `osf_channel_read_timestamps(c, out, cap)` | Copiare i timestamp (ns); quelli equidistanti vengono **ricostruiti** dai segmenti |
| `osf_channel_read_f64(c, out, cap)` | Copiare i valori come `double`: converte ogni tipo numerico/bool; `0` per String/Binary/GPS |
| `osf_channel_read_i64(c, out, cap)` | Copiare i valori come `int64` (stessa regola di conversione) |
| `osf_channel_read_gps(c, out_lla, cap_samples)` | GPS: 3 double (lat, lon, alt) **per campione**; `out_lla` richiede spazio per `3 * cap_samples` double |
| `osf_channel_string_at(c, i)` | Campione stringa `i`, preso in prestito, terminato da NUL; NULL in caso di errore di intervallo/tipo |
| `osf_channel_binary_at(c, i, &len)` | Campione binario `i`, preso in prestito; i byte possono contenere NUL incorporati |

Tutti i reader copy-out scrivono `min(sample_count, cap)` elementi e
restituiscono tale numero: il modello usuale è «prima
`osf_channel_sample_count`, poi dimensionare il buffer».

### Scrittura (round trip)

| Funzione | Scopo |
|---|---|
| `osf_write_to_file(m, path)` | Esportare il manager caricato come **OSF5**: utilizzabile anche come convertitore OSF4 → OSF5 |

Un builder C completo campione per campione non fa volutamente parte
dell'ambito attuale (estensione pianificata); la ABI copre lettura +
riesportazione.

## Esempio C completo

```c
#include <osf/capi.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <file>\n", argv[0]); return 2; }

    osf_manager* m = NULL;
    if (osf_load_file(argv[1], &m) != OSF_OK) {
        fprintf(stderr, "%s: %s\n", argv[1], osf_last_error_message());
        return 1;
    }

    size_t n = osf_manager_channel_count(m);
    printf("channels: %zu (creator: %s)\n", n, osf_manager_creator(m));

    for (size_t i = 0; i < n; ++i) {
        const osf_channel* c = osf_manager_channel_at(m, i);
        size_t count = osf_channel_sample_count(c);
        printf("  [%u] %-30s  %zu samples\n",
               osf_channel_index(c), osf_channel_name(c), count);

        if (osf_channel_data_type(c) == OSF_DT_DOUBLE && count > 0) {
            double*  vals = malloc(count * sizeof *vals);
            int64_t* ts   = malloc(count * sizeof *ts);
            size_t got_v = osf_channel_read_f64(c, vals, count);
            size_t got_t = osf_channel_read_timestamps(c, ts, count);
            if (got_v > 0 && got_t > 0)
                printf("      first: t=%lld ns  v=%g\n", (long long)ts[0], vals[0]);
            free(vals); free(ts);
        }
    }

    osf_manager_free(m);
    return 0;
}
```

Questo preciso modello d'uso viene compilato e verificato su tutte
e tre le piattaforme CI (Linux/macOS/Windows) tramite il
test C99 standalone `tests/capi/test_capi.c`.

## Integrazione da C# (bozza P/Invoke)

```csharp
internal static class OsfNative {
    [DllImport("osf-c", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int osf_load_file(
        [MarshalAs(UnmanagedType.LPStr)] string path, out IntPtr manager);

    [DllImport("osf-c", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void osf_manager_free(IntPtr manager);

    [DllImport("osf-c", CallingConvention = CallingConvention.Cdecl)]
    internal static extern UIntPtr osf_channel_read_f64(
        IntPtr channel, [Out] double[] buffer, UIntPtr cap);
    // … weitere Signaturen analog …
}
```

Note: convenzione Cdecl; copiare le stringhe prese in prestito con
`Marshal.PtrToStringAnsi` prima che il manager venga rilasciato;
recuperare il `osf_last_error_message()` thread-locale subito dopo la
chiamata fallita sullo **stesso** thread.

## Meccanismo di esportazione

`OSF_C_API` si espande, durante la compilazione della libreria, in
`__declspec(dllexport)` (Windows) o
`__attribute__((visibility("default")))` (ELF/Mach-O), e in `dllimport`
durante l'utilizzo. I consumatori non devono definire nulla:
è sufficiente includere l'header e fare il link con `osf-c`.

> Questo documento è rilasciato con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
