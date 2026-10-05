---
title: Build e integrazione
description: Build CMake della libreria C++ OSF — prerequisiti, opzioni, target, integrazione nel proprio progetto, Doxygen, test e CI
sidebar_position: 6
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - CMake
  - Build
  - Doxygen
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Build e integrazione

La guida di build di riferimento, costantemente aggiornata, è il file `BUILD.md`
direttamente nella directory della libreria (incl. FAQ su ambienti proxy,
avvisi del linker e cross-compilation). Questa pagina riassume
gli aspetti più importanti e integra gli scenari di integrazione.

## Prerequisiti

- **CMake ≥ 3.20**
- **Compilatore C++17:** MSVC (VS 2017 15.7+), GCC ≥ 7, Clang ≥ 5,
  AppleClang ≥ 10
- **Internet alla prima configurazione:** GoogleTest e zlib vengono scaricati come
  tarball con pin SHA256 tramite `FetchContent` e vengono memorizzati nella cache
  nell'albero di build; successivamente la build funziona offline.

## Avvio rapido

```bash
git clone https://github.com/optimeas/osf.git
cd osf/implementations/cpp

cmake -B build
cmake --build build
ctest --test-dir build
```

Windows (generatore Visual Studio, multi-config):

```powershell
cmake -B build -G "Visual Studio 17 2022"    # VS 2026: "Visual Studio 18 2026"
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

## Opzioni CMake

| Opzione | Predefinito | Effetto |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Nucleo come shared library anziché statica |
| `OSF_BUILD_TESTS` | `ON` | Compila la suite GoogleTest/ctest |
| `OSF_BUILD_EXAMPLES` | `ON` | Compila i programmi di esempio (`inspect`, `dump`, `write`, `copy`) |
| `OSF_BUILD_DOCS` | `OFF` | Target Doxygen `osf-docs` (richiede Doxygen; se manca, viene saltato in modo pulito) |
| `OSF_BUILD_C_API` | `OFF` | Compila anche la libreria C-ABI `osf-c` + il test C99 |
| `OSF_USE_SYSTEM_ZLIB` | `OFF` | zlib di sistema (`find_package(ZLIB)`) anziché zlib tramite FetchContent |
| `OSF_WARNINGS_AS_ERRORS` | `OFF` | `/WX` o `-Werror`; `ON` in CI, volutamente tollerante in locale |

**C++17 non è un'opzione,** ma la baseline definita in modo fisso della
libreria: un passaggio a C++20+ sarebbe un
upgrade consapevole della libreria, non un'opzione di build.

## Target

| Target | Tipo | Contenuto |
|---|---|---|
| `osf::osf` | libreria statica (o shared) | l'intero nucleo; nome file `libosf.a` / `osf.lib` (target CMake interno `osf_core`, `OUTPUT_NAME osf`) |
| `osf::headers` | INTERFACE | percorsi di include pubblici più le directory vendored di `tl::expected` e `nlohmann/json` (collegate come SYSTEM, in modo che gli avvisi upstream restino silenziosi) |
| `osf-c` | Shared library | la ABI C99 (solo con `OSF_BUILD_C_API=ON`) |
| `osf-docs` | Custom | HTML Doxygen (solo con `OSF_BUILD_DOCS=ON`; non fa parte di `ALL`) |

## Integrazione nel proprio progetto

Oggi la via diretta è `add_subdirectory` su un checkout
(un workflow `cmake --install`/package è previsto come futura estensione):

```cmake
# Variante A: Repo als Submodul/Checkout neben dem eigenen Code
add_subdirectory(extern/osf/implementations/cpp osf-build)
target_link_libraries(meine_app PRIVATE osf::osf)
```

```cmake
# Variante B: FetchContent auf das GitHub-Repo
include(FetchContent)
FetchContent_Declare(osf
    GIT_REPOSITORY https://github.com/optimeas/osf.git
    GIT_TAG        main                       # besser: einen Tag/Commit pinnen
    SOURCE_SUBDIR  implementations/cpp)
