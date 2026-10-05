---
title: ABI C (osf-c)
description: L'interface C99 pure de la bibliothèque C++ OSF — règles de propriété, catalogue de fonctions et intégration depuis C, C# et OCX
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
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# ABI C — la bibliothèque `osf-c`

`osf-c` est une bibliothèque partagée distincte (DLL / `.so` / `.dylib`) avec
une **interface C99 pure** au-dessus du noyau C++ — destinée aux
consommateurs qui ne parlent pas C++ : programmes C, C#/P-Invoke,
ActiveX/OCX et futurs bindings pour d'autres langages.

```bash
cmake -B build -D OSF_BUILD_C_API=ON
cmake --build build
```

Le seul header est `osf/capi.h` — il ne dépend que de `<stdint.h>` /
`<stddef.h>` et est protégé par `extern "C"` pour les consommateurs C++.
**Aucune exception C++ ne franchit jamais la frontière de l'ABI** (chaque
point d'entrée est encapsulé dans un try/catch).

## Règles de propriété et de durée de vie

Trois règles suffisent pour toute l'API :

1. **`osf_manager` est alloué sur le tas et possédé** — le libérer avec
   `osf_manager_free()` (NULL est un no-op).
2. **Les handles `osf_channel` et tous les `const char*` renvoyés par les
   getters sont empruntés** — valides seulement jusqu'au
   `osf_manager_free()` du manager propriétaire. Copier si la
   valeur doit survivre au handle.
3. **Les lecteurs d'échantillons/horodatages copient** dans un tampon
   fourni par l'appelant (copy-out) et renvoient le nombre
   d'éléments écrits.

## Gestion des erreurs

Les appels susceptibles d'échouer renvoient `osf_status` (`OSF_OK == 0`) ; les codes
reflètent `osf::Error::Code` (`OSF_ERR_IO`, `OSF_ERR_INVALID_METABLOCK`,
`OSF_ERR_REMOVED_IN_SPEC`, …, append-only). En cas d'erreur, le message
détaillé est disponible **localement au thread** :

```c
osf_manager* m = NULL;
if (osf_load_file("messung.osf", &m) != OSF_OK) {
    fprintf(stderr, "laden: %s\n", osf_last_error_message());
    return 1;
}
```

`osf_last_error_message()` n'est jamais NULL et reste valide jusqu'au prochain
appel `osf_*` du même thread.

## Catalogue de fonctions

### Manager (lecture)

| Fonction | Rôle |
|---|---|
| `osf_load_file(path, &m)` | Charger entièrement un OSF/OSFZ (décompression transparente incluse) |
| `osf_manager_free(m)` | Libérer le manager + tout ce qui a été emprunté |
| `osf_manager_channel_count(m)` | Nombre de canaux (ordre du metablock) |
| `osf_manager_channel_at(m, i)` | Handle de canal emprunté par position `[0, count)` |
| `osf_manager_channel_by_name(m, name)` | Handle de canal emprunté par nom ; NULL si inconnu |
| `osf_manager_is_compressed(m)` / `osf_manager_compression_format(m)` | Détection OSFZ (`OSF_COMPRESSION_NONE/ZLIB/GZIP`) |
| `osf_manager_creator(m)` / `osf_manager_created_utc(m)` | Métadonnées du fichier (`""` si non définies) |
| `osf_version()` | Chaîne de version de la bibliothèque (statique) |

### Canal (lecture)

| Fonction | Rôle |
|---|---|
| `osf_channel_name(c)` / `osf_channel_index(c)` | Identité |
| `osf_channel_data_type(c)` | `osf_data_type` (`OSF_DT_DOUBLE`, `OSF_DT_STRING`, …) |
| `osf_channel_physical_unit(c)` | Unité (`""` si non définie) |
| `osf_channel_sample_count(c)` | Nombre d'échantillons |
| `osf_channel_read_timestamps(c, out, cap)` | Copier les horodatages (ns) ; les canaux équidistants sont **reconstruits** à partir des segments |
| `osf_channel_read_f64(c, out, cap)` | Copier les valeurs en `double` — convertit tout type numérique/bool ; `0` pour String/Binary/GPS |
| `osf_channel_read_i64(c, out, cap)` | Copier les valeurs en `int64` (même règle de conversion) |
| `osf_channel_read_gps(c, out_lla, cap_samples)` | GPS : 3 doubles (lat, lon, alt) **par échantillon** ; `out_lla` nécessite de la place pour `3 * cap_samples` doubles |
| `osf_channel_string_at(c, i)` | Échantillon string `i`, emprunté, terminé par NUL ; NULL en cas d'erreur de plage/de type |
| `osf_channel_binary_at(c, i, &len)` | Échantillon binaire `i`, emprunté ; les octets peuvent contenir des NUL intégrés |

Tous les lecteurs copy-out écrivent `min(sample_count, cap)` éléments et
renvoient ce nombre — le schéma habituel est « d'abord
`osf_channel_sample_count`, puis dimensionner le tampon ».

### Écriture (aller-retour)

| Fonction | Rôle |
|---|---|
| `osf_write_to_file(m, path)` | Exporter un manager chargé en **OSF5** — utilisable aussi comme convertisseur OSF4 → OSF5 |

Un builder C complet échantillon par échantillon ne fait volontairement pas partie
du périmètre actuel (extension prévue) ; l'ABI couvre la lecture + la
réexportation.

## Exemple C complet

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

Ce schéma d'utilisation précis est compilé et vérifié sur les trois plateformes
de CI (Linux/macOS/Windows) par le test C99 autonome
`tests/capi/test_capi.c`.

## Intégration depuis C# (esquisse P/Invoke)

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

Remarques : convention Cdecl ; copier les chaînes empruntées avec
`Marshal.PtrToStringAnsi` avant que le manager ne soit libéré ;
récupérer le `osf_last_error_message()` local au thread directement après
l'appel ayant échoué, sur **le même** thread.

## Mécanisme d'export

`OSF_C_API` se développe, lors de la compilation de la bibliothèque, en
`__declspec(dllexport)` (Windows) ou
`__attribute__((visibility("default")))` (ELF/Mach-O), et en `dllimport`
lors de la consommation. Les consommateurs n'ont rien à définir —
il suffit d'inclure le header et de lier avec `osf-c`.

> Ce document est placé sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
