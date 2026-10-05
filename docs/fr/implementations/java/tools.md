---
title: Outils — CLI et visionneuse
description: Les outils Java fournis osf-cli (ligne de commande picocli avec info/channels/dump/convert) et osf-viewer (traceur multicanal JavaFX avec décimation min/max par pixel)
sidebar_position: 5
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - osf-cli
  - osf-viewer
  - picocli
  - JavaFX
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Outils — CLI et visionneuse

Outre la bibliothèque, le réacteur Java fournit deux applications autonomes :
**`osf-cli`** — une ligne de commande scriptable pour inspecter, exporter et
convertir des fichiers OSF — et **`osf-viewer`** — une visionneuse JavaFX qui
trace de nombreux canaux simultanément. Les deux s'appuient exclusivement sur
l'API publique de la bibliothèque (voir [Lecture](./reading.md) et
[Écriture](./writing.md)) et en sont en même temps des exemples complets.

Base : **Java 21**, réacteur Maven. Le build global (`mvn -f
implementations/java/pom.xml package`) produit les deux outils en une seule
passe ; les détails sur le réacteur figurent sous [Build](./building.md).

## osf-cli

`osf-cli` est une application [picocli](https://picocli.info/) comportant
quatre sous-commandes. La classe d'entrée
[`OsfCli`](https://github.com/optimeas/osf/blob/main/implementations/java/osf-cli/src/main/java/com/optimeas/osf/cli/OsfCli.java)
enregistre `info`, `channels`, `dump` et `convert` ; chaque commande dispose de
`-h`/`--help` et `-V`/`--version` (issus de `mixinStandardHelpOptions`).

### Build sous forme de jar exécutable

Le module `osf-cli` est regroupé via le `maven-shade-plugin` en un **jar
exécutable autonome** (classe principale
`com.optimeas.osf.cli.OsfCli`, `finalName` `osf-cli`) :

```bash
mvn -f implementations/java/pom.xml -pl osf-cli -am package
java -jar implementations/java/osf-cli/target/osf-cli.jar --help
```

Le jar shade contient la bibliothèque OSF et picocli ; il s'exécute donc sans
classpath supplémentaire. Par souci de concision, `osf` désigne ci-dessous
`java -jar …/osf-cli.jar`.

### `info` — métadonnées et aperçu des canaux

```bash
osf info messung.osf
```

Charge le fichier (OSF4, OSF5 ou OSFZ de manière transparente) et affiche le
format, les métadonnées du fichier, l'état de compression et une ligne par
canal :

```text
format: OSF5
creator: optiMEAS
created_utc: 2026-01-15T09:30:00Z
compressed: false
channels: 3
  [0] temperature  type=double  mode=equidistant  samples=10000  unit=°C
  [1] status       type=string  mode=variable     samples=42     unit=
  [2] position     type=gps_location  mode=timestamped  samples=500  unit=
```

La ligne `format:` vaut `OSF4` ou `OSF5` ; les métadonnées proviennent
telles quelles du métabloc (`creator`, `created_utc`, `location`, …) ;
pour les entrées compressées, `compressed: true (gzip)` apparaît.

### `channels` — tableau des canaux

```bash
osf channels messung.osf --sort NAME
```

Affiche un tableau aligné (colonnes *index, name, datatype,
mode, samples, unit*). `--sort` accepte `INDEX` (par défaut) ou
`NAME` :

```text
index   name                                      datatype      mode          samples   unit
------------------------------------------------------------------------------------------
0       temperature                               double        equidistant   10000     °C
2       position                                  gps_location  timestamped   500
1       status                                    string        variable      42
```

### `dump` — données de canaux vers CSV

`dump` écrit les valeurs des canaux au format CSV — par défaut **tous les
canaux traçables** (numériques + `bool` ; `string`/`binary`/`gps` sont
ignorés).

| Option | Effet |
|---|---|
| `--channel <name\|index>` | Sélectionner un canal par nom **ou** par index entier ; répétable. Sans indication : tous les canaux traçables |
| `--format <csv\|unified-csv>` | `csv` (par défaut) : un bloc par canal ; `unified-csv` : un tableau large |
| `--timestamp-format <…>` | `DATETIME` (par défaut), `SECONDS`, `ISO8601`, `NANOSECONDS` |
| `--out <datei>` | Sortie dans un fichier au lieu de stdout |

Formats d'horodatage : `DATETIME` = `uuuu-MM-dd HH:mm:ss.SSS` (UTC, ms),
`SECONDS` = secondes décimales avec 9 décimales, `ISO8601` =
`uuuu-MM-dd'T'HH:mm:ss'Z'`, `NANOSECONDS` = entier brut en nanosecondes.
Les valeurs `double` entières sont rendues sans point décimal
(`1` au lieu de `1.0`).

**CSV par canal** (par défaut) — chaque bloc avec un en-tête `# channel:` et
des lignes `timestamp,value` :

```bash
osf dump messung.osf --channel temperature --timestamp-format SECONDS
```
```text
# channel: temperature
timestamp,value
0.000000000,21.5
0.001000000,21.6
```

**CSV unifié** — un tableau large avec une ligne par horodatage distinct ;
cellules vides lorsqu'un canal n'a pas d'échantillon à cet instant :

```bash
osf dump messung.osf --format unified-csv --channel 0 --channel 3 --out werte.csv
```
```text
timestamp,temperature,humidity
1970-01-01 00:00:00.000,21.5,48
1970-01-01 00:00:00.001,21.6,
```

Les noms de canaux contenant une virgule, des guillemets ou un saut de ligne
sont placés entre guillemets conformément au format CSV.

### `convert` — vers OSF5 (éventuellement compressé)

`convert` lit une entrée quelconque (OSF4/OSF5, éventuellement compressée) et
l'écrit en **OSF5** :

```bash
osf convert alt-osf4.osf neu.osf                 # OSF4 → OSF5
osf convert messung.osf messung.osfz --compress  # OSF5 → gzip (OSFZ)
```

| Option | Effet |
|---|---|
| `--compress` | Empaqueter la sortie en gzip (produit un fichier OSFZ) |
| `--writer <BLOCK\|STREAMING>` | Backend de writer ; `BLOCK` (par défaut) met en mémoire tampon et écrit en une seule passe, `STREAMING` rejoue échantillon par échantillon via un `FileChannel` |

`STREAMING` ne prend pas en charge `--compress` ; avec cette combinaison, la
commande se rabat sur `BLOCK` en affichant un message. En cas de succès,
`convert` indique `wrote <datei> (N channels)`. La commande est ainsi aussi
le convertisseur OSF4→OSF5 le plus simple.

Code source complet :
[`osf-cli/src/main/java/com/optimeas/osf/cli/`](https://github.com/optimeas/osf/tree/main/implementations/java/osf-cli/src/main/java/com/optimeas/osf/cli).

## osf-viewer

`osf-viewer` est une application **JavaFX** qui représente simultanément de
nombreux canaux d'un fichier OSF. L'idée centrale est une **décimation
min/max par pixel** : quel que soit le nombre de millions d'échantillons qui
tombent sur une colonne d'écran, aucune valeur aberrante n'est jamais
perdue.

### Démarrage

La visionneuse se lance le plus simplement via le plug-in Maven JavaFX
(classe principale `com.optimeas.osf.viewer.ViewerApp`) :

```bash
mvn -pl osf-viewer -f implementations/java/pom.xml javafx:run
```

Il est possible d'ouvrir directement un fichier au démarrage — le premier
argument de démarrage est interprété comme un chemin :

```bash
mvn -pl osf-viewer -f implementations/java/pom.xml javafx:run \
    -Djavafx.args="messung.osf"
```

Une fenêtre « OSF Viewer » de 1000×700 s'ouvre, avec barre d'outils, liste
des canaux, zone de tracé et barre d'état.

### Interface utilisateur

- **Barre d'outils** — *Open…* ouvre une boîte de dialogue de fichier (filtre
  `*.osf`, `*.osfz`) ; *Zoom Reset* ramène la plage visible à l'étendue
  temporelle complète de tous les canaux traçables.
- **Liste des canaux** (à gauche) — un tableau avec les colonnes *Plot, Name,
  DataType, Mode, Samples, Unit*. La case à cocher Plot affiche un canal dans
  le tracé ; pour les canaux non traçables (String, Binary, GPS), elle est
  désactivée et porte l'infobulle « not plotted in v1 ».
- **Zone de tracé** (au centre) — l'affichage proprement dit des courbes ;
  elle occupe l'espace restant et se redessine à chaque changement de taille.
- **Barre d'état** (en bas) — état du chargement et informations de lecture
  du curseur.

Le chargement s'effectue toujours **en arrière-plan** (`Task` JavaFX), afin que
l'interface reste réactive pendant la lecture de gros fichiers.

### Décimation — min/max par pixel

Pour chaque colonne de pixels, le
[`Decimator`](https://github.com/optimeas/osf/blob/main/implementations/java/osf-viewer/src/main/java/com/optimeas/osf/viewer/Decimator.java)
détermine le **minimum ET le maximum** de tous les échantillons tombant dans
cette fenêtre temporelle et trace un trait vertical de `minY` à `maxY`. Ainsi,
même en cas de compression extrême, chaque pic reste visible — contrairement à
la simple suppression de points intermédiaires. L'association des échantillons
utilise une recherche binaire (`lowerBound`) sur les horodatages croissants ;
elle est donc rapide même avec des millions d'échantillons. Chaque canal
sélectionné met à l'échelle son axe Y de façon autonome (autoscale sur la
plage de valeurs mise en cache) ; les couleurs alternent dans une palette de
six teintes bien distinguables.

### Interaction

- **Déplacement (pan)** — tirer horizontalement avec le bouton de la souris
  enfoncé déplace la fenêtre temporelle.
- **Zoom** — molette de la souris ; défiler vers le haut zoome en avant
  (facteur 0,8), vers le bas zoome en arrière (facteur 1,25), chaque fois
  **autour de la position du curseur**, de sorte que l'instant situé sous le
  curseur reste en place.
- **Informations de lecture du curseur** — lors d'un déplacement de la souris,
  la barre d'état affiche l'instant du curseur et, pour chaque canal
  sélectionné, la valeur de l'échantillon le plus proche.

La transformation de coordonnées (temps↔X, valeur↔Y avec axe Y inversé) est
encapsulée dans `AxisTransform`, la logique de lecture et de dessin dans
`PlotCanvas` ; les deux sont volontairement séparés de la couche de modèle
pure (`ViewerModel`, `Decimator`, `AxisTransform` — sans imports JavaFX) et
donc testables sans environnement JavaFX en cours d'exécution. Les détails de
cette découpe se trouvent sous [Architecture](./architecture.md) et
[Éléments internes](./internals.md).

Code source complet :
[`osf-viewer/src/main/java/com/optimeas/osf/viewer/`](https://github.com/optimeas/osf/tree/main/implementations/java/osf-viewer/src/main/java/com/optimeas/osf/viewer).

## Suite

- [Lecture](./reading.md) et [Écriture](./writing.md) — l'API de bibliothèque
  utilisée par les deux outils.
- [Gestion des erreurs](./error-handling.md) — comment `OsfException` se
  répercute jusque dans la sortie de la CLI.
- [Livre de recettes](./cookbook.md) — recettes courtes à copier.
- [Retour à la présentation Java](../java.md).

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
