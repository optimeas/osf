---
title: Gestion des erreurs
description: Result<T> et osf::Error dans le noyau sans exceptions, le catalogue complet des codes d'erreur et la couche optionnelle osf::throwing
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - Result
  - Error
  - Exceptions
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Gestion des erreurs

Le noyau de l'implémentation C++ est **sans exceptions** : toute
opération susceptible d'échouer renvoie `osf::Result<T>` — un
`tl::expected<T, osf::Error>`. Qui préfère les exceptions superpose la
fine couche optionnelle `osf::throwing`. Les deux styles peuvent
être mélangés.

## `osf::Error` et `osf::Result<T>`

```cpp
struct Error {
    Code        code;     // stabile Kategorie — hierauf verzweigen
    std::string message;  // menschenlesbares Detail — nur zur Anzeige
};

template <typename T>
using Result = tl::expected<T, Error>;
```

Idiome de base :

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

Règles :

- **Se brancher sur `code`, n'afficher que `message`.** Le libellé du
  message ne fait pas partie de l'API et peut changer.
- Tous les retours `Result` sont `[[nodiscard]]` — le compilateur signale
  les erreurs ignorées.
- `errorCategoryName(code)` fournit un identifiant de chaîne stable
  pour les journaux (statique, sans propriété).
- `Result<void>` signale des opérations purement succès/échec
  (`writer.start()`, `writeToFile(...)`, …) : `if (!r) …`.

## Catalogue des codes d'erreur

### Erreurs d'entrée et d'API

| Code | Signification | Source typique |
|---|---|---|
| `InvalidArgument` | Précondition d'API violée : `ChannelDef` invalide, index de canal inconnu sur le writer, `count == 0`, writer dans une mauvaise phase du cycle de vie, fréquence d'échantillonnage non positive | Writer |
| `IoError` | Erreur de fichier/flux : ouverture impossible, erreur de lecture/écriture, erreur fsync | partout |
| `NotFound` | réservé aux API de recherche (les recherches de canaux renvoient `nullptr` à la place) | — |
| `Unknown` | Repli sans catégorie plus spécifique ; code d'un `Error` construit par défaut | — |
| `ParseError` | erreur d'analyse générique lorsqu'aucune catégorie plus spécifique ne convient (rare) | Parseur |

### Magic header

| Code | Signification |
|---|---|
| `InvalidMagicHeader` | La première ligne n'est pas un header OSF bien formé — le plus souvent « pas un fichier OSF » |
| `UnsupportedVersion` | Header analysable, mais l'identifiant n'est pas l'une des quatre graphies acceptées (`OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) |
| `MagicHeaderTooLong` | Aucun saut de ligne dans les 128 octets — certainement pas un fichier OSF |

### Metablock

| Code | Signification |
|---|---|
| `InvalidMetablock` | Erreur de structure : champ obligatoire manquant, nombre non analysable, `sizeOfLengthValue` ≠ 2/4 (corromprait sinon silencieusement chaque lecture de bloc), élément racine incorrect |
| `JsonParseError` | OSF5 : le corps du metablock n'est pas du JSON valide (diagnostic du parseur dans `message`) |
| `XmlParseError` | OSF4 : le corps du metablock n'est pas du XML bien formé (diagnostic + décalage en octets dans `message`) |
| `RemovedInSpec` | Le fichier utilise un type de données **supprimé** par la révision de spécification 2026-05-04 (`pair`, `triple`, `candata`, `gpsdata`). Rejeté strictement — l'ancienne disposition de payload n'est pas reproductible à partir d'un build actuel ; le message indique le remplaçant |

### Flux de blocs

| Code | Signification |
|---|---|
| `UnknownChannelIndex` | Le bloc référence un index de canal sans définition dans le metablock. Sans définition, la largeur du champ de longueur est inconnue → signal de corruption, arrêt strict |
| `InvalidBlock` | Payload structurellement défectueux (longueur incorrecte pour le type de données, bloc équidistant sur un canal string, échantillon dépassant la capacité de bloc du streaming writer, …) |
| `ChannelMixedBlockTypes` | Un canal fournit à la fois des blocs équidistants (`bcStartData`/`bcContinuedData`) et des blocs timestamped — interdit par la spécification |
| `ContinuedDataWithoutStart` | `bcContinuedData` sans `bcStartData` préalable — sans segment ouvert, la suite n'a pas de base de temps |
| `RelStampWithoutAnchor` | `bcContinuedRelStampData` sans horodatage absolu préalable — les deltas n'ont pas d'ancrage |
| `DataTypeMismatch` | Type demandé ≠ type stocké (p. ex. `asDoublesFlat` sur un canal `int32`, `asStrings` sur un canal binaire) |

### Ce qui n'est volontairement **pas** une erreur

| Situation | Comportement |
|---|---|
| Le fichier se termine au milieu d'un bloc (coupure de courant) | Best-effort : tous les blocs complets sont fournis, `stats.blocksTruncated = 1`, l'itération se termine proprement |
| Type de données/type de canal futur inconnu | Le canal est analysé comme `Unsupported`, les blocs sont consommés alignés comme `Skipped`, les autres canaux se chargent normalement |
| Control bytes obsolètes/réservés (anciens fichiers de terrain) | `BlockKind::Skipped` avec `SkipReason`, compteur dans `stats` |
| Champs de canal obsolètes (`scale`, `offset`, `physicalunit1..3`, …) | tolérés et ignorés silencieusement (les fichiers de terrain réels les portent tous) |
| Recherche de canal sans résultat | `nullptr`, pas de `Result` |

## La couche avec exceptions — `osf::throwing`

Header-only, optionnelle (`#include <osf/throwing.h>`), volontairement **absente**
du header umbrella `<osf/osf.h>` et non compilée dans la
bibliothèque. Qui ne l'inclut jamais n'embarque aucune
mécanique d'exceptions.

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

La couche se compose d'exactement trois éléments :

| Élément | Rôle |
|---|---|
| `osf::Exception : std::runtime_error` | porte l'`osf::Error` complet ; se trouve dans `osf`, et non dans `osf::throwing` |
| `osf::throwing::unwrap(Result<T>)` | adaptateur universel : extraire la valeur ou lever une exception. Fonctionne avec **tout** `Result` du noyau, y compris ceux des méthodes de writer — il n'y a donc pas besoin de wrapper avec exceptions par méthode |
| `osf::throwing::load / writeToFile / writeTo` | équivalents avec exceptions des opérations de haut niveau les plus courantes |

## Choix du style en pratique

- **Code de bibliothèque/embarqué, chemins critiques, bases de code avec
  `-fno-exceptions` :** rester sur le noyau `Result`.
- **Code applicatif avec une stratégie d'exceptions existante :** utiliser `throwing`
  à la frontière de l'application ; en interne, tout reste `Result`.
- **Mixte :** `unwrap` ponctuellement là où une erreur ne ferait de toute façon
  que se propager — p. ex. dans un `main` de CLI qui possède un
  `try/catch` au sommet.

## Cas particulier des writers : erreurs persistantes (sticky errors)

Le `StreamingWriter` mémorise la première erreur d'E/S (état « Broken »)
et la renvoie à **chaque** appel suivant, y compris
`close()`. Dans les boucles d'écriture, une seule vérification d'erreur par
itération suffit donc ; la cause n'est pas perdue même si l'évaluation n'a lieu
qu'à la fin.

> Ce document est placé sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
