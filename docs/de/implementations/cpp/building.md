---
title: Bauen & Einbinden
description: CMake-Build der OSF-C++-Bibliothek — Voraussetzungen, Optionen, Targets, Einbindung ins eigene Projekt, Doxygen, Tests und CI
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
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Bauen & Einbinden

Die maßgebliche, laufend gepflegte Bauanleitung ist die `BUILD.md`
direkt im Bibliotheksverzeichnis (inkl. FAQ zu Proxy-Umgebungen,
Linker-Warnungen und Cross-Compilation). Diese Seite fasst das
Wichtigste zusammen und ergänzt die Einbindungs-Szenarien.

## Voraussetzungen

- **CMake ≥ 3.20**
- **C++17-Compiler:** MSVC (VS 2017 15.7+), GCC ≥ 7, Clang ≥ 5,
  AppleClang ≥ 10
- **Internet beim ersten Configure:** GoogleTest und zlib kommen als
  SHA256-gepinnte Tarballs per `FetchContent` und werden im Build-Baum
  gecacht; danach ist der Build offline.

## Schnellstart

```bash
git clone https://github.com/optimeas/osf.git
cd osf/implementations/cpp

cmake -B build
cmake --build build
ctest --test-dir build
```

Windows (Visual-Studio-Generator, Multi-Config):

```powershell
cmake -B build -G "Visual Studio 17 2022"    # VS 2026: "Visual Studio 18 2026"
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

## CMake-Optionen

| Option | Standard | Wirkung |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Kern als Shared Library statt statisch |
| `OSF_BUILD_TESTS` | `ON` | GoogleTest-/ctest-Suite bauen |
| `OSF_BUILD_EXAMPLES` | `ON` | Beispielprogramme (`inspect`, `dump`, `write`, `copy`) bauen |
| `OSF_BUILD_DOCS` | `OFF` | Doxygen-Target `osf-docs` (erfordert Doxygen; fehlt es, wird sauber übersprungen) |
| `OSF_BUILD_C_API` | `OFF` | C-ABI-Bibliothek `osf-c` + C99-Test mitbauen |
| `OSF_USE_SYSTEM_ZLIB` | `OFF` | System-zlib (`find_package(ZLIB)`) statt FetchContent zlib |
| `OSF_WARNINGS_AS_ERRORS` | `OFF` | `/WX` bzw. `-Werror`; in CI `ON`, lokal bewusst nachsichtig |

**C++17 ist keine Option,** sondern die fest definierte Baseline der
Bibliothek — ein Wechsel auf C++20+ wäre ein bewusstes
Library-Upgrade, kein Build-Schalter.

## Targets

| Target | Art | Inhalt |
|---|---|---|
| `osf::osf` | statische (oder shared) Bibliothek | der gesamte Kern; Dateiname `libosf.a` / `osf.lib` (internes CMake-Target `osf_core`, `OUTPUT_NAME osf`) |
| `osf::headers` | INTERFACE | öffentliche Include-Pfade plus die vendorten `tl::expected`- und `nlohmann/json`-Verzeichnisse (SYSTEM-attached, damit Upstream-Warnungen still bleiben) |
| `osf-c` | Shared Library | das C99-ABI (nur mit `OSF_BUILD_C_API=ON`) |
| `osf-docs` | Custom | Doxygen-HTML (nur mit `OSF_BUILD_DOCS=ON`; nicht Teil von `ALL`) |

## In das eigene Projekt einbinden

Der direkte Weg ist heute `add_subdirectory` auf einen Checkout
(ein `cmake --install`-/Package-Workflow ist als künftige Erweiterung
vorgesehen):

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

Für Konsumenten genügt `#include <osf/osf.h>`; `osf::throwing` und
das C-ABI werden bei Bedarf separat eingebunden.

## Vendorte Abhängigkeiten

Unter `third_party/` liegen byte-identisch (Tag-gepinnt,
SHA256-verifiziert, nur die LICENSE-Datei mit Provenance-Zeilen
umbenannt):

