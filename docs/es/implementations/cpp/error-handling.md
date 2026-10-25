---
title: Gestión de errores
description: Result<T> y osf::Error en el núcleo libre de excepciones, el catálogo completo de códigos de error y la capa opcional osf::throwing
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - Result
  - Error
  - Excepciones
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Gestión de errores

El núcleo de la implementación en C++ está **libre de excepciones**: toda
operación que pueda fallar devuelve `osf::Result<T>`, es decir, un
`tl::expected<T, osf::Error>`. Quien prefiera excepciones coloca encima la
fina capa opcional `osf::throwing`. Ambos estilos pueden combinarse.

## `osf::Error` y `osf::Result<T>`

```cpp
struct Error {
    Code        code;     // stabile Kategorie — hierauf verzweigen
    std::string message;  // menschenlesbares Detail — nur zur Anzeige
};

template <typename T>
using Result = tl::expected<T, Error>;
```

Idioma básico:

```cpp
auto r = osf::DataManager::loadFromFile(pfad);
if (!r) {
    log("Laden fehlgeschlagen [{}]: {}",
        osf::errorCategoryName(r.error().code),   // stabiler Name, z. B. "io_error"
        r.error().message);
    return;
}
osf::DataManager const& mgr = *r;   // oder r.value()
```

Reglas:

- **Ramifique según `code` y use `message` solo para mostrarlo.** La
  redacción del mensaje no forma parte de la API y puede cambiar.
- Todos los valores de retorno `Result` son `[[nodiscard]]`: el compilador
  advierte de los errores ignorados.
- `errorCategoryName(code)` devuelve un identificador de cadena estable para
  los registros (estático, sin propiedad).
- `Result<void>` señala operaciones puras de éxito/fallo
  (`writer.start()`, `writeToFile(...)`, …): `if (!r) …`.

## Catálogo de códigos de error

### Errores de entrada y de API

| Código | Significado | Origen habitual |
|---|---|---|
| `InvalidArgument` | Precondición de la API incumplida: `ChannelDef` no válida, índice de canal desconocido en el escritor, `count == 0`, escritor en una fase incorrecta del ciclo de vida, frecuencia de muestreo no positiva | Escritor |
| `IoError` | Error de archivo/flujo: no se puede abrir, error de lectura/escritura, error de fsync | En todas partes |
| `NotFound` | Reservado para las API de búsqueda (las búsquedas de canal devuelven `nullptr` en su lugar) | — |
| `Unknown` | Valor de reserva sin una categoría más específica; código de un `Error` construido por defecto | — |
| `ParseError` | Error de análisis genérico cuando no encaja una categoría más específica (poco frecuente) | Analizador |

### Cabecera mágica

