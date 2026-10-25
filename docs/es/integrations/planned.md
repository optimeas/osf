---
title: Integraciones planificadas
description: Integraciones de ecosistema planificadas para OSF — Apache Arrow, PyTorch, TensorFlow, MCP y LangChain
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Arrow
  - PyTorch
  - TensorFlow
  - MCP
  - LangChain
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Integraciones planificadas

Más allá de las [implementaciones](../implementations/index.md) en distintos
lenguajes, están previstas integraciones con **ecosistemas de datos e IA**
extendidos. A diferencia de las implementaciones, que materializan el propio
formato, las integraciones tienden un puente entre OSF y las cadenas de
herramientas existentes. Están planificadas; el estado actual se mantiene en
el repositorio de [GitHub](https://github.com/optimeas/osf).

| Integración | Finalidad planificada |
|---|---|
| **Apache Arrow** | Canales OSF como tablas/RecordBatches de Arrow — puente sin copia hacia Pandas, Polars, DuckDB y el resto del ecosistema Arrow. |
| **PyTorch** | Archivos OSF como `Dataset`/`DataLoader` para el entrenamiento con datos de medición y series temporales. |
| **TensorFlow** | Alimentación de datos OSF a pipelines de `tf.data`. |
| **MCP** (Model Context Protocol) | Un servidor MCP que hace accesibles los archivos OSF a asistentes de IA (listar canales, leer fragmentos). |
| **LangChain** | OSF como fuente de datos en flujos de trabajo de LangChain. |

Para el análisis tabular, ya hoy está disponible la vía del paquete de Python
[`osfdata`](python.md) y NumPy/Pandas; las integraciones anteriores pretenden
acortar este camino para los respectivos ecosistemas.

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
