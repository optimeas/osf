---
title: Rust-Implementierung
description: Die osf-core-Bibliothek in Rust — Lesen, Schreiben und transparentes OSFZ; zugleich Fundament der Python-Anbindung
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
---

🇬🇧 [English version](../../en/implementations/rust.md)

# Rust-Implementierung

Die Crate **`osf-core`** ist eine sichere, performante Rust-Implementierung
des Open Streaming Format. Sie zielt auf Systemprogrammierung,
leistungsfähige Server-Anwendungen und — später, über `no_std` — auf
Embedded-Ziele.

`osf-core` ist zugleich das **Fundament der Python-Anbindung** `osfdata`:
die eigentliche Arbeit erledigt Rust, Python nutzt nur einen dünnen
PyO3-Wrapper darüber (siehe
[DECISIONS §18](https://github.com/optimeas/osf/blob/main/DECISIONS.md)).
Eine Codebasis, zwei Zielgruppen.

## Funktionsumfang

Der Lesepfad ist vollständig (roher `BlockReader` plus typisierter
`DataManager`), der Schreibpfad erzeugt OSF5 im Block-Modus, und die Crate
durchläuft für jede mitgelieferte Referenzdatei einen vollständigen
Round-Trip (Laden → Schreiben → erneut Laden, bitweise verglichen).

| Fähigkeit | Status |
|---|---|
| Magic-Header (OSF4/OSF5) inkl. Legacy-Kennungen | ✅ |
| OSF4-XML- und OSF5-JSON-Metablock-Parser | ✅ |
| Block-Reader (alle Datentypen) + `ReaderStats` | ✅ |
| Best-Effort bei abgeschnittenen Dateien | ✅ |
| Typisierter `DataManager`, Zugriff per Name und Index | ✅ |
| Äquidistante Segmente (mehrfaches `bcStartData`) | ✅ |
| Block-Writer (OSF5) + Round-Trip-Validierung | ✅ |
| Transparente OSFZ-Dekompression (gzip + zlib) | ✅ |
| PyO3-Bindings (`osfdata`) | ✅ |

## Bauen

Aus `implementations/rust/`:

```bash
cargo build
cargo test
cargo clippy
```

Sechs Integrations-Suiten laufen über `examples/` und `examples/generated/`
und prüfen jede mitgelieferte `.osf`-Datei vom Magic-Header bis zum
Round-Trip. Eine `#[ignore]`-Performance-Probe lässt sich mit
`cargo test --release -- --ignored` ausführen; `steam_loco.osf` liest und
schreibt lokal in jeweils ~3 ms.

`osf-core` ist Teil des Repository-Workspaces; eine Veröffentlichung auf
crates.io ist noch nicht erfolgt. Bis dahin wird die Crate per Pfad- oder
Git-Abhängigkeit eingebunden.

## Eine Datei inspizieren

```bash
cargo run --example inspect -- ../../examples/steam_loco.osf
cargo run --example inspect -- ../../examples/weather_station.osfz
```

`inspect` ist schnell (nur Header + Metablock). Für einen vollständigen
Lesevorgang mit Zählern gibt es `stats`, für eine typisierte
Kanal-Übersicht `dump`, und `copy` demonstriert den Writer (Laden →
Schreiben → Verifizieren).

## Manager-API

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

`load_from_file` ist der bequeme Einstieg; `load_from_reader(impl Read)` tut
dasselbe ausgehend von einem beliebigen Reader. Der tiefer liegende
`BlockReader`-Iterator bleibt für Aufrufer verfügbar, die rohe Blöcke
verarbeiten wollen.

## Writer-API

Zwei Stufen, symmetrisch zur Leseseite:

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

### Randbedingungen

- **Nur OSF5** (DECISIONS §6) und **nur Block-Modus** (DECISIONS §7);
  Streaming-Schreiben ist eingebetteten Sprach-Zielen vorbehalten.
- **Kein OSFZ-Output** (DECISIONS §12) — gelesen wird OSFZ transparent.
- Äquidistante Blöcke nur `float`/`double` (Spec-Rev 2026-05-04); andere
  numerische Typen als `bcAbsTimeStampData`.
- Block-Splitting und das Hochsetzen von `sizeoflengthvalue` (2 → 4 bei
  großen variablen Samples) geschehen automatisch.

## Transparentes OSFZ

Der Reader erkennt komprimierte OSF-Dateien an den ersten zwei Bytes
(gzip `0x1F 0x8B`, zlib `0x78 …`) und entpackt sie über `flate2`
(reines Rust, kein System-zlib), bevor der Magic-Header-Parser sie sieht.
`ReaderStats` legt `compressed` und `compression_format`
(`None`/`Zlib`/`Gzip`) offen.

## Quellcode und weiterführende Informationen

- Quellcode auf GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  Verzeichnis `implementations/rust/osf-core/`
- Python-Anbindung darüber: [Python-Integration](../integrations/python.md)
- Format-Spezifikation: Kapitel [OSF-Format](../osf_general.md)
