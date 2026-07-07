---
title: C ABI (osf-c)
description: The pure C99 interface of the OSF C++ library — ownership rules, function catalogue and binding from C, C# and OCX
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

# C ABI — the `osf-c` library

`osf-c` is a separate shared library (DLL / `.so` / `.dylib`) with a
**pure C99 interface** over the C++ core — intended for consumers that
do not speak C++: C programs, C#/P-Invoke, ActiveX/OCX and future
language bindings.

```bash
cmake -B build -D OSF_BUILD_C_API=ON
cmake --build build
```

The only header is `osf/capi.h` — it depends only on `<stdint.h>` /
`<stddef.h>` and is `extern "C"`-guarded for C++ consumers. **No C++
exception ever crosses the ABI boundary** (every entry point is
try/catch-wrapped).

## Ownership and lifetime rules

Three rules suffice for the whole API:

1. **`osf_manager` is heap-owned** — free it with `osf_manager_free()`
   (NULL is a no-op).
2. **`osf_channel` handles and all `const char*` returned by getters are
   borrowed** — valid only until the owning manager's
   `osf_manager_free()`. Copy if the value must outlive the handle.
3. **Sample/timestamp readers copy** into a caller-provided buffer
   (copy-out) and return the number written.

## Error handling

Fallible calls return `osf_status` (`OSF_OK == 0`); the codes mirror
`osf::Error::Code` (`OSF_ERR_IO`, `OSF_ERR_INVALID_METABLOCK`,
`OSF_ERR_REMOVED_IN_SPEC`, …, append-only). On an error the detail
message is available **thread-locally**:

```c
osf_manager* m = NULL;
if (osf_load_file("measurement.osf", &m) != OSF_OK) {
    fprintf(stderr, "load: %s\n", osf_last_error_message());
    return 1;
}
```

`osf_last_error_message()` is never NULL and valid until the next
`osf_*` call on the same thread.

## Function catalogue

### Manager (reading)

| Function | Purpose |
|---|---|
| `osf_load_file(path, &m)` | load OSF/OSFZ completely (transparent decompression included) |
| `osf_manager_free(m)` | free the manager + everything borrowed |
| `osf_manager_channel_count(m)` | channel count (metablock order) |
| `osf_manager_channel_at(m, i)` | borrowed channel handle by position `[0, count)` |
| `osf_manager_channel_by_name(m, name)` | borrowed channel handle by name; NULL if unknown |
| `osf_manager_is_compressed(m)` / `osf_manager_compression_format(m)` | OSFZ detection (`OSF_COMPRESSION_NONE/ZLIB/GZIP`) |
| `osf_manager_creator(m)` / `osf_manager_created_utc(m)` | file metadata (`""` when unset) |
| `osf_version()` | library version string (static) |

### Channel (reading)

| Function | Purpose |
|---|---|
| `osf_channel_name(c)` / `osf_channel_index(c)` | identity |
| `osf_channel_data_type(c)` | `osf_data_type` (`OSF_DT_DOUBLE`, `OSF_DT_STRING`, …) |
| `osf_channel_physical_unit(c)` | unit (`""` when unset) |
| `osf_channel_sample_count(c)` | sample count |
| `osf_channel_read_timestamps(c, out, cap)` | copy timestamps (ns); equidistant ones are **reconstructed** from the segments |
| `osf_channel_read_f64(c, out, cap)` | copy values as `double` — converts any numeric/bool type; `0` for string/binary/GPS |
| `osf_channel_read_i64(c, out, cap)` | copy values as `int64` (same conversion rule) |
| `osf_channel_read_gps(c, out_lla, cap_samples)` | GPS: 3 doubles (lat, lon, alt) **per sample**; `out_lla` needs room for `3 * cap_samples` doubles |
| `osf_channel_string_at(c, i)` | string sample `i`, borrowed, NUL-terminated; NULL on range/type error |
| `osf_channel_binary_at(c, i, &len)` | binary sample `i`, borrowed; bytes may contain embedded NULs |

All copy-out readers write `min(sample_count, cap)` elements and return
that count — the usual pattern is "first `osf_channel_sample_count`,
then size the buffer".

### Writing (round-trip)

| Function | Purpose |
|---|---|
| `osf_write_to_file(m, path)` | export the loaded manager as **OSF5** — also usable as an OSF4 → OSF5 converter |

A full sample-by-sample C builder is deliberately not part of the
current scope (a planned extension); the ABI covers reading + re-export.

## Complete C example

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

This exact usage pattern is built and verified on all three CI platforms
(Linux/macOS/Windows) by the standalone C99 test
`tests/capi/test_capi.c`.

## Binding from C# (P/Invoke sketch)

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
    // … further signatures analogously …
}
```

Notes: Cdecl convention; copy borrowed strings with
`Marshal.PtrToStringAnsi` before the manager is freed; fetch the
thread-local `osf_last_error_message()` right after the failed call on
the **same** thread.

## Export mechanics

`OSF_C_API` expands, when building the library, to
`__declspec(dllexport)` (Windows) or
`__attribute__((visibility("default")))` (ELF/Mach-O), and to
`dllimport` when consuming. Consumers need define nothing — just include
the header and link against `osf-c`.

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
