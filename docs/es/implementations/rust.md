---
title: Implementación en Rust
description: La biblioteca osf-core en Rust — lectura, escritura y OSFZ transparente; a la vez base de la integración con Python
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
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Implementación en Rust

El crate **`osf-core`** es una implementación segura y de alto rendimiento
en Rust del Open Streaming Format. Está orientado a la programación de
sistemas, a aplicaciones de servidor de alto rendimiento y —más adelante,
mediante `no_std`— a destinos embebidos.

`osf-core` es a la vez la **base de la integración con Python** `osfdata`:
el trabajo real lo realiza Rust, y Python solo utiliza un envoltorio PyO3
ligero por encima (véase
[DECISIONS §18](https://github.com/optimeas/osf/blob/main/DECISIONS.md)).
Una única base de código, dos grupos de destinatarios.

## Alcance funcional

La ruta de lectura está completa (el `BlockReader` en bruto más el
`DataManager` tipado), la ruta de escritura genera OSF5 en modo bloque, y el
crate supera un ciclo completo (round-trip) con cada archivo de referencia
incluido (cargar → escribir → volver a cargar, comparado bit a bit).

| Capacidad | Estado |
|---|---|
| Magic Header (OSF4/OSF5) incl. identificadores heredados | ✅ |
| Analizadores de metabloque OSF4-XML y OSF5-JSON | ✅ |
| Lector de bloques (todos los tipos de datos) + `ReaderStats` | ✅ |
| Best-effort con archivos truncados | ✅ |
| `DataManager` tipado, acceso por nombre y por índice | ✅ |
| Segmentos equidistantes (varios `bcStartData`) | ✅ |
| Escritor de bloques (OSF5) + validación round-trip | ✅ |
| Descompresión OSFZ transparente (gzip + zlib) | ✅ |
| Bindings PyO3 (`osfdata`) | ✅ |

## Compilación

Desde `implementations/rust/`:

```bash
cargo build
cargo test
cargo clippy
```

Seis suites de integración se ejecutan sobre `examples/` y
`examples/generated/` y comprueban cada archivo `.osf` incluido, desde el
Magic Header hasta el round-trip. Una prueba de rendimiento marcada con
`#[ignore]` puede ejecutarse con `cargo test --release -- --ignored`;
`steam_loco.osf` se lee y se escribe en local en aproximadamente 3 ms cada
operación.

`osf-core` forma parte del workspace del repositorio; todavía no se ha
publicado en crates.io. Hasta entonces, el crate se incorpora mediante una
dependencia por ruta o por Git.

## Inspeccionar un archivo

```bash
cargo run --example inspect -- ../../examples/steam_loco.osf
cargo run --example inspect -- ../../examples/weather_station.osfz
```

`inspect` es rápido (solo cabecera + metabloque). Para una lectura completa
con contadores existe `stats`, para un resumen tipado de los canales `dump`,
y `copy` demuestra el escritor (cargar → escribir → verificar).

## API del gestor

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

`load_from_file` es el punto de entrada cómodo; `load_from_reader(impl Read)`
hace lo mismo a partir de cualquier lector. El iterador `BlockReader` de
nivel inferior sigue disponible para quienes llaman y desean procesar
bloques en bruto.

## API del escritor

Dos niveles, simétricos al lado de lectura:

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

### Condiciones de contorno

- **Solo OSF5** (DECISIONS §6) y **solo modo bloque** (DECISIONS §7); la
  escritura en streaming está reservada a los lenguajes de destino
  embebidos.
- **Sin salida OSFZ** (DECISIONS §12) — OSFZ se lee de forma transparente.
- Los bloques equidistantes solo admiten `float`/`double` (revisión de la
  especificación 2026-05-04); los demás tipos numéricos se almacenan como
  `bcAbsTimeStampData`.
- La división de bloques y el aumento de `sizeoflengthvalue` (de 2 a 4 con
  muestras variables grandes) se realizan automáticamente.

## OSFZ transparente

El lector reconoce los archivos OSF comprimidos por sus dos primeros bytes
(gzip `0x1F 0x8B`, zlib `0x78 …`) y los descomprime mediante `flate2`
(Rust puro, sin zlib del sistema) antes de que el analizador del Magic Header
los vea. `ReaderStats` expone `compressed` y `compression_format`
(`None`/`Zlib`/`Gzip`).

## Código fuente e información adicional

- Código fuente en GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  directorio `implementations/rust/osf-core/`
- Integración con Python sobre esta base: [Integración con Python](../integrations/python.md)
- Especificación del formato: capítulo [Formato OSF](../osf_general.md)

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
