---
title: Implementación en Delphi
description: La implementación de referencia en Delphi del Open Streaming Format — biblioteca, demos y la línea de comandos osftool
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Delphi
  - osftool
  - RAD Studio
  - Implementación de referencia
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Implementación en Delphi

La implementación en Delphi es la **implementación de referencia** del Open
Streaming Format: ruta completa de lectura y escritura para OSF4 y OSF5, un
conjunto de demos y la línea de comandos `osftool`. Además genera el
conjunto de [archivos de referencia](../examples/osf_file_examples.md) con el
que se comprueban las demás implementaciones. Desarrollada con RAD Studio
23.0 (Delphi 12).

## Biblioteca

Las unidades de la biblioteca se encuentran en `implementations/delphi/src/`.
Los componentes públicos más importantes:

| Unidad | Contenido |
|---|---|
| `OSF.Types` | Tipos de datos y tipos de canal (`TOSFDataType`, `TOSFVersion`, `TOSFGpsLocation`) y funciones auxiliares |
| `OSF.Filer` | `TOSFFile` — lector/escritor en streaming para OSF4 y OSF5; descompresión OSFZ transparente; análisis del XML de OSF4 mediante OmniXML (sin dependencia de MSXML) |
| `OSF.Data.Manager` | `TOSFDataManager` — lectura de alto nivel, rellena canales tipados; acceso por nombre |
| `OSF.Data.Channels` | Canales tipados en memoria, incluidos segmentos equidistantes (cada `bcStartData` abre un segmento) |
| `OSF.Log` | Despachador de registro y progreso a nivel de proceso (`Logger: TOSFLog`) con patrón de oyentes (DECISIONS §22) |
| `OSF.Export.CSV` / `OSF.Export.CSV.Unified` | Exportación a CSV — por canal (XY) o con eje de tiempo común |
| `OSF.Export.HDF5` | Exportación a HDF5 (Windows; un conjunto de datos compuesto comprimido por canal) |
| `OSF.Merger` | `TOSFMerger` — combina varios archivos OSF de un intervalo de tiempo en un único archivo de salida |
| `OSF.Meta.Cache` | Caché `.json` auxiliar por cada archivo OSF (intervalos de tiempo, número de muestras) para obtener metadatos rápidamente |

Un programa sin formularios, `OSFCompileCheck.dpr`, situado en el directorio
raíz de la implementación, cubre mediante `uses` todas las unidades de la
biblioteca y proporciona una señal de compilación limpia tras los
refactorizados.

### Arquitectura de registro

Cada unidad de la biblioteca escribe eventos de registro y de progreso en el
`Logger: TOSFLog` global. Las aplicaciones registran un `TLoggerListener`
(con su propio `MinLevel` y callbacks de eventos); el singleton se encarga de
la distribución. Detalles en
[DECISIONS §22](https://github.com/optimeas/osf/blob/main/DECISIONS.md).

## Demos

En `implementations/delphi/demos/`:

| Demo | Finalidad |
|---|---|
| `osfviewer/` | Cargar un archivo OSF, listar los canales, mostrar un canal como TeeChart (solo en el IDE) |
| `osfgenerator/` | Escribe el conjunto de referencia de 17 archivos; la variante de consola `OSFGeneratorCLI` lo genera de forma no interactiva |
| `osfcsvexport/` | Demo de la canalización `TOSFFile` → `TOSFDataManager` → exportación a CSV |
| `osfmerger/` | Interfaz VCL para `TOSFMerger` (Win64) |

## Línea de comandos: `osftool`

`osftool` es la herramienta de línea de comandos basada en verbos de la
implementación en Delphi (Win64), con nueve verbos: `merge`, `export`,
`info`, `channels`, `stat`, `cache`, `config`, `convert`, `verify`.

La descripción completa se encuentra en el capítulo propio
**[osftool](../tools/osftool.md)**.

## Compilación

La biblioteca y la mayoría de las demos se compilan con `dcc32` (Win32) o
`dcc64` (Win64) desde el directorio del proyecto correspondiente;
`osfmerger` y `osftool` como Win64. Ejemplo (comprobación de compilación):

```powershell
cd implementations\delphi
dcc32 -B -Q OSFCompileCheck.dpr
```

La exportación a HDF5 necesita `hdf5.dll` en tiempo de ejecución; el enlace
(`src/hdf5/`) es una interfaz Delphi independiente, sin relación con OSF,
hacia la biblioteca C de HDF5.

## Código fuente e información adicional

- Código fuente en GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  directorio `implementations/delphi/`
- Línea de comandos: capítulo [osftool](../tools/osftool.md)
- Especificación del formato: capítulo [Formato OSF](../osf_general.md)

> Este documento se distribuye bajo la licencia [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
