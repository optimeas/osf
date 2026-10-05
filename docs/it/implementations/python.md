---
title: Implementazione Python
description: Il pacchetto osfdata — integrazione Python di OSF tramite la foundation Rust
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
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Implementazione Python

L'integrazione Python è fornita dal pacchetto **`osfdata`** (importato
come `osf`). Non è un'implementazione Python pura, ma un sottile wrapper
PyO3 sulla [foundation Rust](rust.md) `osf-core`: ne risultano tempi di
caricamento nell'ordine dei millisecondi e dati numerici senza copia
come array NumPy.

```python
import osf

mgr = osf.load("messung.osf")             # .osf und .osfz
temp = mgr.channel("Sensor.Temperatur")
werte = temp.samples()                     # NumPy-Array, dtype passt zum OSF-Typ
zeit  = temp.timestamps_ns()
```

Installazione:

```bash
pip install osfdata        # oder:  uv pip install osfdata
```

La **documentazione completa** — piattaforme supportate, panoramica
dell'API (`DataManager`, `Channel`, `Segment`, `WriterBuilder`),
scrittura, OSFZ e note d'uso — si trova nel capitolo
**[Integrazione Python](../integrations/python.md)**. Qui non viene
deliberatamente duplicata.

## Codice sorgente e ulteriori informazioni

- Pacchetto su PyPI: [pypi.org/project/osfdata](https://pypi.org/project/osfdata/)
- Codice sorgente su GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  directory `implementations/python/`
- API completa: [Integrazione Python](../integrations/python.md)
- Nucleo sottostante: [implementazione Rust](rust.md)

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
