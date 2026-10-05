---
title: Compilation et intégration
description: Build CMake de la bibliothèque C++ OSF — prérequis, options, cibles, intégration dans son propre projet, Doxygen, tests et CI
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

# Compilation et intégration

Le guide de compilation de référence, maintenu en continu, est le fichier `BUILD.md`
situé directement dans le répertoire de la bibliothèque (avec une FAQ sur les environnements proxy,
les avertissements de l'éditeur de liens et la compilation croisée). Cette page résume
l'essentiel et complète avec les scénarios d'intégration.

## Prérequis

- **CMake ≥ 3.20**
- **Compilateur C++17 :** MSVC (VS 2017 15.7+), GCC ≥ 7, Clang ≥ 5,
  AppleClang ≥ 10
- **Internet lors de la première configuration :** GoogleTest et zlib sont fournis sous forme de
  tarballs épinglés par SHA256 via `FetchContent` et mis en cache dans l'arborescence de build ;
  ensuite, le build fonctionne hors ligne.

## Démarrage rapide

```bash
git clone https://github.com/optimeas/osf.git
cd osf/implementations/cpp

cmake -B build
cmake --build build
ctest --test-dir build
```

Windows (générateur Visual Studio, multi-config) :

```powershell
cmake -B build -G "Visual Studio 17 2022"    # VS 2026: "Visual Studio 18 2026"
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

## Options CMake

| Option | Défaut | Effet |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Noyau en bibliothèque partagée au lieu de statique |
| `OSF_BUILD_TESTS` | `ON` | Compiler la suite GoogleTest/ctest |
| `OSF_BUILD_EXAMPLES` | `ON` | Compiler les programmes d'exemple (`inspect`, `dump`, `write`, `copy`) |
| `OSF_BUILD_DOCS` | `OFF` | Cible Doxygen `osf-docs` (nécessite Doxygen ; s'il est absent, la cible est ignorée proprement) |
| `OSF_BUILD_C_API` | `OFF` | Compiler aussi la bibliothèque ABI C `osf-c` + le test C99 |
| `OSF_USE_SYSTEM_ZLIB` | `OFF` | zlib système (`find_package(ZLIB)`) au lieu de zlib via FetchContent |
| `OSF_WARNINGS_AS_ERRORS` | `OFF` | `/WX` ou `-Werror` ; `ON` en CI, volontairement indulgent en local |

**C++17 n'est pas une option,** mais la base de référence définie de façon
fixe pour la bibliothèque — passer à C++20+ serait une mise à niveau délibérée
de la bibliothèque, et non un interrupteur de build.

## Cibles

| Cible | Type | Contenu |
|---|---|---|
| `osf::osf` | bibliothèque statique (ou partagée) | tout le noyau ; nom de fichier `libosf.a` / `osf.lib` (cible CMake interne `osf_core`, `OUTPUT_NAME osf`) |
| `osf::headers` | INTERFACE | chemins d'inclusion publics plus les répertoires intégrés de `tl::expected` et `nlohmann/json` (attachés en SYSTEM afin que les avertissements amont restent silencieux) |
| `osf-c` | bibliothèque partagée | l'ABI C99 (uniquement avec `OSF_BUILD_C_API=ON`) |
| `osf-docs` | Custom | HTML Doxygen (uniquement avec `OSF_BUILD_DOCS=ON` ; ne fait pas partie de `ALL`) |

## Intégration dans son propre projet

La voie directe est aujourd'hui `add_subdirectory` sur un checkout
(un workflow `cmake --install`/package est prévu comme extension future) :

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

Pour les consommateurs, `#include <osf/osf.h>` suffit ; `osf::throwing` et
l'ABI C sont intégrés séparément en cas de besoin.

## Dépendances intégrées

Sous `third_party/` se trouvent, de manière identique octet pour octet (épinglées par tag,
vérifiées par SHA256, seul le fichier LICENSE a été renommé avec des
lignes de provenance) :

| Bibliothèque | Licence | Usage |
|---|---|---|
| `tl::expected` | CC0-1.0 | Monade `Result<T>` |
| `nlohmann/json` 3.12.0 | MIT | Metablock OSF5 (JSON), mode d'analyse sans exceptions |
| `pugixml` | MIT | Metablock OSF4 (XML) |

zlib 1.3.2 (pour OSFZ) n'est pas intégré, mais fourni via FetchContent
(tarball + épinglage SHA256) ou par le système avec `OSF_USE_SYSTEM_ZLIB=ON`.
Ne jamais relicencier les licences intégrées — elles conservent leur
licence amont, indépendamment de la licence MIT du projet.

## Référence de l'API (Doxygen)

```bash
cmake -B build -D OSF_BUILD_DOCS=ON
cmake --build build --target osf-docs        # Windows: zusätzlich --config Debug
# Ergebnis: build/doxygen/html/index.html
```

Les headers publics sont dotés de commentaires Doxygen sur toute leur étendue ;
la référence générée complète ces pages du manuel avec le niveau complet
des signatures. Sans Doxygen installé, la cible est ignorée avec
un message STATUS.

## Tests

```bash
ctest --test-dir build                        # Windows: -C Debug
```

- Les **tests unitaires** (`tests/unit/`) travaillent sur des données synthétiques et
  couvrent chaque couche individuellement.
- Les **tests d'intégration** (`tests/integration/`) lisent les véritables
  fichiers d'exemple sous `examples/` (données de terrain + les 17 fichiers
  de référence générés) et prouvent entre autres des allers-retours exacts au bit près.
- Le **test ABI C** (`tests/capi/test_capi.c`) est un programme
  C99 autonome — il prouve la compatibilité C et le linkage DLL.

Attendu : **tous les tests au vert** (état au 2026-06-12 : 321 tests avec
`OSF_BUILD_C_API=ON`), 0 avertissement sous `/W4 /permissive-`.

## CI

`.github/workflows/ci.yml` compile et teste l'arborescence C++ à chaque push
sur `implementations/cpp/**` sur **ubuntu-latest, macos-14 et
windows-latest** avec `OSF_WARNINGS_AS_ERRORS=ON` et
`OSF_BUILD_C_API=ON`. Remarque pour le développement local : la
version MSVC de la CI peut différer de la version locale — un build
`/WX` propre en local ne remplace pas l'exécution de la CI ; vérification d'une branche via
`gh workflow run ci.yml --ref <branch>`.

## Pièges connus

- **Proxy d'entreprise / interception TLS :** si le téléchargement
  FetchContent échoue (`CRYPT_E_NO_REVOCATION_CHECK` ou similaire),
  télécharger une fois manuellement les tarballs (p. ex. avec PowerShell
  `Invoke-WebRequest`, qui utilise le magasin de certificats Windows),
  les extraire et faire pointer CMake vers les copies locales avec
  `-D FETCHCONTENT_SOURCE_DIR_GOOGLETEST=…` /
  `-D FETCHCONTENT_SOURCE_DIR_ZLIB=…`.
  Sinon, indiquer un miroir interne (en conservant les mêmes SHA256)
  — détails dans la FAQ de `BUILD.md`.
- **`LNK4098` (MSVC) :** incohérence de CRT entre GoogleTest et
  `osf_core` ; `tests/CMakeLists.txt` définit déjà
  `gtest_force_shared_crt=ON` — ne pas l'écraser.
- **ctest trouve 0 test :** `gtest_discover_tests` exécute les
  binaires de test au moment de la configuration ; dans les environnements
  sandboxés, lancer directement le binaire et vérifier le journal de configuration.

> Ce document est placé sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
