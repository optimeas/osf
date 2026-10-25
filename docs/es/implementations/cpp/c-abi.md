---
title: C-ABI (osf-c)
description: La interfaz C99 pura de la biblioteca C++ de OSF — reglas de propiedad (ownership), catálogo de funciones y conexión desde C, C# y OCX
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
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# C-ABI — la biblioteca `osf-c`

`osf-c` es una biblioteca compartida independiente (DLL / `.so` / `.dylib`)
con una **interfaz C99 pura** sobre el núcleo de C++, pensada para
consumidores que no hablan C++: programas en C, C#/P-Invoke, ActiveX/OCX y
futuros enlaces de lenguajes.

```bash
cmake -B build -D OSF_BUILD_C_API=ON
cmake --build build
```

La única cabecera es `osf/capi.h`: solo depende de `<stdint.h>` /
`<stddef.h>` y está protegida con `extern "C"` para los consumidores de C++.
**Ninguna excepción de C++ cruza nunca el límite de la ABI** (cada punto de
entrada está encapsulado en un try/catch).

## Reglas de propiedad (ownership) y de tiempo de vida

Tres reglas bastan para toda la API:

1. **`osf_manager` es propiedad del heap**: debe liberarse con
   `osf_manager_free()` (NULL no hace nada).
2. **Los manejadores `osf_channel` y todos los `const char*` devueltos por
   los getters son prestados**: solo son válidos hasta el
   `osf_manager_free()` del gestor al que pertenecen. Cópielos si el valor
   debe sobrevivir al manejador.
3. **Los lectores de muestras y de marcas de tiempo copian** a un búfer
   proporcionado por quien llama (copy-out) y devuelven el número de
   elementos escritos.

## Gestión de errores

Las llamadas que pueden fallar devuelven `osf_status` (`OSF_OK == 0`); los
códigos reflejan `osf::Error::Code` (`OSF_ERR_IO`,
`OSF_ERR_INVALID_METABLOCK`, `OSF_ERR_REMOVED_IN_SPEC`, …, solo se añaden
nuevos). En caso de error, el mensaje detallado está disponible **local al
hilo**:

```c
osf_manager* m = NULL;
if (osf_load_file("messung.osf", &m) != OSF_OK) {
    fprintf(stderr, "laden: %s\n", osf_last_error_message());
    return 1;
}
```

`osf_last_error_message()` nunca es NULL y es válido hasta la siguiente
llamada `osf_*` del mismo hilo.

## Catálogo de funciones

### Gestor (lectura)

| Función | Finalidad |
|---|---|
| `osf_load_file(path, &m)` | Cargar OSF/OSFZ por completo (descompresión transparente incluida) |
| `osf_manager_free(m)` | Liberar el gestor + todo lo prestado |
| `osf_manager_channel_count(m)` | Número de canales (orden del metabloque) |
| `osf_manager_channel_at(m, i)` | Manejador de canal prestado por posición `[0, count)` |
| `osf_manager_channel_by_name(m, name)` | Manejador de canal prestado por nombre; NULL si se desconoce |
| `osf_manager_is_compressed(m)` / `osf_manager_compression_format(m)` | Detección de OSFZ (`OSF_COMPRESSION_NONE/ZLIB/GZIP`) |
| `osf_manager_creator(m)` / `osf_manager_created_utc(m)` | Metadatos del archivo (`""` si no está establecido) |
| `osf_version()` | Cadena de versión de la biblioteca (estática) |

### Canal (lectura)

| Función | Finalidad |
|---|---|
| `osf_channel_name(c)` / `osf_channel_index(c)` | Identidad |
| `osf_channel_data_type(c)` | `osf_data_type` (`OSF_DT_DOUBLE`, `OSF_DT_STRING`, …) |
| `osf_channel_physical_unit(c)` | Unidad (`""` si no está establecida) |
| `osf_channel_sample_count(c)` | Número de muestras |
| `osf_channel_read_timestamps(c, out, cap)` | Copiar las marcas de tiempo (ns); las equidistantes se **reconstruyen** a partir de los segmentos |
| `osf_channel_read_f64(c, out, cap)` | Copiar los valores como `double`: convierte cualquier tipo numérico/bool; `0` para String/Binary/GPS |
| `osf_channel_read_i64(c, out, cap)` | Copiar los valores como `int64` (misma regla de conversión) |
| `osf_channel_read_gps(c, out_lla, cap_samples)` | GPS: 3 doubles (lat, lon, alt) **por muestra**; `out_lla` necesita espacio para `3 * cap_samples` doubles |
| `osf_channel_string_at(c, i)` | Muestra de cadena `i`, prestada, terminada en NUL; NULL en caso de error de rango o de tipo |
| `osf_channel_binary_at(c, i, &len)` | Muestra binaria `i`, prestada; los bytes pueden contener NUL incrustados |

Todos los lectores copy-out escriben `min(sample_count, cap)` elementos y
devuelven ese número; el patrón habitual es «primero
`osf_channel_sample_count`, después dimensionar el búfer».

### Escritura (ida y vuelta)

| Función | Finalidad |
|---|---|
| `osf_write_to_file(m, path)` | Exportar un gestor cargado como **OSF5**; también utilizable como conversor OSF4 → OSF5 |

Un constructor (builder) en C completo, muestra a muestra, deliberadamente
no forma parte del alcance actual (ampliación prevista); la ABI cubre la
lectura y la reexportación.

## Ejemplo completo en C

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

Este patrón de uso exacto se compila y se verifica en las tres plataformas
de CI (Linux/macOS/Windows) mediante la prueba independiente en C99
`tests/capi/test_capi.c`.

## Conexión desde C# (esbozo de P/Invoke)

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

Indicaciones: convención Cdecl; copie las cadenas prestadas con
`Marshal.PtrToStringAnsi` antes de liberar el gestor; recupere el
`osf_last_error_message()` local al hilo inmediatamente después de la
llamada fallida y en el **mismo** hilo.

## Mecánica de exportación

`OSF_C_API` se expande al compilar la biblioteca a
`__declspec(dllexport)` (Windows) o a
`__attribute__((visibility("default")))` (ELF/Mach-O) y, al consumirla, a
`dllimport`. Los consumidores no necesitan definir nada: basta con incluir
la cabecera y enlazar con `osf-c`.

> Este documento se distribuye bajo la licencia [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
