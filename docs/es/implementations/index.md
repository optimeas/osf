---
title: Implementaciones
description: Implementaciones de OSF por lenguaje de programación — Delphi, Rust, Python, C++, Java y lenguajes previstos
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Implementación
  - Delphi
  - Rust
  - Python
  - C++
  - Java
last_update:
  date: 2026-07-10
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

## Implementaciones de OSF

OSF es un formato abierto; se ha mantenido deliberadamente tan sencillo que
puede implementarse de forma independiente en cualquier lenguaje. Este
capítulo describe las **implementaciones concretas** del formato por
lenguaje de programación: qué pueden hacer, cómo se instalan o se compilan y
dónde se encuentra el código fuente.

Debe distinguirse del capítulo [Integraciones](../integrations/index.md):
este trata de la conexión con **ecosistemas** (Arrow, PyTorch, MCP …), no de
las implementaciones en cada lenguaje.

### Resumen del estado

Leyenda: ✅ disponible · 🚧 en desarrollo activo · 📋 previsto

| Implementación | Estado | Breve descripción |
|---|---|---|
| **[Delphi](delphi.md)** | ✅ | Implementación de referencia — biblioteca completa, demos y la CLI `osftool` (Windows / RAD Studio) |
| **[Rust](rust.md)** (`osf-core`) | ✅ | Lectura, escritura y OSFZ transparente; además, base de la integración con Python |
| **[Python](python.md)** (`osfdata`) | ✅ | Enlaces PyO3 sobre el núcleo de Rust, integración con NumPy; véase [Integración con Python](../integrations/python.md) |
| **[C++](cpp.md)** | ✅ | Implementación independiente en C++17 — lector, ambos escritores, C-ABI; CI en Linux/macOS/Windows. Manual detallado para desarrolladores en [C++ en detalle](cpp/architecture.md) |
| **[Java](java.md)** | ✅ | Completa — Java 21, módulo JPMS, ambos escritores OSF5, OSFZ transparente, perfil de integridad crc; además `osf-cli` y `osf-viewer` |
| **[Otros lenguajes](planned.md)** | 📋 | C — previsto |

El estado más reciente lo mantiene el repositorio en
[GitHub](https://github.com/optimeas/osf).

### ¿Por dónde empezar?

- **Analizar datos** (Python/notebook, Pandas, ML): el paquete
  [`osfdata`](../integrations/python.md) — `pip install osfdata`, cargar el
  archivo, canales como matriz NumPy.
- **Integración nativa / alto rendimiento** (servidor, embebido): la
  implementación en [Rust](rust.md) o en [C++](cpp.md).
- **Herramientas de Windows / comportamiento de referencia**: la
  implementación en [Delphi](delphi.md) con la línea de comandos `osftool`.

Todas las implementaciones leen el mismo conjunto de
[archivos de ejemplo](../examples/osf_file_examples.md) y siguen las mismas
reglas semánticas de la especificación.

> Este documento se distribuye bajo la licencia [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
