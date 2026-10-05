---
title: osftool — Outil en ligne de commande
description: L'outil en ligne de commande osftool pour fusionner, exporter, contrôler et convertir des fichiers OSF
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - osftool
  - CLI
  - Ligne de commande
  - merge
  - export
last_update:
  date: 2026-05-22
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# osftool — Outil en ligne de commande

**osftool** est un outil en ligne de commande basé sur des verbes, destiné au travail avec des fichiers au format Open Streaming Format. Il lit et écrit OSF4 et OSF5, traite de façon transparente les fichiers OSFZ compressés et regroupe dans un seul exécutable les tâches courantes autour des données OSF : fusionner des fichiers sur un intervalle de temps, exporter des canaux vers CSV ou HDF5, contrôler les métadonnées, calculer des statistiques, convertir entre versions du format et vérifier l'intégrité des fichiers.

osftool repose sur la bibliothèque OSF Delphi (`implementations/delphi/src/`). La cible de build principale est Windows 64 bits ; le projet contient en outre des configurations de build pour macOS (Intel et Apple Silicon) et Linux 64 bits. La version actuelle est la **1.1.0**.

## À quoi sert osftool

osftool couvre, depuis un script ou un terminal, les tâches récurrentes d'un flux de travail basé sur OSF — sans IDE et sans programmation :

- **Fusionner des données de terrain.** De nombreux fichiers OSF/OSFZ répartis dans une arborescence de répertoires sont lus et fusionnés en un seul fichier pour un intervalle de temps et une sélection de canaux donnés.
- **Transférer des données vers des programmes d'analyse.** Les canaux sont exportés vers CSV — sous la forme d'un bloc XY par canal ou d'un axe temporel commun avec une colonne par canal — ou, dans la version Windows, vers un fichier HDF5.
- **Contrôler rapidement des fichiers.** Les métadonnées, la plage de temps, les listes de canaux et les statistiques par canal sont disponibles sans ouvrir le fichier dans une application.
- **Migrer entre versions du format.** Les fichiers OSF4 sont réécrits en OSF5 et inversement.
- **Contrôler l'intégrité.** Les fichiers sont parcourus bloc par bloc et vérifiés quant à leur cohérence structurelle.
- **Automatisation.** Chaque commande dispose d'un mode `--json` lisible par machine et d'un jeu uniforme de codes de sortie, de sorte qu'osftool s'intègre proprement dans des scripts et des pipelines CI.

## Installation

### Installateur Windows

Le moyen le plus rapide d'obtenir osftool sous Windows est l'installateur préconçu :

