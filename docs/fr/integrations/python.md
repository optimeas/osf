---
title: Intégration Python
description: Bindings Python pour le Open Streaming Format (OSF) — lecture et écriture avec des performances natives via osfdata
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Python
  - osfdata
  - PyO3
  - NumPy
  - pandas
last_update:
  date: 2026-07-26
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Intégration Python

Le paquet **`osfdata`** fournit un raccordement Python complet au Open Streaming Format. Il lit et écrit des fichiers OSF4 et OSF5, reconnaît automatiquement les fichiers OSFZ compressés et s'intègre de façon fluide à l'écosystème scientifique Python autour de NumPy.

Contrairement aux implémentations purement Python, le vrai travail est effectué dans une bibliothèque Rust. Python ne voit et n'utilise que le mince wrapper placé au-dessus. Résultat : des temps de chargement de l'ordre de la milliseconde même avec plusieurs centaines de milliers d'échantillons, une surcharge mémoire très faible, et des données numériques transférées sans copie vers des tableaux NumPy.

## À quoi sert `osfdata`

`osfdata` résout des tâches courantes autour des données OSF en Python :

- **Intégrer des données dans des pipelines d'analyse.** Les fichiers OSF sont chargés, des canaux individuels sont récupérés directement sous forme de tableaux NumPy et transmis à des bibliothèques comme SciPy, scikit-learn ou PyTorch.
- **Fusionner des données de sources différentes.** Les fichiers OSF issus du terrain sont lus, filtrés ou combinés et écrits sous forme de nouveau fichier OSF.
- **Mettre à jour les données existantes.** Les fichiers OSF4 sont lus et réécrits en OSF5 — une migration pratique sans outil séparé.
- **Traiter de façon transparente les fichiers compressés.** Les fichiers OSFZ (zlib ou gzip) sont reconnus et décompressés automatiquement, sans que l'utilisateur ait quoi que ce soit à configurer.

Le paquet s'adresse aux analystes de données, ingénieurs et scientifiques qui souhaitent intégrer des données OSF dans des workflows Python. Il ne s'adresse pas aux développeurs embarqués qui *produisent* des données — pour cela, il existe des implémentations d'écriture distinctes et légères directement sur les appareils.

## Relation avec `python-osf`

