---
title: Compilación e integración
description: Compilación con CMake de la biblioteca C++ de OSF — requisitos, opciones, destinos, integración en el propio proyecto, Doxygen, pruebas y CI
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
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Compilación e integración

Las instrucciones de compilación de referencia, mantenidas de forma
continua, son el `BUILD.md` situado directamente en el directorio de la
biblioteca (incluidas las preguntas frecuentes sobre entornos con proxy,
advertencias del enlazador y compilación cruzada). Esta página resume lo
esencial y añade los escenarios de integración.

## Requisitos

- **CMake ≥ 3.20**
- **Compilador de C++17:** MSVC (VS 2017 15.7+), GCC ≥ 7, Clang ≥ 5,
  AppleClang ≥ 10
- **Internet en la primera configuración:** GoogleTest y zlib se obtienen
  como tarballs fijados por SHA256 mediante `FetchContent` y se almacenan en
  caché en el árbol de compilación; después, la compilación funciona sin
  conexión.

## Inicio rápido

```bash
git clone https://github.com/optimeas/osf.git
cd osf/implementations/cpp

cmake -B build
cmake --build build
ctest --test-dir build
```

Windows (generador de Visual Studio, multiconfiguración):

```powershell
cmake -B build -G "Visual Studio 17 2022"    # VS 2026: "Visual Studio 18 2026"
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

## Opciones de CMake

| Opción | Valor predeterminado | Efecto |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Núcleo como biblioteca compartida en lugar de estática |
| `OSF_BUILD_TESTS` | `ON` | Compilar la suite GoogleTest/ctest |
| `OSF_BUILD_EXAMPLES` | `ON` | Compilar los programas de ejemplo (`inspect`, `dump`, `write`, `copy`) |
| `OSF_BUILD_DOCS` | `OFF` | Destino de Doxygen `osf-docs` (requiere Doxygen; si falta, se omite sin errores) |
| `OSF_BUILD_C_API` | `OFF` | Compilar también la biblioteca C-ABI `osf-c` + prueba en C99 |
| `OSF_USE_SYSTEM_ZLIB` | `OFF` | zlib del sistema (`find_package(ZLIB)`) en lugar de la zlib de FetchContent |
| `OSF_WARNINGS_AS_ERRORS` | `OFF` | `/WX` o `-Werror`; `ON` en la CI, deliberadamente tolerante en local |

**C++17 no es una opción,** sino la base fijada de la biblioteca: el paso a
C++20 o superior sería una actualización deliberada de la biblioteca, no un
interruptor de compilación.

## Destinos (targets)

| Destino | Tipo | Contenido |
|---|---|---|
| `osf::osf` | Biblioteca estática (o compartida) | Todo el núcleo; nombre de archivo `libosf.a` / `osf.lib` (destino interno de CMake `osf_core`, `OUTPUT_NAME osf`) |
| `osf::headers` | INTERFACE | Rutas de inclusión públicas más los directorios incluidos de `tl::expected` y `nlohmann/json` (asociados como SYSTEM para que las advertencias del upstream permanezcan en silencio) |
| `osf-c` | Biblioteca compartida | La ABI de C99 (solo con `OSF_BUILD_C_API=ON`) |
| `osf-docs` | Personalizado | HTML de Doxygen (solo con `OSF_BUILD_DOCS=ON`; no forma parte de `ALL`) |

## Integración en el propio proyecto

El camino directo actual es `add_subdirectory` sobre una copia del
repositorio (un flujo de trabajo con `cmake --install` y paquetes está
previsto como ampliación futura):

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

Para los consumidores basta con `#include <osf/osf.h>`; `osf::throwing` y
la C-ABI se integran por separado cuando se necesitan.

## Dependencias incluidas en el repositorio (vendored)

En `third_party/` se encuentran, idénticas byte a byte (fijadas por
etiqueta, verificadas con SHA256, solo se ha renombrado el archivo LICENSE
con líneas de procedencia):

