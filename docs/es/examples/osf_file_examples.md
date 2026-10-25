---
title: Archivos de ejemplo
description: Archivos de ejemplo OSF — archivos de referencia sintéticos y grabaciones reales de campo para pruebas y aprendizaje
image: "/img/om_social_card.png"
keywords:
  - Formato de archivo
  - OSF4
  - OSF5
  - OSF
  - Ejemplos
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

## Archivos de ejemplo OSF

El directorio [`examples/`](https://github.com/optimeas/osf/tree/main/examples)
del repositorio contiene dos tipos de archivos con los que se puede
aprender el formato y comprobar un lector paso a paso: **archivos de
referencia generados** (sintéticos, una característica por archivo) y
**muestras de campo** (grabaciones reales de equipos de medición de
optiMEAS).

## Archivos de referencia generados

Los archivos de `examples/generated/` los genera la
[implementación de referencia en Delphi](../implementations/delphi.md)
(reproducibles de forma no interactiva con `OSFGeneratorCLI`). Cada archivo
se centra en **una** característica y existe tanto en la variante OSF4
(cabecera XML) como en la variante OSF5 (cabecera JSON):

| Archivo (OSF4 / OSF5) | Muestra |
|---|---|
| `osf4_equidistant.osf` / `osf5_equidistant.osf` | Canal equidistante (frecuencia de muestreo fija) |
| `osf4_scalar_numeric.osf` / `osf5_scalar_numeric.osf` | Canales escalares de coma flotante con marca de tiempo |
| `osf4_scalar_int64.osf` / `osf5_scalar_int64.osf` | Canal de entero de 64 bits con signo |
| `osf4_scalar_unsigned.osf` / `osf5_scalar_unsigned.osf` | Canales de enteros sin signo |
| `osf4_gpslocation.osf` / `osf5_gpslocation.osf` | Canal de posición GPS |
| `osf4_timestamped_string.osf` / `osf5_timestamped_string.osf` | Datos útiles `string` con marca de tiempo |
| `osf4_timestamped_binary.osf` / `osf5_timestamped_binary.osf` | Blobs `binary` con marca de tiempo |
| `osf4_mixed.osf` / `osf5_mixed.osf` | Varios tipos de canal en un solo archivo |
| — / `osf5_mixed_extended.osf` | Archivo OSF5 mixto ampliado |

En total **17 archivos** (8 OSF4 + 9 OSF5). Un lector correcto debe
procesarlos todos sin errores. Estos archivos son los casos de prueba de
lectura válidos para todos los lenguajes; el emparejamiento OSF4/OSF5
comprueba además las reglas deterministas según la versión, por ejemplo la
terminación en nulo de los datos útiles `string`/`binary` (OSF4 con `0x00`
final, OSF5 sin él).

## Muestras de campo — grabaciones reales

Datos reales de dispositivos de optiMEAS. Cubren lo que los archivos
sintéticos no pueden: grandes cantidades de canales, patrones reales de
marcas de tiempo y flujos que terminan abruptamente. Un lector debe leerlos
sin fallar y devolver los datos disponibles.

| Ruta | Formato | Descripción |
|---|---|---|
| `motorbike.osf` | OSF4 | 81 canales de telemetría de motocicleta — velocidades, temperaturas, GPS, estado del sistema |
| `steam_loco.osf` (+ `.csv`) | OSF4 | 123 canales de una grabación de una locomotora de vapor (`.csv` es una exportación de referencia de los mismos datos) |
| `weather_station.osfz` | OSF4, gzip | 28 canales, OSFZ comprimido con gzip — comprueba la descompresión transparente al leer |
| `Testdata Motorbike/` | OSFZ | Grabaciones de motocicleta de varios días en subcarpetas diarias (`YYYYMMDD/…`) — pruebas de robustez y de lectura masiva |

## Abrir un archivo

Lo más rápido es el paquete de Python
[`osfdata`](../integrations/python.md):

```python
import osf

mgr = osf.load("examples/steam_loco.osf")
print(f"{len(mgr)} Kanäle")
for ch in mgr.channels:
    print(ch.name, ch.data_type, ch.sample_count)
```

Los archivos también se pueden abrir con la implementación en
[Rust](../implementations/rust.md)
(`cargo run --example inspect -- <pfad>`), con la de
[C++](../implementations/cpp.md) o con la de
[Delphi](../implementations/delphi.md) (`osftool info`). El capítulo
[Formato OSF](../osf_general.md) ofrece una introducción al formato.

> Este documento se distribuye bajo la licencia [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
