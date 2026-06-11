---
title: C-ABI (osf-c)
description: Die reine C99-Schnittstelle der OSF-C++-Bibliothek — Ownership-Regeln, Funktionskatalog und Anbindung aus C, C# und OCX
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
---

# C-ABI — die Bibliothek `osf-c`

`osf-c` ist eine separate Shared Library (DLL / `.so` / `.dylib`) mit
einer **reinen C99-Schnittstelle** über dem C++-Kern — gedacht für
Konsumenten, die kein C++ sprechen: C-Programme, C#/P-Invoke,
ActiveX/OCX und künftige Sprach-Bindings. Vertrag: DECISIONS §23.

```bash
cmake -B build -D OSF_BUILD_C_API=ON
cmake --build build
```

Der einzige Header ist `osf/c_api.h` — er hängt nur von `<stdint.h>` /
`<stddef.h>` ab und ist für C++-Konsumenten `extern "C"`-geschützt.
**Keine C++-Exception überquert jemals die ABI-Grenze** (jeder
Einstiegspunkt ist try/catch-gekapselt).

## Ownership- und Lebensdauer-Regeln

Drei Regeln genügen für die gesamte API:

1. **`osf_manager` ist heap-besessen** — mit `osf_manager_free()`
   freigeben (NULL ist ein No-op).
2. **`osf_channel`-Handles und alle von Gettern zurückgegebenen
   `const char*` sind geliehen** — gültig nur bis zum
   `osf_manager_free()` des besitzenden Managers. Kopieren, wenn der
   Wert das Handle überleben soll.
3. **Sample-/Zeitstempel-Reader kopieren** in einen vom Aufrufer
   bereitgestellten Puffer (Copy-out) und geben die geschriebene
   Anzahl zurück.

## Fehlerbehandlung

Fehlbare Aufrufe geben `osf_status` zurück (`OSF_OK == 0`); die Codes
spiegeln `osf::Error::Code` (`OSF_ERR_IO`, `OSF_ERR_INVALID_METABLOCK`,
`OSF_ERR_REMOVED_IN_SPEC`, …, append-only). Bei einem Fehler steht die
Detail-Message **thread-lokal** bereit:

```c
osf_manager* m = NULL;
if (osf_load_file("messung.osf", &m) != OSF_OK) {
    fprintf(stderr, "laden: %s\n", osf_last_error_message());
    return 1;
}
```

`osf_last_error_message()` ist nie NULL und gültig bis zum nächsten
`osf_*`-Aufruf desselben Threads.

## Funktionskatalog

### Manager (Lesen)

| Funktion | Zweck |
|---|---|
| `osf_load_file(path, &m)` | OSF/OSFZ vollständig laden (transparente Dekompression inklusive) |
| `osf_manager_free(m)` | Manager + alles Geliehene freigeben |
| `osf_manager_channel_count(m)` | Kanalanzahl (Metablock-Reihenfolge) |
| `osf_manager_channel_at(m, i)` | geliehenes Kanal-Handle per Position `[0, count)` |
| `osf_manager_channel_by_name(m, name)` | geliehenes Kanal-Handle per Name; NULL wenn unbekannt |
| `osf_manager_is_compressed(m)` / `osf_manager_compression_format(m)` | OSFZ-Erkennung (`OSF_COMPRESSION_NONE/ZLIB/GZIP`) |
| `osf_manager_creator(m)` / `osf_manager_created_utc(m)` | Datei-Metadaten (`""` wenn nicht gesetzt) |
| `osf_version()` | Versionsstring der Bibliothek (statisch) |

### Kanal (Lesen)

| Funktion | Zweck |
|---|---|
| `osf_channel_name(c)` / `osf_channel_index(c)` | Identität |
| `osf_channel_data_type(c)` | `osf_data_type` (`OSF_DT_DOUBLE`, `OSF_DT_STRING`, …) |
| `osf_channel_physical_unit(c)` | Einheit (`""` wenn nicht gesetzt) |
| `osf_channel_sample_count(c)` | Sample-Anzahl |
| `osf_channel_read_timestamps(c, out, cap)` | Zeitstempel (ns) kopieren; äquidistante werden aus den Segmenten **rekonstruiert** |
| `osf_channel_read_f64(c, out, cap)` | Werte als `double` kopieren — konvertiert jeden numerischen/bool-Typ; `0` bei String/Binary/GPS |
| `osf_channel_read_i64(c, out, cap)` | Werte als `int64` kopieren (gleiche Konvertierungsregel) |
| `osf_channel_read_gps(c, out_lla, cap_samples)` | GPS: 3 Doubles (lat, lon, alt) **pro Sample**; `out_lla` braucht Platz für `3 * cap_samples` Doubles |
| `osf_channel_string_at(c, i)` | String-Sample `i`, geliehen, NUL-terminiert; NULL bei Bereichs-/Typ-Fehler |
| `osf_channel_binary_at(c, i, &len)` | Binary-Sample `i`, geliehen; Bytes dürfen eingebettete NULs enthalten |

Alle Copy-out-Reader schreiben `min(sample_count, cap)` Elemente und
geben diese Anzahl zurück — das übliche Muster ist „erst
`osf_channel_sample_count`, dann Puffer dimensionieren".

### Schreiben (Round-Trip)

| Funktion | Zweck |
|---|---|
| `osf_write_to_file(m, path)` | geladenen Manager als **OSF5** exportieren (DECISIONS §6) — auch als OSF4 → OSF5-Konverter nutzbar |

Ein vollwertiger Sample-für-Sample-C-Builder ist bewusst nicht Teil
des aktuellen Umfangs (BACKLOG); das ABI deckt Lesen + Re-Export ab.

## Vollständiges C-Beispiel

```c
#include <osf/c_api.h>
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

Gebaut und auf allen drei CI-Plattformen (Linux/macOS/Windows)
verifiziert wird genau dieses Nutzungsmuster durch den
Standalone-C99-Test `tests/c_api/test_c_api.c`.

## Anbindung aus C# (P/Invoke-Skizze)

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

Hinweise: Cdecl-Konvention; geliehene Strings mit
`Marshal.PtrToStringAnsi` kopieren, bevor der Manager freigegeben
wird; das thread-lokale `osf_last_error_message()` direkt nach dem
fehlgeschlagenen Aufruf auf **demselben** Thread abholen.

## Export-Mechanik

`OSF_C_API` expandiert beim Bauen der Bibliothek zu
`__declspec(dllexport)` (Windows) bzw.
`__attribute__((visibility("default")))` (ELF/Mach-O), beim
Konsumieren zu `dllimport`. Konsumenten müssen nichts definieren —
nur den Header einbinden und gegen `osf-c` linken.
