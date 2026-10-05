---
title: Implémentation Delphi
description: L'implémentation de référence Delphi du Open Streaming Format — bibliothèque, démos et ligne de commande osftool
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Delphi
  - osftool
  - RAD Studio
  - Implémentation de référence
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Implémentation Delphi

L'implémentation Delphi est l'**implémentation de référence** du Open
Streaming Format : chemin complet de lecture et d'écriture pour OSF4 et OSF5,
un ensemble de démos et la ligne de commande `osftool`. Elle produit en même
temps le jeu de [fichiers de référence](../examples/osf_file_examples.md) par
rapport auquel les autres implémentations sont vérifiées. Développée avec RAD
Studio 23.0 (Delphi 12).

## Bibliothèque

Les unités de la bibliothèque se trouvent sous `implementations/delphi/src/`.
Les composants publics les plus importants :

| Unité | Contenu |
|---|---|
| `OSF.Types` | Types de données et types de canaux (`TOSFDataType`, `TOSFVersion`, `TOSFGpsLocation`) et fonctions utilitaires |
| `OSF.Filer` | `TOSFFile` — reader/writer en streaming pour OSF4 et OSF5 ; décompression OSFZ transparente ; analyse XML OSF4 via OmniXML (sans dépendance à MSXML) |
| `OSF.Data.Manager` | `TOSFDataManager` — lecture de haut niveau, remplit des canaux typés ; accès par nom |
| `OSF.Data.Channels` | canaux typés en mémoire, y compris segments équidistants (chaque `bcStartData` ouvre un segment) |
| `OSF.Log` | répartiteur de journalisation/progression à l'échelle du processus (`Logger: TOSFLog`) avec modèle à écouteurs (DECISIONS §22) |
| `OSF.Export.CSV` / `OSF.Export.CSV.Unified` | export CSV — par canal (XY) ou avec axe temporel commun |
| `OSF.Export.HDF5` | export HDF5 (Windows ; un dataset composé compressé par canal) |
| `OSF.Merger` | `TOSFMerger` — fusionner plusieurs fichiers OSF d'un intervalle de temps en un seul fichier de sortie |
| `OSF.Meta.Cache` | cache sidecar `.json` par fichier OSF (plages de temps, nombre d'échantillons) pour des métadonnées rapides |

Un programme sans formulaire `OSFCompileCheck.dpr`, à la racine du répertoire
de l'implémentation, couvre via `uses` chaque unité de la bibliothèque et
donne après les refactorings un signal de compilation propre.

### Architecture de journalisation

Chaque unité de la bibliothèque écrit les événements de journal et de
progression dans le `Logger: TOSFLog` global. Les applications enregistrent
un `TLoggerListener` (avec son propre `MinLevel` et ses callbacks
d'événements) ; le singleton assure la distribution. Détails dans
[DECISIONS §22](https://github.com/optimeas/osf/blob/main/DECISIONS.md).

## Démos

Sous `implementations/delphi/demos/` :

| Démo | Objet |
|---|---|
| `osfviewer/` | charger un fichier OSF, lister les canaux, afficher un canal sous forme de TeeChart (uniquement dans l'IDE) |
| `osfgenerator/` | écrit le jeu de référence de 17 fichiers ; la variante console `OSFGeneratorCLI` le produit de façon non interactive |
| `osfcsvexport/` | démo de pipeline `TOSFFile` → `TOSFDataManager` → export CSV |
| `osfmerger/` | interface VCL pour `TOSFMerger` (Win64) |

## Ligne de commande : `osftool`

`osftool` est l'outil en ligne de commande, basé sur des verbes, de
l'implémentation Delphi (Win64) ; il compte neuf verbes — `merge`, `export`,
`info`, `channels`, `stat`, `cache`, `config`, `convert`, `verify`.

La description complète figure dans le chapitre dédié
**[osftool](../tools/osftool.md)**.

## Build

La bibliothèque et la plupart des démos sont compilées avec `dcc32` (Win32)
ou `dcc64` (Win64) depuis le répertoire de projet concerné — `osfmerger` et
`osftool` en Win64. Exemple (compile check) :

```powershell
cd implementations\delphi
dcc32 -B -Q OSFCompileCheck.dpr
```

L'export HDF5 nécessite `hdf5.dll` à l'exécution ; la liaison
(`src/hdf5/`) est une connexion Delphi autonome, indépendante d'OSF, à la
bibliothèque C HDF5.

## Code source et informations complémentaires

- Code source sur GitHub : [github.com/optimeas/osf](https://github.com/optimeas/osf),
  répertoire `implementations/delphi/`
- Ligne de commande : chapitre [osftool](../tools/osftool.md)
- Spécification du format : chapitre [Format OSF](../osf_general.md)

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
