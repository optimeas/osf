---
title: Implementación en Python
description: El paquete osfdata — integración de OSF en Python a través de la base en Rust
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
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Implementación en Python

La integración en Python la proporciona el paquete **`osfdata`** (se importa
como `osf`). No es una implementación puramente en Python, sino un envoltorio
PyO3 ligero sobre la [base en Rust](rust.md) `osf-core`: esto se traduce en
tiempos de carga del orden de milisegundos y en datos numéricos sin copia
como arrays de NumPy.

```python
import osf

mgr = osf.load("messung.osf")             # .osf und .osfz
temp = mgr.channel("Sensor.Temperatur")
werte = temp.samples()                     # NumPy-Array, dtype passt zum OSF-Typ
zeit  = temp.timestamps_ns()
```

Instalación:

```bash
pip install osfdata        # oder:  uv pip install osfdata
```

La **documentación completa** —plataformas compatibles, resumen de la API
(`DataManager`, `Channel`, `Segment`, `WriterBuilder`), escritura, OSFZ y
notas de uso— se encuentra en el capítulo
**[Integración con Python](../integrations/python.md)**. Aquí no se duplica
de forma deliberada.

## Código fuente e información adicional

- Paquete en PyPI: [pypi.org/project/osfdata](https://pypi.org/project/osfdata/)
- Código fuente en GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  directorio `implementations/python/`
- API completa: [Integración con Python](../integrations/python.md)
- Núcleo subyacente: [Implementación en Rust](rust.md)

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
