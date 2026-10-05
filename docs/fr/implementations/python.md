---
title: Implémentation Python
description: Le paquet osfdata — raccordement Python à OSF via la fondation Rust
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Python
  - osfdata
  - PyO3
  - NumPy
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Implémentation Python

Le raccordement Python est fourni par le paquet **`osfdata`** (import sous le
nom `osf`). Ce n'est pas une implémentation Python pure, mais un mince
wrapper PyO3 au-dessus de la [fondation Rust](rust.md) `osf-core` : il en
résulte des temps de chargement de l'ordre de la milliseconde et des données
numériques sans copie sous forme de tableaux NumPy.

```python
import osf

mgr = osf.load("messung.osf")             # .osf und .osfz
temp = mgr.channel("Sensor.Temperatur")
werte = temp.samples()                     # NumPy-Array, dtype passt zum OSF-Typ
zeit  = temp.timestamps_ns()
```

Installation :

```bash
pip install osfdata        # oder:  uv pip install osfdata
```

La **documentation complète** — plateformes prises en charge, aperçu de l'API
(`DataManager`, `Channel`, `Segment`, `WriterBuilder`), écriture, OSFZ et
conseils d'utilisation — se trouve dans le chapitre
**[Intégration Python](../integrations/python.md)**. Elle n'est volontairement
pas dupliquée ici.

## Code source et informations complémentaires

- Paquet sur PyPI : [pypi.org/project/osfdata](https://pypi.org/project/osfdata/)
- Code source sur GitHub : [github.com/optimeas/osf](https://github.com/optimeas/osf),
  répertoire `implementations/python/`
- API complète : [Intégration Python](../integrations/python.md)
- Cœur sous-jacent : [implémentation Rust](rust.md)

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