| Código | Significado |
|---|---|
| `InvalidMagicHeader` | La primera línea no es una cabecera OSF bien formada; normalmente «no es un archivo OSF» |
| `UnsupportedVersion` | La cabecera es analizable, pero el identificador no es ninguna de las cuatro grafías aceptadas (`OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) |
| `MagicHeaderTooLong` | Ningún salto de línea en 128 bytes; con seguridad no es un archivo OSF |

### Metabloque

| Código | Significado |
|---|---|
| `InvalidMetablock` | Error de estructura: falta un campo obligatorio, número no analizable, `sizeOfLengthValue` ≠ 2/4 (de lo contrario corrompería en silencio cada lectura de bloque), elemento raíz incorrecto |
| `JsonParseError` | OSF5: el cuerpo del metabloque no es JSON válido (diagnóstico del analizador en `message`) |
| `XmlParseError` | OSF4: el cuerpo del metabloque no es XML bien formado (diagnóstico + desplazamiento de bytes en `message`) |
| `RemovedInSpec` | El archivo utiliza un tipo de datos **eliminado** con la revisión de la especificación 2026-05-04 (`pair`, `triple`, `candata`, `gpsdata`). Se rechaza de forma estricta: el antiguo diseño de datos útiles no es reproducible desde una compilación actual; el mensaje indica el sustituto |

### Flujo de bloques

| Código | Significado |
|---|---|
| `UnknownChannelIndex` | El bloque hace referencia a un índice de canal sin definición en el metabloque. Sin definición se desconoce el ancho del campo de longitud → señal de corrupción, interrupción estricta |
| `InvalidBlock` | Datos útiles estructuralmente defectuosos (longitud incorrecta para el tipo de datos, bloque equidistante en un canal de cadenas, muestra que desborda la capacidad de bloque del escritor en streaming, …) |
| `ChannelMixedBlockTypes` | Un canal proporciona tanto bloques equidistantes (`bcStartData`/`bcContinuedData`) como bloques con marca de tiempo; prohibido por la especificación |
| `ContinuedDataWithoutStart` | `bcContinuedData` sin un `bcStartData` previo: sin un segmento abierto, la continuación no tiene base temporal |
| `RelStampWithoutAnchor` | `bcContinuedRelStampData` sin una marca de tiempo absoluta previa: los deltas no tienen ancla |
| `DataTypeMismatch` | Tipo solicitado ≠ tipo almacenado (p. ej., `asDoublesFlat` sobre un canal `int32`, `asStrings` sobre un canal binario) |

### Lo que deliberadamente **no** es un error

| Situación | Comportamiento |
|---|---|
| El archivo termina en mitad de un bloque (corte de alimentación) | Mejor esfuerzo: se entregan todos los bloques completos, `stats.blocksTruncated = 1`, la iteración termina limpiamente |
| Tipo de datos/tipo de canal futuro desconocido | El canal se analiza como `Unsupported`, los bloques se consumen alineados como `Skipped`, el resto de canales se cargan con normalidad |
| Bytes de control obsoletos/reservados (archivos de campo antiguos) | `BlockKind::Skipped` con `SkipReason`, contador en `stats` |
| Campos de canal obsoletos (`scale`, `offset`, `physicalunit1..3`, …) | Se toleran e ignoran en silencio (los archivos de campo reales los llevan todos) |
| Búsqueda de canal sin resultado | `nullptr`, sin `Result` |

## La capa que lanza excepciones — `osf::throwing`

Solo de cabeceras, opcional (opt-in) (`#include <osf/throwing.h>`),
deliberadamente **no** incluida en la cabecera general `<osf/osf.h>` ni
compilada dentro de la biblioteca. Quien nunca la incluye no incorpora
ninguna maquinaria de excepciones.

```cpp
#include <osf/throwing.h>

try {
    auto mgr = osf::throwing::load("messung.osf");      // DataManager oder wirft
    osf::throwing::writeToFile(mgr, "kopie.osf");

    osf::StreamingWriter w{pfad};
    auto ch = osf::throwing::unwrap(w.addChannel(def)); // Result<T> -> T oder wirft
    osf::throwing::unwrap(w.start());
    osf::throwing::unwrap(w.writeTimestampedSample<double>(ch, ts, wert));
    osf::throwing::unwrap(w.close());
} catch (osf::Exception const& e) {
    // e.what()  — Message (oder Kategorie-Name, wenn Message leer)
    // e.code()  — Error::Code für programmatisches Verzweigen
    // e.error() — der vollständige strukturierte osf::Error
}
```

La capa consta exactamente de tres componentes:

| Componente | Finalidad |
|---|---|
| `osf::Exception : std::runtime_error` | Transporta el `osf::Error` completo; reside en `osf`, no en `osf::throwing` |
| `osf::throwing::unwrap(Result<T>)` | Adaptador universal: extrae el valor o lanza una excepción. Funciona con **cualquier** `Result` del núcleo, también de los métodos del escritor; por eso no hacen falta envoltorios que lancen excepciones por cada método |
| `osf::throwing::load / writeToFile / writeTo` | Equivalentes que lanzan excepciones de las operaciones de alto nivel más frecuentes |

## Elección de estilo en la práctica

- **Código de biblioteca/embebido, rutas críticas, bases de código con
  `-fno-exceptions`:** quedarse con el núcleo `Result`.
- **Código de aplicación con una estrategia de excepciones existente:**
  utilizar `throwing` en el borde exterior; internamente todo sigue siendo
  `Result`.
- **Mixto:** `unwrap` de forma puntual donde un error simplemente se
  propagaría de todos modos, p. ej., en un `main` de CLI que tiene arriba un
  `try/catch`.

## Caso especial de los escritores: errores persistentes (sticky)

El `StreamingWriter` recuerda el primer error de E/S (estado «Broken») y lo
devuelve en **cada** llamada posterior, incluido `close()`. En los bucles de
escritura basta, por tanto, con una comprobación de errores por iteración; la
causa no se pierde aunque se evalúe solo al final.

> Este documento se distribuye bajo la licencia [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
