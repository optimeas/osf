---
title: Integración con Python
description: Bindings de Python para el Open Streaming Format (OSF) — lectura y escritura con rendimiento nativo mediante osfdata
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Python
  - osfdata
  - PyO3
  - NumPy
  - pandas
last_update:
  date: 2026-07-26
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Integración con Python

El paquete **`osfdata`** ofrece una integración completa en Python del Open Streaming Format. Lee y escribe archivos OSF4 y OSF5, reconoce automáticamente los archivos OSFZ comprimidos y se integra a la perfección en el ecosistema científico de Python en torno a NumPy.

A diferencia de las implementaciones puramente en Python, el trabajo real se realiza en una biblioteca de Rust. Python solo ve y utiliza el envoltorio ligero que hay por encima. El resultado: tiempos de carga del orden de milisegundos incluso con varios cientos de miles de muestras, una sobrecarga de memoria muy baja y datos numéricos que se transfieren a arrays de NumPy sin copia.

## Para qué sirve `osfdata`

`osfdata` resuelve tareas habituales relacionadas con datos OSF en Python:

- **Llevar datos a pipelines de análisis.** Se cargan los archivos OSF, se obtienen canales individuales directamente como arrays de NumPy y se pasan a bibliotecas como SciPy, scikit-learn o PyTorch.
- **Combinar datos de distintas fuentes.** Se leen, filtran o combinan archivos OSF procedentes del campo y se escriben como un nuevo archivo OSF.
- **Actualizar datos existentes al estado actual.** Se leen archivos OSF4 y se vuelven a escribir como OSF5 — una migración cómoda sin herramienta adicional.
- **Procesar archivos comprimidos de forma transparente.** Los archivos OSFZ (zlib o gzip) se reconocen y se descomprimen automáticamente, sin que el usuario tenga que configurar nada.

El paquete está dirigido a analistas de datos, ingenieros y científicos que desean incorporar datos OSF en flujos de trabajo de Python. No está dirigido a desarrolladores de sistemas embebidos que *generan* datos — para ello existen implementaciones de escritura independientes y ligeras directamente en los dispositivos.

## Relación con `python-osf`

