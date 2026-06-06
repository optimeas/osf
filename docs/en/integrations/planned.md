---
title: Planned integrations
description: Planned ecosystem bindings for OSF — Apache Arrow, PyTorch, TensorFlow, MCP and LangChain
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
---

🇩🇪 [German version](../../de/integrations/planned.md)

# Planned integrations

Beyond the language [implementations](../implementations/index.md), bindings
to widely used **data and AI ecosystems** are envisaged. Unlike the
implementations, which realise the format itself, integrations bridge OSF to
existing tool chains. They are planned; the repository on
[GitHub](https://github.com/optimeas/osf) carries the current status.

| Integration | Planned purpose |
|---|---|
| **Apache Arrow** | OSF channels as Arrow tables/RecordBatches — a zero-copy bridge to Pandas, Polars, DuckDB and the rest of the Arrow ecosystem. |
| **PyTorch** | OSF files as a `Dataset`/`DataLoader` for training on measurement and time-series data. |
| **TensorFlow** | Feeding OSF data into `tf.data` pipelines. |
| **MCP** (Model Context Protocol) | An MCP server that makes OSF files accessible to AI assistants (list channels, read excerpts). |
| **LangChain** | OSF as a data source in LangChain workflows. |

For tabular analysis the path via the Python package
[`osfdata`](python.md) and NumPy/Pandas is already open today; the
integrations above are meant to shorten that path for the respective
ecosystems.