| Bibliothek | Lizenz | Zweck |
|---|---|---|
| `tl::expected` | CC0-1.0 | `Result<T>`-Monade |
| `nlohmann/json` 3.12.0 | MIT | OSF5-Metablock (JSON), exception-freier Parse-Modus |
| `pugixml` | MIT | OSF4-Metablock (XML) |

zlib 1.3.2 (für OSFZ) kommt nicht vendort, sondern per FetchContent
(Tarball + SHA256-Pin) oder mit `OSF_USE_SYSTEM_ZLIB=ON` vom System.
Vendorte Lizenzen niemals umlizenzieren — sie behalten ihre
Upstream-Lizenz, unabhängig von der MIT-Lizenz des Projekts.

## API-Referenz (Doxygen)

```bash
cmake -B build -D OSF_BUILD_DOCS=ON
cmake --build build --target osf-docs        # Windows: zusätzlich --config Debug
# Ergebnis: build/doxygen/html/index.html
```

Die öffentlichen Header sind durchgehend mit Doxygen-Kommentaren
versehen; die generierte Referenz ergänzt diese Handbuch-Seiten um die
vollständige Signatur-Ebene. Ohne installiertes Doxygen wird das
Target mit einer STATUS-Meldung übersprungen.

## Tests

```bash
ctest --test-dir build                        # Windows: -C Debug
```

- **Unit-Tests** (`tests/unit/`) arbeiten auf synthetischen Daten und
  decken jede Schicht einzeln ab.
- **Integrations-Tests** (`tests/integration/`) lesen die echten
  Beispieldateien unter `examples/` (Felddaten + die 17 generierten
  Referenzdateien) und beweisen u. a. bitgenaue Round-Trips.
- **C-ABI-Test** (`tests/capi/test_capi.c`) ist ein eigenständiges
  C99-Programm — er beweist C-Kompatibilität und DLL-Linkage.

Erwartung: **alle Tests grün** (Stand 2026-06-12: 321 Tests mit
`OSF_BUILD_C_API=ON`), 0 Warnungen unter `/W4 /permissive-`.

## CI

`.github/workflows/ci.yml` baut und testet den C++-Baum bei jedem Push
auf `implementations/cpp/**` auf **ubuntu-latest, macos-14 und
windows-latest** mit `OSF_WARNINGS_AS_ERRORS=ON` und
`OSF_BUILD_C_API=ON`. Hinweis für lokale Entwicklung: Die
CI-MSVC-Version kann von der lokalen abweichen — ein lokal sauberer
`/WX`-Build ersetzt den CI-Lauf nicht; Branch-Verifikation per
`gh workflow run ci.yml --ref <branch>`.

## Bekannte Stolpersteine

- **Firmen-Proxy / TLS-Interception:** Schlägt der
  FetchContent-Download fehl (`CRYPT_E_NO_REVOCATION_CHECK` o. Ä.),
  die Tarballs einmal manuell laden (z. B. mit PowerShell
  `Invoke-WebRequest`, das den Windows-Zertifikatsspeicher nutzt),
  entpacken und CMake mit
  `-D FETCHCONTENT_SOURCE_DIR_GOOGLETEST=…` /
  `-D FETCHCONTENT_SOURCE_DIR_ZLIB=…` auf die lokalen Kopien zeigen.
  Alternativ einen internen Mirror eintragen (gleiche SHA256 behalten)
  — Details in der FAQ von `BUILD.md`.
- **`LNK4098` (MSVC):** CRT-Mismatch zwischen GoogleTest und
  `osf_core`; `tests/CMakeLists.txt` setzt bereits
  `gtest_force_shared_crt=ON` — nicht überschreiben.
- **ctest findet 0 Tests:** `gtest_discover_tests` führt die
  Test-Binaries zur Configure-Zeit aus; in sandboxed Umgebungen das
  Binary direkt starten und das Configure-Log prüfen.

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
