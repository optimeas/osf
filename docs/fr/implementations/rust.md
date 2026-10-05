---
title: Implémentation Rust
description: La bibliothèque osf-core en Rust — lecture, écriture et OSFZ transparent ; également fondation du raccordement Python
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
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Implémentation Rust

La crate **`osf-core`** est une implémentation Rust sûre et performante du
Open Streaming Format. Elle vise la programmation système, les applications
serveur hautes performances et — plus tard, via `no_std` — les cibles
embarquées.

`osf-core` est aussi la **fondation du raccordement Python** `osfdata` :
le vrai travail est effectué par Rust, Python n'utilise qu'un mince wrapper
PyO3 au-dessus (voir
[DECISIONS §18](https://github.com/optimeas/osf/blob/main/DECISIONS.md)).
Une seule base de code, deux publics.

## Étendue fonctionnelle

Le chemin de lecture est complet (`BlockReader` brut plus `DataManager`
typé), le chemin d'écriture produit de l'OSF5 en mode bloc, et la crate
effectue pour chaque fichier de référence fourni un aller-retour complet
(chargement → écriture → rechargement, comparé bit à bit).

| Capacité | État |
|---|---|
| En-tête magique (OSF4/OSF5) y compris identifiants hérités | ✅ |
| Parseurs de métabloc OSF4 XML et OSF5 JSON | ✅ |
| Lecteur de blocs (tous les types de données) + `ReaderStats` | ✅ |
| Best effort pour les fichiers tronqués | ✅ |
| `DataManager` typé, accès par nom et par index | ✅ |
| Segments équidistants (`bcStartData` multiple) | ✅ |
| Block writer (OSF5) + validation par aller-retour | ✅ |
| Décompression OSFZ transparente (gzip + zlib) | ✅ |
| Bindings PyO3 (`osfdata`) | ✅ |

## Build

Depuis `implementations/rust/` :

```bash
cargo build
cargo test
cargo clippy
```

Six suites d'intégration s'exécutent sur `examples/` et `examples/generated/`
et vérifient chaque fichier `.osf` fourni, de l'en-tête magique jusqu'à
l'aller-retour. Une sonde de performance `#[ignore]` peut être exécutée avec
`cargo test --release -- --ignored` ; `steam_loco.osf` est lu et écrit
localement en ~3 ms chacun.

`osf-core` fait partie du workspace du dépôt ; une publication sur
crates.io n'a pas encore eu lieu. D'ici là, la crate est intégrée par
dépendance de chemin ou de Git.

## Inspecter un fichier

```bash
cargo run --example inspect -- ../../examples/steam_loco.osf
cargo run --example inspect -- ../../examples/weather_station.osfz
```

`inspect` est rapide (uniquement en-tête + métabloc). Pour une lecture
complète avec compteurs, il existe `stats`, pour un aperçu typé des canaux
`dump`, et `copy` démontre le writer (chargement → écriture → vérification).

## API du manager

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

`load_from_file` est l'entrée pratique ; `load_from_reader(impl Read)` fait la
même chose à partir d'un reader quelconque. L'itérateur `BlockReader`, de
niveau inférieur, reste disponible pour les appelants qui veulent traiter des
blocs bruts.

## API du writer

Deux niveaux, symétriques au côté lecture :

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

### Contraintes

- **Uniquement OSF5** (DECISIONS §6) et **uniquement mode bloc** (DECISIONS §7) ;
  l'écriture en streaming est réservée aux cibles de langage embarquées.
- **Pas de sortie OSFZ** (DECISIONS §12) — OSFZ est lu de manière transparente.
- Blocs équidistants uniquement `float`/`double` (révision de la spécification du 2026-05-04) ; les autres
  types numériques sont écrits comme `bcAbsTimeStampData`.
- Le découpage des blocs et le relèvement de `sizeoflengthvalue` (2 → 4 pour
  les grands échantillons variables) se font automatiquement.

## OSFZ transparent

Le reader reconnaît les fichiers OSF compressés d'après les deux premiers
octets (gzip `0x1F 0x8B`, zlib `0x78 …`) et les décompresse via `flate2`
(Rust pur, sans zlib système) avant que le parseur d'en-tête magique ne les
voie. `ReaderStats` expose `compressed` et `compression_format`
(`None`/`Zlib`/`Gzip`).

## Code source et informations complémentaires

- Code source sur GitHub : [github.com/optimeas/osf](https://github.com/optimeas/osf),
  répertoire `implementations/rust/osf-core/`
- Raccordement Python au-dessus : [Intégration Python](../integrations/python.md)
- Spécification du format : chapitre [Format OSF](../osf_general.md)

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