**[Télécharger osftool pour Windows (64 bits)](https://www.optimeas.com/hubfs/Software%20Download/osftool-setup-latest.exe)**

L'installateur met en place `OsfTool.exe` avec l'environnement d'exécution HDF5, ajoute le répertoire du programme au `PATH` et laisse le choix entre une installation pour tous les utilisateurs et une installation réservée à l'utilisateur actuel. Une fois l'opération terminée, `osftool` est disponible dans tout terminal.

### Compiler à partir du code source

osftool fait partie du dépôt OSF et se compile avec Embarcadero Delphi 12 (RAD Studio 23) ou une version plus récente.

```
cd implementations/delphi/tools/osftool
dcc64 -B -Q OsfTool.dpr
```

Cela produit `OsfTool.exe` pour Windows 64 bits. Le fichier de projet `OsfTool.dproj` contient en outre les configurations de build `OSX64`, `OSXARM64` et `Linux64` pour une utilisation depuis l'IDE RAD Studio ; le code source est exempt de conflits de configuration de compilation conditionnelle pour ces cibles.

### Ajouter osftool au PATH

Pour que `osftool` puisse être appelé depuis n'importe quel répertoire, son emplacement est ajouté au chemin de recherche :

```
osftool config install-path
```

Sous Windows, cette commande inscrit le répertoire de l'exécutable dans le `PATH` propre à l'utilisateur (`HKCU\Environment`) — les droits d'administrateur ne sont pas nécessaires. Sous macOS et Linux, la commande affiche l'extrait de shell à ajouter à `~/.zshrc` ou `~/.bashrc`. La modification est annulée avec `osftool config uninstall-path`.

## Utilisation générale

```
osftool <befehl> [optionen] [argumente]
osftool <befehl> --help
```

`osftool` sans argument ou `osftool --help` affiche l'aide globale avec la liste des commandes. Lorsque `--help` est ajouté à une commande, celle-ci affiche son aide détaillée sans exécuter d'action. `osftool --version` (ou `-V`) affiche la version et l'horodatage du build ; avec `--short`, uniquement le numéro de version.

### Commandes

| Commande   | Objet |
|------------|-------|
| `merge`    | Fusionner des fichiers OSF d'un répertoire sur un intervalle de temps |
| `export`   | Exporter des canaux OSF vers CSV ou d'autres formats |
| `info`     | Afficher les métadonnées et la plage de temps d'un fichier |
| `channels` | Lister tous les canaux d'un fichier |
| `stat`     | Calculer des statistiques (minimum, maximum, moyenne, …) par canal |
| `cache`    | Gérer les fichiers de cache sidecar `.json` |
| `config`   | Afficher et modifier les paramètres par défaut |
| `convert`  | Convertir entre OSF4 et OSF5 |
| `verify`   | Contrôler l'intégrité du fichier et la cohérence des blocs |

### Options globales

Chaque commande comprend ces options :

| Option      | Effet |
|-------------|-------|
| `--json`    | Écrit un résultat JSON lisible par machine sur stdout au lieu d'un texte lisible par l'humain |
| `--quiet`   | Supprime les sorties informatives ; les avertissements et les erreurs restent affichés |
| `--verbose` | Écrit des messages de débogage sur stderr |

Les résultats vont sur **stdout**, les messages de journal et les erreurs sur **stderr**, de sorte que les deux puissent être redirigés séparément. En mode `--json`, stdout contient exclusivement le document JSON.

### Codes de sortie

| Code | Signification |
|------|-----------|
| `0`  | Succès |
| `1`  | Arguments invalides |
| `2`  | Fichier ou répertoire introuvable |
| `3`  | Erreur d'E/S |
| `4`  | Erreur de format (fichier OSF invalide ou endommagé) |

## Le cache sidecar

Plusieurs commandes peuvent utiliser un fichier **sidecar** `.json`, placé à côté du fichier OSF correspondant. Le fichier sidecar contient des métadonnées par canal — nombre de valeurs, premier et dernier horodatage, plage de temps globale — que le bloc de métadonnées OSF ne porte pas à lui seul. Lorsqu'un fichier sidecar valide est présent, des commandes comme `info` et `channels` répondent immédiatement, au lieu de parcourir chaque bloc.

Le fichier sidecar est utilisé automatiquement ; avec `--no-cache`, il est ignoré et le fichier source est parcouru directement. La commande `cache` construit, renouvelle, contrôle et supprime des fichiers sidecar en grand nombre.

## Référence des commandes

### merge

Parcourt récursivement un répertoire à la recherche de fichiers `.osf` et `.osfz`, sélectionne les fichiers qui chevauchent un intervalle de temps et fusionne les canaux choisis en un seul fichier de sortie.

```
osftool merge <rootdir> <outputfile> [kanal ...] [optionen]
```

| Argument     | Description |
|--------------|--------------|
| `rootdir`    | Répertoire racine ; parcouru récursivement à la recherche de `.osf` et `.osfz` |
| `outputfile` | Chemin du fichier de sortie (`.osf`) |
| `kanal`      | Noms de canaux facultatifs ; à omettre pour fusionner tous les canaux |

| Option         | Description |
|----------------|--------------|
| `--start <ts>` | Début de l'intervalle, ISO 8601 (par défaut : `1970-01-01T00:00:00`) |
| `--end <ts>`   | Fin de l'intervalle, ISO 8601 (par défaut : date et heure actuelles) |
| `--osf4`       | Écrire une sortie OSF4 (par défaut : issu de la configuration `output.format`) |
| `--overwrite`  | Écraser les horodatages qui se chevauchent (par défaut : issu de la configuration `output.overlap`) |
| `--no-cache`   | Ne lire ni écrire de fichiers sidecar `.json` |
| `-q`, `--quiet`   | Supprimer l'affichage en direct ; uniquement les erreurs, sur stderr |
| `-v`, `--verbose` | Afficher chaque ligne de journal (sortie classique défilante, pas de barre en direct) |
| `--json`          | Écrire sur stdout un flux d'événements JSON Lines lisible par machine |
| `--log <pfad>`    | Écrire le journal de diagnostic complet (tous les niveaux) dans un fichier |

`--start` et `--end` sont facultatifs et indépendants : chaque borne possède sa propre valeur par défaut, et une option indiquée ne remplace que cette borne. Sans aucune des deux options, `merge` couvre toute la plage disponible.

```
# Alles unter ./feld-daten in eine Datei zusammenführen
osftool merge ./feld-daten zusammen.osf

# Nur zwei Kanäle innerhalb eines Zeitfensters zusammenführen
osftool merge ./feld-daten fenster.osf Sensor/Temperatur Sensor/Druck \
  --start 2026-05-05T10:00:00 --end 2026-05-05T12:00:00
```

Par défaut, `merge` affiche un **indicateur de progression en direct** : un court titre annonce chaque phase — parcours du répertoire, lecture des métadonnées des fichiers, lecture des fichiers, écriture de la sortie — et, en dessous, une unique ligne de barre de progression est redessinée sur place, indiquant le pourcentage, le compteur de fichiers et le nom du fichier en cours de traitement. Les avertissements et les erreurs apparaissent au-dessus de la barre dès qu'ils surviennent ; les messages informatifs de bas niveau par canal restent supprimés.

```
Reading files...
  [████████████████████░░░░░░░░░░░░░░░░░░░░] 50% (173/346) - 20260517_192802.osfz
```

Les options de sortie modifient cet affichage et s'excluent mutuellement : `-v` / `--verbose` affiche à la place le journal défilant complet ; `-q` / `--quiet` n'affiche que les erreurs sur stderr et reste sinon silencieux ; `--json` fournit un flux d'événements JSON Lines pour les pipelines. `--log <pfad>` est orthogonal — il écrit le journal de diagnostic complet dans un fichier dans tous les modes. Lorsque stdout est redirigé vers un tube ou un fichier, osftool remplace automatiquement l'affichage en direct par des lignes de progression simples périodiques.

### export

Exporte les canaux d'un seul fichier OSF/OSFZ vers CSV — ou, dans la version Windows, vers HDF5.

```
osftool export <inputfile> <outputfile> [kanal ...] [optionen]
```

| Argument     | Description |
|--------------|--------------|
| `inputfile`  | Fichier source `.osf` ou `.osfz` |
| `outputfile` | Chemin du fichier de sortie |
| `kanal`      | Noms de canaux facultatifs ; à omettre pour exporter tous les canaux |

| Option                     | Description |
|----------------------------|--------------|
| `--format <fmt>`           | `csv` (par défaut) — un bloc XY par canal ; `unified-csv` — un axe temporel commun avec une colonne par canal ; `hdf5` — un fichier HDF5, un dataset par canal (uniquement version Windows) |
| `--timestamp-format <fmt>` | Format d'horodatage pour `unified-csv` : `datetime` (par défaut), `seconds`, `iso8601`, `nanoseconds` |
| `--start <zeit>`           | N'exporter que les valeurs à partir de cette heure UTC (ISO 8601) |
| `--end <zeit>`             | N'exporter que les valeurs jusqu'à cette heure UTC (ISO 8601) |
| `--decimal-sep <c>`        | Séparateur décimal CSV : `comma` (par défaut) ou `dot` |
| `--encoding <enc>`         | Encodage CSV : `iso-8859-1` (par défaut) ou `utf-8` |
| `--chunk-size <n>`         | HDF5 : valeurs par chunk de dataset (par défaut `8192`) |
| `--deflate-level <n>`      | HDF5 : niveau de compression gzip `0`–`9` (par défaut `4`) |
| `--no-shuffle`             | HDF5 : désactiver le préfiltre shuffle |
| `--namespace-sep <c>`      | HDF5 : caractère séparateur qui décompose un nom de canal en chemin de groupe HDF5 (par défaut `.`) |
| `--hdf5-lib-dir <pfad>`    | HDF5 : répertoire dans lequel `hdf5.dll` est recherché |
| `--exclude-empty`          | Ignorer les canaux sans valeurs |

`--start` et `--end` doivent être indiqués ensemble — une option sans l'autre est refusée. Les valeurs par défaut de `--decimal-sep` et `--encoding` proviennent du fichier de configuration.

Le format `hdf5` n'est disponible que dans la version Windows. Chaque canal devient un dataset 1-D chunké et compressé par deflate, composé d'enregistrements `{int64 timestamp_ns; value}` ; le nom du canal est décomposé en hiérarchie de groupes HDF5 au niveau de `--namespace-sep`, et les métadonnées aux niveaux fichier et canal sont écrites sous forme d'attributs HDF5. L'exportation nécessite la bibliothèque d'exécution HDF5 : `hdf5.dll` doit être accessible via le chemin de recherche du système ou via le répertoire indiqué avec `--hdf5-lib-dir`. Les options `--chunk-size`, `--deflate-level`, `--no-shuffle` et `--namespace-sep` n'agissent que sur `hdf5` ; `--decimal-sep` et `--encoding` n'agissent que sur les formats CSV.

```
osftool export motorbike.osf motorbike.csv --format unified-csv \
  --timestamp-format iso8601 --decimal-sep dot

# HDF5-Export (Windows-Build), stärkere Kompression
osftool export motorbike.osf motorbike.h5 --format hdf5 --deflate-level 6
```

### info

Affiche les métadonnées et la plage de temps globale d'un fichier.

```
osftool info <file> [optionen]
```

| Option       | Description |
|--------------|--------------|
| `--no-cache` | Ne pas utiliser le fichier sidecar `.json` ; parcourir directement le fichier OSF |
| `--json`     | Sortie au format JSON |

Le rapport contient la version du format, le créateur et l'heure de création, le tag, la reason, le commentaire, le nombre de canaux, le premier et le dernier horodatage de données ainsi que la durée qui en résulte.

```
osftool info motorbike.osf
```

### channels

Liste chaque canal du bloc de métadonnées.

```
osftool channels <file> [optionen]
```

| Option               | Description |
|----------------------|--------------|
| `--filter <muster>`  | Filtre à caractères génériques sur les noms de canaux, par ex. `GPS.*` |
| `--no-cache`         | Ne pas utiliser le fichier sidecar `.json` |
| `--json`             | Sortie sous forme de tableau JSON |

Chaque ligne indique l'index du canal, le nom, le type de données, l'unité physique et — si un fichier sidecar valide est présent — le nombre de valeurs ainsi que le premier et le dernier horodatage. Sans fichier sidecar, ces colonnes affichent `?`.

```
osftool channels motorbike.osf --filter "Sensor/*"
```

### stat

Calcule le minimum, le maximum, la moyenne et l'écart type pour chaque canal numérique avec l'algorithme en ligne à une seule passe de Welford.

```
osftool stat <file> [kanal ...] [optionen]
```

| Option           | Description |
|------------------|--------------|
| `--start <zeit>` | Ne prendre en compte que les valeurs à partir de cette heure UTC (ISO 8601) |
| `--end <zeit>`   | Ne prendre en compte que les valeurs jusqu'à cette heure UTC (ISO 8601) |
| `--json`         | Sortie au format JSON |

`--start` et `--end` sont chacun facultatifs de manière indépendante. Les canaux de type string, binaire et gpslocation sont listés, mais signalés comme *non numériques*. Pour les canaux Int64/UInt64, un avertissement signale une possible perte de précision lors de la conversion en `Double` pour le calcul.

```
osftool stat motorbike.osf --start 2026-05-05T10:00:00
```

### cache

Gère les fichiers de cache sidecar `.json` pour chaque fichier `.osf`/`.osfz` situé sous un répertoire.

```
osftool cache <subbefehl> <rootdir> [optionen]
```

| Sous-commande | Description |
|-----------|--------------|
| `build`   | Créer les fichiers sidecar manquants ; ignorer les fichiers disposant déjà d'un fichier sidecar valide |
| `rebuild` | Recréer de force chaque fichier sidecar |
| `clean`   | Supprimer tous les fichiers sidecar situés sous `rootdir` |
| `status`  | Afficher quels fichiers n'ont pas de fichier sidecar valide |

| Option           | Description |
|------------------|--------------|
| `--no-recursive` | Se limiter au répertoire racine (par défaut : inclure les sous-répertoires) |
| `--json`         | Sortie au format JSON |

```
osftool cache build ./feld-daten
osftool cache status ./feld-daten
```

### config

Contrôle et modifie les paramètres persistants que les autres commandes utilisent comme valeurs par défaut, et gère l'entrée `PATH`.

```
osftool config                    Alle aktuellen Einstellungen anzeigen
osftool config set <key> <wert>   Einen Wert setzen
osftool config reset              Alle Einstellungen auf Vorgaben zurücksetzen
osftool config install-path       osftool zum Benutzer-PATH hinzufügen
osftool config uninstall-path     osftool aus dem Benutzer-PATH entfernen
osftool config --json             Einstellungen als JSON anzeigen
```

Les clés disponibles sont décrites sous [Configuration](#konfiguration).

### convert

Convertit un fichier entre OSF4 et OSF5 en le réécrivant via le merger.

```
osftool convert <inputfile> <outputfile> [optionen]
```

| Option   | Description |
|----------|--------------|
| `--osf4` | Écrire la sortie au format OSF4 |
| `--osf5` | Écrire la sortie au format OSF5 |
| `--json` | Sortie au format JSON |

`--osf4` et `--osf5` s'excluent mutuellement. Si aucune des deux options n'est indiquée, la version cible provient de la clé de configuration `output.format`. Il faut noter que le convertisseur écrit toujours des blocs de données avec horodatage absolu, quelle que soit la structure du fichier source.

```
osftool convert legacy.osf modern.osf --osf5
```

### verify

Parcourt un fichier du début à la fin et vérifie son intégrité structurelle.

```
osftool verify <file> [optionen]
```

Les contrôles suivants sont effectués :

1. En-tête magique lisible et version reconnue.
2. Bloc de métadonnées exploitable (XML ou JSON valide).
3. L'index de canal de chaque bloc est présent dans le bloc de métadonnées.
4. Aucun bloc n'indique une longueur dépassant la taille du fichier.
5. Les horodatages augmentent de façon monotone pour chaque canal.
6. Le fichier se termine proprement — le dernier bloc n'est pas tronqué.

| Option     | Description |
|------------|--------------|
| `--strict` | Traiter les avertissements comme des erreurs (influe sur le code de sortie) |
| `--json`   | Sortie au format JSON |

Les problèmes qui compromettent l'intégrité sont signalés comme des erreurs et entraînent le code de sortie `4` ; les anomalies corrigeables (par exemple un dernier bloc tronqué) sont des avertissements. Avec `--strict`, les avertissements entraînent eux aussi le code de sortie `4`.

```
osftool verify motorbike.osf --strict
```

## Configuration {#konfiguration}

osftool enregistre ses paramètres persistants dans un fichier JSON. Les autres commandes y lisent leurs valeurs par défaut, de sorte que la configuration agit comme une politique propre à l'utilisateur.

| Plateforme | Emplacement |
|-----------|-------------|
| Windows   | `%APPDATA%\osftool\config.json` |
| macOS / Linux | `~/.config/osftool/config.json` |

Le fichier est facultatif — s'il est absent, chaque paramètre retombe sur sa valeur par défaut intégrée.

| Clé                  | Par défaut   | Signification |
|----------------------|--------------|-----------|
| `output.format`      | `osf5`       | Format de sortie par défaut pour `merge` et `convert` |
| `output.overlap`     | `skip`       | Stratégie de chevauchement : `skip` ou `overwrite` |
| `export.decimal_sep` | `,`          | Séparateur décimal par défaut pour CSV |
| `export.encoding`    | `iso-8859-1` | Encodage par défaut pour CSV |
| `cache.enabled`      | `true`       | Utiliser les fichiers sidecar `.json` |
| `cache.auto_build`   | `true`       | Construire automatiquement le cache pendant une analyse |

Les valeurs se lisent et s'écrivent avec la commande `config` :

```
osftool config                          # alle Einstellungen anzeigen
osftool config set output.format osf4   # eine Vorgabe ändern
osftool config reset                    # Vorgaben wiederherstellen
```

Une option de ligne de commande ne remplace la valeur par défaut configurée que pour l'appel concerné.

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
