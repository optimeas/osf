---
title: Implementazione Rust
description: La libreria osf-core in Rust — lettura, scrittura e OSFZ trasparente; al tempo stesso fondamento dell'integrazione Python
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Rust
  - osf-core
  - Cargo
  - no_std
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Implementazione Rust

La crate **`osf-core`** è un'implementazione Rust sicura e performante
del Open Streaming Format. Si rivolge alla programmazione di sistema,
alle applicazioni server ad alte prestazioni e — in seguito, tramite
`no_std` — a target embedded.

`osf-core` è al tempo stesso il **fondamento dell'integrazione Python**
`osfdata`: il lavoro vero e proprio è svolto da Rust, Python utilizza
solo un sottile wrapper PyO3 sopra di esso (vedere
[DECISIONS §18](https://github.com/optimeas/osf/blob/main/DECISIONS.md)).
Un'unica base di codice, due destinatari.

## Funzionalità

Il percorso di lettura è completo (`BlockReader` grezzo più `DataManager`
tipizzato), il percorso di scrittura genera OSF5 in modalità a blocchi e
la crate esegue per ogni file di riferimento fornito un round-trip
completo (caricamento → scrittura → nuovo caricamento, confrontato bit
per bit).

| Capacità | Stato |
|---|---|
| Magic header (OSF4/OSF5) inclusi gli identificatori legacy | ✅ |
| Parser del metablock OSF4 XML e OSF5 JSON | ✅ |
| Block reader (tutti i tipi di dati) + `ReaderStats` | ✅ |
| Best effort con file troncati | ✅ |
| `DataManager` tipizzato, accesso per nome e per indice | ✅ |
| Segmenti equidistanti (`bcStartData` multiplo) | ✅ |
| Block writer (OSF5) + validazione round-trip | ✅ |
| Decompressione OSFZ trasparente (gzip + zlib) | ✅ |
| Binding PyO3 (`osfdata`) | ✅ |

## Build

Da `implementations/rust/`:

```bash
cargo build
cargo test
cargo clippy
```

Sei suite di integrazione vengono eseguite su `examples/` e
`examples/generated/` e verificano ogni file `.osf` fornito, dal magic
header al round-trip. Un test di prestazioni `#[ignore]` può essere
eseguito con `cargo test --release -- --ignored`; `steam_loco.osf` viene
letto e scritto localmente in circa 3 ms ciascuno.

`osf-core` fa parte del workspace del repository; una pubblicazione su
crates.io non è ancora avvenuta. Fino ad allora la crate viene integrata
tramite dipendenza per percorso o Git.

## Ispezionare un file

```bash
cargo run --example inspect -- ../../examples/steam_loco.osf
cargo run --example inspect -- ../../examples/weather_station.osfz
```

`inspect` è veloce (solo header + metablock). Per un'operazione di
lettura completa con contatori esiste `stats`, per una panoramica
tipizzata dei canali `dump`, e `copy` dimostra il writer (caricamento →
scrittura → verifica).

## API del manager

```rust
use osf_core::DataManager;

let mgr = DataManager::load_from_file("examples/steam_loco.osf")?;
println!("Kanäle: {}", mgr.channels().len());

// Zugriff über den Namen (verpflichtend, DECISIONS §10)
let temp = mgr.channel("Sensor.Temperature").expect("nicht gefunden");

// Über die Samples iterieren — Segment-Zeitstempel werden lazy rekonstruiert
for sample in temp.samples_with_time() {
    println!("{} ns: {:?}", sample.timestamp_ns, sample.value);
}
```

`load_from_file` è il punto di ingresso comodo; `load_from_reader(impl Read)`
fa lo stesso partendo da un reader qualsiasi. L'iteratore `BlockReader`,
di livello più basso, resta disponibile per i chiamanti che desiderano
elaborare blocchi grezzi.

## API del writer

Due livelli, simmetrici rispetto al lato di lettura:

```rust
use osf_core::{DataManager, writer};

// Komfort: einen DataManager als OSF5 zurückschreiben (Round-Trip)
let mgr = DataManager::load_from_file("input.osf")?;
writer::write_to_file(&mgr, "output.osf")?;
```

```rust
use osf_core::writer::{WriterBuilder, ChannelDef};
use osf_core::types::{ChannelType, DataType};

// Builder: programmatischer Aufbau
let mut builder = WriterBuilder::new().creator("my-app:1.0").tag("preview");
let idx = builder.add_channel(ChannelDef {
    name: "Sensor.Temperature".into(),
    data_type: DataType::Double,
    channel_type: ChannelType::Scalar,
    ..Default::default()
})?;
builder.add_timestamped_samples_f64(idx, &timestamps_ns, &values)?;
builder.write_to_file("output.osf")?;
```

### Vincoli

- **Solo OSF5** (DECISIONS §6) e **solo modalità a blocchi**
  (DECISIONS §7); la scrittura in streaming è riservata ai target
  linguistici embedded.
- **Nessun output OSFZ** (DECISIONS §12) — OSFZ viene letto in modo
  trasparente.
- Blocchi equidistanti solo `float`/`double` (revisione della specifica
  2026-05-04); gli altri tipi numerici come `bcAbsTimeStampData`.
- La suddivisione in blocchi e l'aumento di `sizeoflengthvalue` (2 → 4 in
  caso di campioni variabili di grandi dimensioni) avvengono
  automaticamente.

## OSFZ trasparente

Il reader riconosce i file OSF compressi dai primi due byte (gzip
`0x1F 0x8B`, zlib `0x78 …`) e li decomprime tramite `flate2` (Rust
puro, nessuna zlib di sistema) prima che il parser del magic header li
veda. `ReaderStats` espone `compressed` e `compression_format`
(`None`/`Zlib`/`Gzip`).

## Codice sorgente e ulteriori informazioni

- Codice sorgente su GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  directory `implementations/rust/osf-core/`
- Integrazione Python basata su di essa: [Integrazione Python](../integrations/python.md)
- Specifica del formato: capitolo [Formato OSF](../osf_general.md)

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