`osfdata` es el sucesor moderno del paquete existente [`python-osf`](https://github.com/optimeas/python-osf). `python-osf` es una implementación puramente en Python que solo puede leer OSF4 y que es claramente más lenta con archivos grandes. Además, `osfdata` ofrece:

- compatibilidad completa con OSF4 y OSF5 (lectura *y* escritura),
- un rendimiento considerablemente superior gracias a la base en Rust,
- cobertura completa de todos los tipos de datos, incluidos `binary`, `gpslocation` y los enteros sin signo,
- conformidad con la revisión actual de la especificación (2026-05-04),
- descompresión OSFZ transparente tanto para archivos comprimidos con zlib como con gzip.

`python-osf` se marcará como obsoleto más adelante, en cuanto `osfdata` cubra en la práctica todos los casos de uso.

## Plataformas compatibles

`osfdata` se distribuye como paquete binario precompilado (wheel) para las siguientes plataformas:

| Plataforma | Arquitectura | Versiones de Python |
|---|---|---|
| Linux | x86_64 | 3.9 – 3.13 |
| Linux | aarch64 (ARM de 64 bits) | 3.9 – 3.13 |
| macOS | arm64 (Apple Silicon) | 3.9 – 3.13 |
| Windows | x86_64 | 3.9 – 3.13 |

En todas las plataformas compatibles, la instalación se realiza sin compilador ni otras herramientas. Para macOS con Intel no se entrega ningún wheel; es posible instalar desde la distribución de código fuente, pero en ese caso se requiere una toolchain de Rust local. Lo mismo se aplica a otras plataformas exóticas (FreeBSD, Windows en ARM, distribuciones de Linux antiguas sin compatibilidad con manylinux).

## Instalación

`osfdata` puede instalarse con los dos gestores de paquetes de Python habituales.

### Con pip

```bash
pip install osfdata
```

### Con uv

```bash
uv pip install osfdata
```

`uv` es un gestor de paquetes más reciente y bastante más rápido que combina `pip` y `venv`. Si aún no lo conoce, encontrará la instalación y la documentación en [docs.astral.sh/uv](https://docs.astral.sh/uv).

## Importación y primer ejemplo

Tras la instalación, el paquete se importa con el nombre corto `osf` (el nombre de distribución `osfdata` se usa en PyPI; el nombre de importación es independiente de este — un patrón habitual en Python, comparable a `scikit-learn` ↔ `import sklearn`).

```python
import osf

mgr = osf.load("messung.osf")
print(f"Datei enthält {len(mgr)} Kanäle")

temp = mgr.channel("Sensor.Temperatur")
samples = temp.samples()      # NumPy-Array, dtype passt zum OSF-Datentyp
zeitstempel = temp.timestamps_ns()
```

Con esto ya está hecho lo esencial: cargar el archivo, acceder a un canal por su nombre y obtener los valores como array de NumPy.

## Resumen de la API

La biblioteca consta de unos pocos componentes públicos claros. Todos los demás detalles —por ejemplo, cómo se gestiona un canal con varios segmentos o cómo se distinguen los datos con marca de tiempo de los equidistantes— se deducen al trabajar con los objetos que se enumeran aquí.

### Funciones a nivel de módulo

| Función | Finalidad |
|---|---|
| `osf.load(path)` | Carga un archivo OSF u OSFZ y devuelve un `DataManager`. Reconoce el formato automáticamente. |
| `osf.save(manager, path)` | Escribe un `DataManager` como archivo OSF5. |

### Clase `DataManager`

Representa un archivo OSF cargado con todos sus canales y metadatos.

| Atributo / método | Descripción |
|---|---|
| `len(mgr)` | Número de canales. |
| `mgr.channels` | Lista de todos los canales (`list[Channel]`). |
| `mgr.channel(name)` | Acceder a un canal por su nombre. Devuelve `None` si no existe. |
| `mgr.channel_by_index(i)` | Acceder a un canal por su índice numérico. |
| `mgr.stats` | Objeto `ReaderStats` con estadísticas de la operación de lectura. |

### Clase `Channel`

Un canal individual con metadatos y valores. Tres variantes —equidistante, numérico con marca de tiempo, variable con marca de tiempo (para cadenas de texto y datos binarios)— se utilizan a través de la misma API.

| Atributo / método | Descripción |
|---|---|
| `ch.name` | Nombre del canal (a menudo jerárquico, p. ej. `"Motor.Drehzahl"`). |
| `ch.index` | Índice numérico del canal en el archivo. |
| `ch.data_type` | Tipo de datos como cadena de texto (`"double"`, `"int32"`, `"string"` …). |
| `ch.channel_type` | `"equidistant"`, `"timestamped"` o `"variable"`. |
| `ch.sample_count` | Número de muestras. |
| `ch.physical_unit` | Unidad física (si se ha indicado). |
| `ch.is_empty` | True si el canal no contiene datos. |
| `ch.samples()` | Valores como array de NumPy. |
| `ch.timestamps_ns()` | Marcas de tiempo en nanosegundos desde la época como array de NumPy `int64`. |
| `ch.segments` | Lista de segmentos (significativa solo en canales equidistantes). |

### Clase `Segment`

Describe una sección de un canal equidistante — por ejemplo, cuando una grabación se divide en varias fases por eventos de disparo o correcciones de deriva.

| Atributo | Descripción |
|---|---|
| `seg.start_timestamp_ns` | Instante inicial del segmento en nanosegundos desde la época. |
| `seg.sample_rate_hz` | Frecuencia de muestreo dentro de este segmento en hercios. |
| `seg.sample_count` | Número de muestras de este segmento. |

### Clase `ReaderStats`

Información de diagnóstico sobre la última operación de carga.

| Atributo | Descripción |
|---|---|
| `stats.compressed` | True si el archivo estaba comprimido con OSFZ. |
| `stats.compression_format` | `"gzip"`, `"zlib"` o `None`. |
| `stats.channels_total` | Número de canales del archivo. |
| `stats.blocks_total` | Número de bloques de datos leídos. |
| `stats.elapsed_ms` | Tiempo de carga en milisegundos. |
| `stats.file_size_bytes` | Tamaño del archivo en el disco. |

### Clase `WriterBuilder`

Construye archivos OSF5 a partir de definiciones de canal y datos de muestra. El builder acepta los canales uno tras otro, y cada canal puede recibir varios segmentos o bloques de muestras.

```python
b = osf.WriterBuilder().creator("messsystem-v1").tag("vortest")

idx = b.add_channel(
    name="Sensor.Druck",
    data_type="double",
    channel_type="scalar",
    physical_unit="bar",
)

import numpy as np
werte = np.array([1.013, 1.015, 1.014, 1.012], dtype=np.float64)
b.add_equidistant_segment(
    idx,
    start_ns=1_700_000_000_000_000_000,
    sample_rate_hz=1.0,
    values=werte,
)

b.write_to_file("ausgabe.osf")
```

Los métodos más importantes:

| Método | Finalidad |
|---|---|
| `b.creator(s)` / `b.tag(s)` / `b.reason(s)` | Establecer los metadatos del archivo. |
| `b.location(lat, lon, alt)` | Posición geográfica de la grabación. |
| `b.add_channel(...)` | Definir un canal nuevo; devuelve el índice del canal. |
| `b.add_equidistant_segment(idx, start_ns, sample_rate_hz, values)` | Añadir un segmento equidistante (solo `float`/`double`). |
| `b.add_timestamped_samples(idx, ts_ns, values)` | Añadir muestras numéricas con marca de tiempo. |
| `b.add_string_samples(idx, ts_ns, values)` | Añadir muestras de cadena de texto con marca de tiempo. |
| `b.add_binary_samples(idx, ts_ns, values)` | Añadir muestras de datos binarios (p. ej. imágenes, audio). |
| `b.write_to_file(path)` | Escribir en un archivo los datos reunidos. |

## Notas de uso

**Marcas de tiempo.** Todas las indicaciones de tiempo son enteros de 64 bits en nanosegundos desde la época Unix (UTC). Esta precisión cubre tanto las mediciones de vibraciones de alta frecuencia como los datos de proceso lentos, sin necesidad de distintos tipos de datos de tiempo. Para una versión posterior se ha planificado una capa de conversión opcional al tipo `datetime` de Python.

**Tipos de datos de NumPy.** El dtype del array de NumPy devuelto corresponde directamente al tipo de datos de OSF: `double` → `float64`, `int32` → `int32`, `bool` → `bool`, etc. No se produce ninguna conversión implícita — si el canal contiene `int16`, el array de NumPy también es `int16`.

**Gestión de memoria.** Los arrays numéricos de muestras se transfieren entre Rust y Python sin copia. Esto hace que la lectura de canales grandes (varios millones de muestras) sea muy rápida incluso con hardware modesto.

**Gestión de errores.** Todas las funciones lanzan `osf.OsfError` (una subclase de `Exception`) cuando algo sale mal — archivo no encontrado, formato no válido, tipo de datos desconocido. El comportamiento ante bloques de datos desconocidos o dañados sigue el principio de best-effort: se entregan datos hasta el último bloque legible correctamente y, a continuación, se interrumpe de forma limpia.

## Ejemplos de aplicación

Cuadernos de ejemplo y scripts detallados seguirán en una sección aparte. Temas planificados:

- Exploración rápida de un archivo OSF desconocido
- Migración de OSF4 a OSF5
- Filtrado y combinación de varias grabaciones
- Transición a pandas para el análisis tabular
- Integración en datasets de PyTorch

## Código fuente e información adicional

- El paquete en PyPI: [pypi.org/project/osfdata](https://pypi.org/project/osfdata/)
- Código fuente en GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf), directorio `implementations/python/`
- Proceso de compilación y publicación: véase [`BUILD.md`](https://github.com/optimeas/osf/blob/main/implementations/python/BUILD.md) en el repositorio
- Especificación del formato: véase el capítulo [Formato OSF](../osf_general.md) de esta documentación

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