`osfdata` est le successeur moderne du paquet existant [`python-osf`](https://github.com/optimeas/python-osf). `python-osf` est une implémentation Python pure qui ne peut lire que l'OSF4 et qui est nettement plus lente sur les gros fichiers. `osfdata` offre en outre :

- une prise en charge complète d'OSF4 et d'OSF5 (lecture *et* écriture),
- des performances nettement supérieures grâce à la fondation Rust,
- une couverture complète de tous les types de données, y compris `binary`, `gpslocation` et les entiers non signés,
- la conformité à la révision actuelle de la spécification (2026-05-04),
- une décompression OSFZ transparente pour les fichiers compressés en zlib comme en gzip.

`python-osf` sera marqué comme obsolète ultérieurement, dès que `osfdata` couvrira en pratique tous les cas d'usage.

## Plateformes prises en charge

`osfdata` est distribué sous forme de paquet binaire précompilé (wheel) pour les plateformes suivantes :

| Plateforme | Architecture | Versions de Python |
|---|---|---|
| Linux | x86_64 | 3.9 – 3.13 |
| Linux | aarch64 (ARM 64 bits) | 3.9 – 3.13 |
| macOS | arm64 (Apple Silicon) | 3.9 – 3.13 |
| Windows | x86_64 | 3.9 – 3.13 |

Sur toutes les plateformes prises en charge, l'installation s'effectue sans compilateur ni autres outils. macOS Intel n'est pas livré sous forme de wheel ; une installation à partir de la distribution source est possible, mais nécessite alors une toolchain Rust locale. Il en va de même pour d'autres plateformes exotiques (FreeBSD, Windows-on-ARM, anciennes distributions Linux sans compatibilité manylinux).

## Installation

`osfdata` peut être installé avec les deux gestionnaires de paquets Python courants.

### Avec pip

```bash
pip install osfdata
```

### Avec uv

```bash
uv pip install osfdata
```

`uv` est un gestionnaire de paquets plus récent et nettement plus rapide, qui combine `pip` et `venv`. Pour ceux qui ne le connaissent pas encore : installation et documentation sous [docs.astral.sh/uv](https://docs.astral.sh/uv).

## Import et premier exemple

Après l'installation, le paquet est importé sous le nom court `osf` (le nom de distribution `osfdata` est utilisé sur PyPI, le nom d'import en est indépendant — un schéma courant en Python, comparable à `scikit-learn` ↔ `import sklearn`).

```python
import osf

mgr = osf.load("messung.osf")
print(f"Datei enthält {len(mgr)} Kanäle")

temp = mgr.channel("Sensor.Temperatur")
samples = temp.samples()      # NumPy-Array, dtype passt zum OSF-Datentyp
zeitstempel = temp.timestamps_ns()
```

L'essentiel est ainsi fait : charger le fichier, accéder à un canal par son nom, obtenir les valeurs sous forme de tableau NumPy.

## Aperçu de l'API

La bibliothèque se compose de quelques éléments publics clairs. Tous les autres détails — par exemple comment un canal comportant plusieurs segments est géré ou comment les données horodatées et équidistantes se distinguent — se déduisent du travail avec les objets énumérés ici.

### Fonctions au niveau du module

| Fonction | Objet |
|---|---|
| `osf.load(path)` | Charge un fichier OSF ou OSFZ et renvoie un `DataManager`. Reconnaît automatiquement le format. |
| `osf.save(manager, path)` | Écrit un `DataManager` sous forme de fichier OSF5. |

### Classe `DataManager`

Représente un fichier OSF chargé avec tous ses canaux et métadonnées.

| Attribut / méthode | Description |
|---|---|
| `len(mgr)` | Nombre de canaux. |
| `mgr.channels` | Liste de tous les canaux (`list[Channel]`). |
| `mgr.channel(name)` | Accéder à un canal par son nom. Renvoie `None` s'il n'existe pas. |
| `mgr.channel_by_index(i)` | Accéder à un canal par son index numérique. |
| `mgr.stats` | Objet `ReaderStats` avec des statistiques sur la lecture. |

### Classe `Channel`

Un canal individuel avec ses métadonnées et ses valeurs. Trois variantes — équidistant, horodaté numérique, horodaté variable (pour les chaînes et les données binaires) — sont accessibles via la même API.

| Attribut / méthode | Description |
|---|---|
| `ch.name` | Nom du canal (souvent hiérarchique, par ex. `"Motor.Drehzahl"`). |
| `ch.index` | Index numérique du canal dans le fichier. |
| `ch.data_type` | Type de données sous forme de chaîne (`"double"`, `"int32"`, `"string"` …). |
| `ch.channel_type` | `"equidistant"`, `"timestamped"` ou `"variable"`. |
| `ch.sample_count` | Nombre d'échantillons. |
| `ch.physical_unit` | Unité physique (si indiquée). |
| `ch.is_empty` | True si le canal ne contient aucune donnée. |
| `ch.samples()` | Valeurs sous forme de tableau NumPy. |
| `ch.timestamps_ns()` | Horodatages en nanosecondes depuis l'époque, sous forme de tableau NumPy `int64`. |
| `ch.segments` | Liste des segments (significative uniquement pour les canaux équidistants). |

### Classe `Segment`

Décrit une section d'un canal équidistant — par exemple lorsqu'un enregistrement est divisé en plusieurs phases par des événements de déclenchement ou des corrections de dérive.

| Attribut | Description |
|---|---|
| `seg.start_timestamp_ns` | Instant de début du segment en nanosecondes depuis l'époque. |
| `seg.sample_rate_hz` | Fréquence d'échantillonnage au sein de ce segment en hertz. |
| `seg.sample_count` | Nombre d'échantillons de ce segment. |

### Classe `ReaderStats`

Informations de diagnostic sur le dernier chargement.

| Attribut | Description |
|---|---|
| `stats.compressed` | True si le fichier était compressé en OSFZ. |
| `stats.compression_format` | `"gzip"`, `"zlib"` ou `None`. |
| `stats.channels_total` | Nombre de canaux dans le fichier. |
| `stats.blocks_total` | Nombre de blocs de données lus. |
| `stats.elapsed_ms` | Temps de chargement en millisecondes. |
| `stats.file_size_bytes` | Taille du fichier sur le disque. |

### Classe `WriterBuilder`

Construit des fichiers OSF5 à partir de définitions de canaux et de données d'échantillons. Le builder accepte les canaux l'un après l'autre ; chaque canal peut recevoir plusieurs segments ou blocs d'échantillons.

```python
b = osf.WriterBuilder().creator("messsystem-v1").tag("vortest")

idx = b.add_channel(
    name="Sensor.Druck",
    data_type="double",
    channel_type="scalar",
    physical_unit="bar",
)

import numpy as np
werte = np.array([1.013, 1.015, 1.014, 1.012], dtype=np.float64)
b.add_equidistant_segment(
    idx,
    start_ns=1_700_000_000_000_000_000,
    sample_rate_hz=1.0,
    values=werte,
)

b.write_to_file("ausgabe.osf")
```

Les méthodes les plus importantes :

| Méthode | Objet |
|---|---|
| `b.creator(s)` / `b.tag(s)` / `b.reason(s)` | Définir les métadonnées du fichier. |
| `b.location(lat, lon, alt)` | Position géographique de l'enregistrement. |
| `b.add_channel(...)` | Définir un nouveau canal, renvoie l'index du canal. |
| `b.add_equidistant_segment(idx, start_ns, sample_rate_hz, values)` | Ajouter un segment équidistant (uniquement `float`/`double`). |
| `b.add_timestamped_samples(idx, ts_ns, values)` | Ajouter des échantillons numériques horodatés. |
| `b.add_string_samples(idx, ts_ns, values)` | Ajouter des échantillons de chaînes avec horodatage. |
| `b.add_binary_samples(idx, ts_ns, values)` | Ajouter des échantillons de données binaires (par ex. images, audio). |
| `b.write_to_file(path)` | Écrire dans un fichier les données rassemblées. |

## Remarques d'utilisation

**Horodatages.** Toutes les indications de temps sont des entiers 64 bits en nanosecondes depuis l'époque Unix (UTC). Cette précision couvre aussi bien les mesures de vibrations à haute fréquence que les données de process lentes, sans exiger des types de données temporels différents. Une couche de conversion facultative vers le type `datetime` de Python est prévue pour une version ultérieure.

**Types de données NumPy.** Le dtype du tableau NumPy renvoyé correspond directement au type de données OSF : `double` → `float64`, `int32` → `int32`, `bool` → `bool`, etc. Aucune conversion implicite n'a lieu — si le canal contient du `int16`, le tableau NumPy est lui aussi en `int16`.

**Gestion de la mémoire.** Les tableaux d'échantillons numériques sont transférés entre Rust et Python sans copie. Cela rend la lecture de grands canaux (plusieurs millions d'échantillons) très rapide, même sur du matériel modeste.

