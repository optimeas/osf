---
title: Implémentations
description: Implémentations OSF par langage de programmation — Delphi, Rust, Python, C++, Java et langages prévus
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Implémentation
  - Delphi
  - Rust
  - Python
  - C++
  - Java
last_update:
  date: 2026-07-10
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

## Implémentations OSF

OSF est un format ouvert — il est volontairement conçu de façon assez simple
pour pouvoir être implémenté de manière autonome dans n'importe quel langage.
Ce chapitre décrit les **implémentations concrètes** du format par langage de
programmation : ce qu'elles savent faire, comment les installer ou les
construire, et où se trouve le code source.

À distinguer du chapitre [Intégrations](../integrations/index.md) : il y est
question du raccordement à des **écosystèmes** (Arrow, PyTorch, MCP …),
et non des implémentations de langage elles-mêmes.

### Aperçu de l'état

Légende : ✅ disponible · 🚧 en développement actif · 📋 prévu

| Implémentation | État | Description succincte |
|---|---|---|
| **[Delphi](delphi.md)** | ✅ | Implémentation de référence — bibliothèque complète, démos et la CLI `osftool` (Windows / RAD Studio) |
| **[Rust](rust.md)** (`osf-core`) | ✅ | Lecture, écriture et OSFZ transparent ; également fondation du raccordement Python |
| **[Python](python.md)** (`osfdata`) | ✅ | Bindings PyO3 sur le cœur Rust, intégration NumPy ; voir [Intégration Python](../integrations/python.md) |
| **[C++](cpp.md)** | ✅ | Implémentation C++17 autonome — reader, les deux writers, ABI C ; CI sous Linux/macOS/Windows. Manuel détaillé du développeur sous [C++ en détail](cpp/architecture.md) |
| **[Java](java.md)** | ✅ | Complète — Java 21, module JPMS, les deux writers OSF5, OSFZ transparent, profil d'intégrité crc ; avec `osf-cli` et `osf-viewer` |
| **[Autres langages](planned.md)** | 📋 | C — prévu |

L'état le plus récent est tenu à jour par le dépôt sur
[GitHub](https://github.com/optimeas/osf).

### Par où commencer ?

- **Analyser des données** (Python/notebook, Pandas, ML) : le paquet
  [`osfdata`](../integrations/python.md) — `pip install osfdata`, charger un
  fichier, canaux sous forme de tableau NumPy.
- **Intégration native / hautes performances** (serveur, embarqué) :
  l'implémentation [Rust](rust.md) ou [C++](cpp.md).
- **Outils Windows / comportement de référence** : l'implémentation
  [Delphi](delphi.md) avec la ligne de commande `osftool`.

Toutes les implémentations lisent le même jeu de
[fichiers d'exemple](../examples/osf_file_examples.md) et suivent les mêmes
règles sémantiques de la spécification.

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