set(OSF_BUILD_TESTS OFF)
set(OSF_BUILD_EXAMPLES OFF)
FetchContent_MakeAvailable(osf)
target_link_libraries(meine_app PRIVATE osf::osf)
```

Per i consumatori è sufficiente `#include <osf/osf.h>`; `osf::throwing` e
la C-ABI vengono integrati separatamente, se necessario.

## Dipendenze vendored

In `third_party/` si trovano, identiche byte per byte (pin sul tag,
verificate con SHA256, solo il file LICENSE rinominato con
righe di provenienza):

| Libreria | Licenza | Scopo |
|---|---|---|
| `tl::expected` | CC0-1.0 | monade `Result<T>` |
| `nlohmann/json` 3.12.0 | MIT | metablock OSF5 (JSON), modalità di parsing senza eccezioni |
| `pugixml` | MIT | metablock OSF4 (XML) |

zlib 1.3.2 (per OSFZ) non è vendored, ma arriva tramite FetchContent
(tarball + pin SHA256) oppure dal sistema con `OSF_USE_SYSTEM_ZLIB=ON`.
Non cambiare mai la licenza delle librerie vendored: mantengono la
licenza upstream, indipendentemente dalla licenza MIT del progetto.

## Riferimento API (Doxygen)

```bash
cmake -B build -D OSF_BUILD_DOCS=ON
cmake --build build --target osf-docs        # Windows: zusätzlich --config Debug
# Ergebnis: build/doxygen/html/index.html
```

Gli header pubblici sono dotati ovunque di commenti Doxygen;
il riferimento generato integra queste pagine del manuale con il
livello completo delle firme. Senza Doxygen installato, il
target viene saltato con un messaggio STATUS.

## Test

```bash
ctest --test-dir build                        # Windows: -C Debug
```

- **Unit test** (`tests/unit/`) operano su dati sintetici e
  coprono singolarmente ogni livello.
- **Test di integrazione** (`tests/integration/`) leggono i veri
  file di esempio in `examples/` (dati di campo + i 17 file di
  riferimento generati) e dimostrano tra l'altro round trip esatti al bit.
- **Test C-ABI** (`tests/capi/test_capi.c`) è un programma
  C99 autonomo: dimostra la compatibilità con C e il linking della DLL.

Risultato atteso: **tutti i test verdi** (stato al 2026-06-12: 321 test con
`OSF_BUILD_C_API=ON`), 0 avvisi con `/W4 /permissive-`.

## CI

`.github/workflows/ci.yml` compila e testa l'albero C++ a ogni push
su `implementations/cpp/**` su **ubuntu-latest, macos-14 e
windows-latest** con `OSF_WARNINGS_AS_ERRORS=ON` e
`OSF_BUILD_C_API=ON`. Nota per lo sviluppo locale: la versione di
MSVC in CI può differire da quella locale: una build `/WX` pulita in
locale non sostituisce l'esecuzione in CI; verifica del branch tramite
`gh workflow run ci.yml --ref <branch>`.

## Ostacoli noti

- **Proxy aziendale / intercettazione TLS:** se il download di
  FetchContent fallisce (`CRYPT_E_NO_REVOCATION_CHECK` o simili),
  scaricare una volta manualmente i tarball (ad es. con PowerShell
  `Invoke-WebRequest`, che utilizza l'archivio dei certificati di Windows),
  estrarli e indicare a CMake le copie locali con
  `-D FETCHCONTENT_SOURCE_DIR_GOOGLETEST=…` /
  `-D FETCHCONTENT_SOURCE_DIR_ZLIB=…`.
  In alternativa, indicare un mirror interno (mantenendo gli stessi SHA256)
  — dettagli nelle FAQ di `BUILD.md`.
- **`LNK4098` (MSVC):** mismatch della CRT tra GoogleTest e
  `osf_core`; `tests/CMakeLists.txt` imposta già
  `gtest_force_shared_crt=ON` — non sovrascrivere.
- **ctest trova 0 test:** `gtest_discover_tests` esegue i
  binari di test al momento della configurazione; in ambienti sandbox avviare il
  binario direttamente e controllare il log di configurazione.

> Questo documento è rilasciato con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