**Gestion des erreurs.** Toutes les fonctions lèvent `osf.OsfError` (une sous-classe de `Exception`) lorsqu'un problème survient — fichier introuvable, format invalide, type de données inconnu. Le comportement face à des blocs de données inconnus ou endommagés suit le principe du best effort : des données sont fournies jusqu'au dernier bloc bien lisible, puis la lecture s'interrompt proprement.

## Exemples d'application

Des notebooks d'exemple détaillés et des scripts suivront dans une section distincte. Thèmes prévus :

- Explorer rapidement un fichier OSF inconnu
- Migration d'OSF4 vers OSF5
- Filtrage et fusion de plusieurs enregistrements
- Passage à pandas pour l'analyse tabulaire
- Intégration dans des datasets PyTorch

## Code source et informations complémentaires

- Le paquet sur PyPI : [pypi.org/project/osfdata](https://pypi.org/project/osfdata/)
- Code source sur GitHub : [github.com/optimeas/osf](https://github.com/optimeas/osf), répertoire `implementations/python/`
- Processus de build et de release : voir [`BUILD.md`](https://github.com/optimeas/osf/blob/main/implementations/python/BUILD.md) dans le dépôt
- Spécification du format : voir le chapitre [Format OSF](../osf_general.md) de cette documentation

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
