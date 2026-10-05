---
title: Exemples de fichiers
description: Fichiers d'exemple OSF — fichiers de référence synthétiques et enregistrements réels de terrain pour tester et apprendre
image: "/img/om_social_card.png"
keywords:
  - Format de fichier
  - OSF4
  - OSF5
  - OSF
  - Exemples
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

## Fichiers d'exemple OSF

Le répertoire [`examples/`](https://github.com/optimeas/osf/tree/main/examples)
du dépôt contient deux types de fichiers permettant d'apprendre le format et
de tester un reader pas à pas : des **fichiers de référence générés**
(synthétiques, une caractéristique par fichier) et des **échantillons de
terrain** (enregistrements réels d'appareils de mesure optiMEAS).

## Fichiers de référence générés

Les fichiers sous `examples/generated/` sont produits par
l'[implémentation de référence Delphi](../implementations/delphi.md)
(reproductibles de façon non interactive avec `OSFGeneratorCLI`). Chaque
fichier cible **une** caractéristique, et existe à chaque fois en variante
OSF4 (en-tête XML) et en variante OSF5 (en-tête JSON) :

| Fichier (OSF4 / OSF5) | Illustre |
|---|---|
| `osf4_equidistant.osf` / `osf5_equidistant.osf` | Canal équidistant (fréquence d'échantillonnage fixe) |
| `osf4_scalar_numeric.osf` / `osf5_scalar_numeric.osf` | Canaux scalaires à virgule flottante horodatés |
| `osf4_scalar_int64.osf` / `osf5_scalar_int64.osf` | Canal entier signé sur 64 bits |
| `osf4_scalar_unsigned.osf` / `osf5_scalar_unsigned.osf` | Canaux entiers non signés |
| `osf4_gpslocation.osf` / `osf5_gpslocation.osf` | Canal de position GPS |
| `osf4_timestamped_string.osf` / `osf5_timestamped_string.osf` | Données utiles `string` horodatées |
| `osf4_timestamped_binary.osf` / `osf5_timestamped_binary.osf` | Blobs `binary` horodatés |
| `osf4_mixed.osf` / `osf5_mixed.osf` | Plusieurs types de canaux dans un même fichier |
| — / `osf5_mixed_extended.osf` | Fichier OSF5 mixte étendu |

Au total **17 fichiers** (8 OSF4 + 9 OSF5). Un reader correct doit tous les
traiter sans erreur. Ces fichiers sont les fixtures de lecture communes à
tous les langages ; l'appariement OSF4/OSF5 vérifie en même temps les règles
déterministes selon la version — par exemple la terminaison par un octet nul
des données utiles `string`/`binary` (OSF4 avec, OSF5 sans `0x00` final).

## Échantillons de terrain — enregistrements réels

Données réelles issues d'appareils optiMEAS. Elles couvrent ce que les
fichiers synthétiques ne peuvent pas offrir : un grand nombre de canaux, des
motifs d'horodatage réels et des flux se terminant brusquement. Un reader doit
les lire sans plantage et restituer les données présentes.

| Chemin | Format | Description |
|---|---|---|
| `motorbike.osf` | OSF4 | 81 canaux de télémétrie de moto — vitesses, températures, GPS, état du système |
| `steam_loco.osf` (+ `.csv`) | OSF4 | 123 canaux issus d'un enregistrement de locomotive à vapeur (le `.csv` est un export de référence des mêmes données) |
| `weather_station.osfz` | OSF4, gzip | 28 canaux, OSFZ compressé en gzip — vérifie la décompression transparente à la lecture |
| `Testdata Motorbike/` | OSFZ | Enregistrements de moto sur plusieurs jours dans des sous-dossiers journaliers (`YYYYMMDD/…`) — tests de robustesse et de lecture en masse |

## Ouvrir un fichier

Le plus rapide est d'utiliser le paquet Python
[`osfdata`](../integrations/python.md) :

```python
import osf

mgr = osf.load("examples/steam_loco.osf")
print(f"{len(mgr)} Kanäle")
for ch in mgr.channels:
    print(ch.name, ch.data_type, ch.sample_count)
```

Les fichiers peuvent également être ouverts avec l'implémentation
[Rust](../implementations/rust.md)
(`cargo run --example inspect -- <pfad>`), l'implémentation
[C++](../implementations/cpp.md) ou l'implémentation
[Delphi](../implementations/delphi.md) (`osftool info`). Le chapitre
[Format OSF](../osf_general.md) fournit une introduction au format.

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