| Biblioteca | Licencia | Finalidad |
|---|---|---|
| `tl::expected` | CC0-1.0 | Mónada `Result<T>` |
| `nlohmann/json` 3.12.0 | MIT | Metabloque OSF5 (JSON), modo de análisis sin excepciones |
| `pugixml` | MIT | Metabloque OSF4 (XML) |

zlib 1.3.2 (para OSFZ) no se incluye en el repositorio, sino que se obtiene
mediante FetchContent (tarball + fijación SHA256) o del sistema con
`OSF_USE_SYSTEM_ZLIB=ON`. Las licencias de los componentes incluidos nunca
deben cambiarse: conservan su licencia original, con independencia de la
licencia MIT del proyecto.

## Referencia de API (Doxygen)

```bash
cmake -B build -D OSF_BUILD_DOCS=ON
cmake --build build --target osf-docs        # Windows: zusätzlich --config Debug
# Ergebnis: build/doxygen/html/index.html
```

Las cabeceras públicas están comentadas con Doxygen de forma consistente;
la referencia generada complementa estas páginas del manual con el nivel
completo de firmas. Si Doxygen no está instalado, el destino se omite con un
mensaje de STATUS.

## Pruebas

```bash
ctest --test-dir build                        # Windows: -C Debug
```

- Las **pruebas unitarias** (`tests/unit/`) trabajan con datos sintéticos y
  cubren cada capa por separado.
- Las **pruebas de integración** (`tests/integration/`) leen los archivos de
  ejemplo reales de `examples/` (datos de campo + los 17 archivos de
  referencia generados) y demuestran, entre otras cosas, ciclos de ida y
  vuelta exactos bit a bit.
- La **prueba de la C-ABI** (`tests/capi/test_capi.c`) es un programa C99
  independiente; demuestra la compatibilidad con C y el enlazado de DLL.

Resultado esperado: **todas las pruebas en verde** (a 2026-06-12: 321
pruebas con `OSF_BUILD_C_API=ON`), 0 advertencias con `/W4 /permissive-`.

## CI

`.github/workflows/ci.yml` compila y prueba el árbol de C++ en cada push a
`implementations/cpp/**` en **ubuntu-latest, macos-14 y windows-latest** con
`OSF_WARNINGS_AS_ERRORS=ON` y `OSF_BUILD_C_API=ON`. Nota para el desarrollo
local: la versión de MSVC de la CI puede diferir de la local; una
compilación `/WX` limpia en local no sustituye la ejecución de la CI. La
verificación de una rama se realiza con `gh workflow run ci.yml --ref <branch>`.

## Escollos conocidos

- **Proxy corporativo / interceptación TLS:** si falla la descarga de
  FetchContent (`CRYPT_E_NO_REVOCATION_CHECK` o similar), descargue los
  tarballs una vez manualmente (p. ej., con PowerShell `Invoke-WebRequest`,
  que utiliza el almacén de certificados de Windows), descomprímalos y
  apunte CMake a las copias locales con
  `-D FETCHCONTENT_SOURCE_DIR_GOOGLETEST=…` /
  `-D FETCHCONTENT_SOURCE_DIR_ZLIB=…`. Como alternativa, registre un espejo
  interno (conservando el mismo SHA256); detalles en las preguntas
  frecuentes de `BUILD.md`.
- **`LNK4098` (MSVC):** discrepancia de CRT entre GoogleTest y `osf_core`;
  `tests/CMakeLists.txt` ya establece `gtest_force_shared_crt=ON`: no lo
  sobrescriba.
- **ctest encuentra 0 pruebas:** `gtest_discover_tests` ejecuta los
  binarios de prueba en el momento de la configuración; en entornos aislados
  (sandbox), inicie el binario directamente y revise el registro de
  configuración.

> Este documento se distribuye bajo la licencia [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
