---
title: Implementazioni
description: Implementazioni OSF per linguaggio di programmazione — Delphi, Rust, Python, C++, Java e linguaggi pianificati
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Implementazione
  - Delphi
  - Rust
  - Python
  - C++
  - Java
last_update:
  date: 2026-07-10
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

## Implementazioni OSF

OSF è un formato aperto — è deliberatamente mantenuto così semplice da
poter essere implementato in modo autonomo in qualsiasi linguaggio.
Questo capitolo descrive le **implementazioni concrete** del formato per
linguaggio di programmazione: che cosa sanno fare, come si installano o
si compilano e dove si trova il codice sorgente.

Da distinguere è il capitolo [Integrazioni](../integrations/index.md):
qui si tratta del collegamento a **ecosistemi** (Arrow, PyTorch, MCP …),
non delle implementazioni nei singoli linguaggi.

### Panoramica dello stato

Legenda: ✅ disponibile · 🚧 in sviluppo attivo · 📋 pianificato

| Implementazione | Stato | Breve descrizione |
|---|---|---|
| **[Delphi](delphi.md)** | ✅ | Implementazione di riferimento — libreria completa, demo e la CLI `osftool` (Windows / RAD Studio) |
| **[Rust](rust.md)** (`osf-core`) | ✅ | Lettura, scrittura e OSFZ trasparente; al tempo stesso fondamento dell'integrazione Python |
| **[Python](python.md)** (`osfdata`) | ✅ | Binding PyO3 sul nucleo Rust, integrazione NumPy; vedere [Integrazione Python](../integrations/python.md) |
| **[C++](cpp.md)** | ✅ | Implementazione C++17 autonoma — reader, entrambi i writer, C-ABI; CI su Linux/macOS/Windows. Manuale dettagliato per sviluppatori in [C++ nel dettaglio](cpp/architecture.md) |
| **[Java](java.md)** | ✅ | Completa — Java 21, modulo JPMS, entrambi i writer OSF5, OSFZ trasparente, profilo di integrità crc; inoltre `osf-cli` e `osf-viewer` |
| **[Altri linguaggi](planned.md)** | 📋 | C — pianificato |

Lo stato più aggiornato è mantenuto dal repository su
[GitHub](https://github.com/optimeas/osf).

### Da dove cominciare?

- **Analizzare i dati** (Python/notebook, Pandas, ML): il pacchetto
  [`osfdata`](../integrations/python.md) — `pip install osfdata`,
  caricare il file, canali come array NumPy.
- **Integrazione nativa / prestazioni elevate** (server, embedded):
  l'implementazione [Rust](rust.md) o [C++](cpp.md).
- **Strumenti Windows / comportamento di riferimento**: l'implementazione
  [Delphi](delphi.md) con la riga di comando `osftool`.

Tutte le implementazioni leggono lo stesso insieme di
[file di esempio](../examples/osf_file_examples.md) e seguono le stesse
regole semantiche della specifica.

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
