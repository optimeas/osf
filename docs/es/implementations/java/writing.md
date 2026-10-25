---
title: Escritura
description: Escritura de OSF5 con osf-java — StreamingWriter (embebido, a prueba de fallos), BlockWriter (orientado al analista), el perfil de integridad crc y round-trip
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - StreamingWriter
  - BlockWriter
  - crc32c
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Escritura

La biblioteca escribe **exclusivamente OSF5** — incluso cuando el origen era
un archivo OSF4 u OSFZ. Dos clases de escritor del paquete
`com.optimeas.osf` cubren dos perfiles de uso muy distintos:

| | `StreamingWriter` | `BlockWriter` |
|---|---|---|
| Uso | Grabación embebida | Análisis, conversión, exportación |
| Memoria | limitada (un búfer de bloque por canal, descartado tras emitirlo) | recopila todas las muestras en RAM |
| Durabilidad | `FileChannel.force(true)` por bloque — a prueba de cortes de corriente | sin `force`; el archivo se crea al final |
| Destino | ruta de archivo (`Path`) | `Path` **o** cualquier `OutputStream` (memoria, socket) |
| `sizeOfLengthValue` | fijo desde la declaración del canal (el metabloque ya está en disco) | aumento automático de 2 → 4 en canales variables |
| Ciclo de vida | `create()` → canales → `begin()` → escritura → `close()` | `new` → recopilación → `writeToFile()` / `writeTo()` |
| Emisión múltiple | no (un archivo por instancia, `Closeable`) | sí (`writeTo*` puede invocarse cualquier número de veces) |

Ambos comparten los mismos tipos de canal, la misma aritmética de chunking y
las mismas familias de escritura (equidistante, numérico con marca de tiempo,
GPS, String/Binary). Para los mismos canales, muestras, `sizeOfLengthValue` y
`created_utc` generan archivos OSF5 **idénticos byte a byte**.

## Declarar canales — `ChannelDef`

Un canal no se construye directamente, sino que se da de alta mediante un
método `add…Channel` del escritor, que internamente crea un `ChannelDef` y
devuelve el **índice de canal** (secuencial desde 0) que utilizan todas las
llamadas de escritura. El `ChannelDef` (un `record`) describe el canal tal
como figura en el metabloque:

```java
public record ChannelDef(
    int index,               // Kanalindex (0..65535), vom Block-Strom referenziert
    String name,             // vollqualifizierter Kanalname (Pflicht)
    DataType dataType,       // aufgelöster Datentyp (Pflicht)
    ChannelType channelType, // Datenform: SCALAR/VECTOR/MATRIX/BINARY
    int sizeOfLengthValue,   // Breite des Längenpräfix: 2 oder 4
    long timeIncrementNs,    // äquidistante Periode in ns; 0 = timestamped
    String physicalUnit,     // physikalische Einheit oder null
    Map<String,String> attributes) { }  // z. B. displayname, comment, reference
```

Dos formas de declaración por escritor:

```java
// Timestamped-Kanal (jedes Sample trägt seinen eigenen Zeitstempel):
int rpm = writer.addTimestampedChannel("motor.drehzahl", DataType.DOUBLE, 2);
int evt = writer.addTimestampedChannel("ereignis", DataType.STRING, 2,
                                       "1/min", Map.of("displayname", "Ereignis"));

// Äquidistanter Kanal (feste Rate; nur der Segmentstart trägt einen Zeitstempel):
int sig = writer.addEquidistantChannel("beschleunigung", DataType.DOUBLE, 4,
                                       1000.0 /* Hz */);
```

Se rechazan con `IllegalArgumentException`: nombre vacío, tipo de datos
`null` o `UNSUPPORTED`, `sizeOfLengthValue` ≠ 2/4, un canal equidistante con
un tipo distinto de `FLOAT`/`DOUBLE`, así como una frecuencia de muestreo no
positiva o no finita. En el `StreamingWriter`, cada llamada a
`add…Channel` tras `begin()` provoca una `OsfException` (la fase de
configuración ya ha terminado). El `channeltype` se normaliza siempre a
`scalar` al escribir — la equidistancia la aporta únicamente el
`timeincrement`, no el `channeltype`.

### Elegir correctamente `sizeOfLengthValue`

El campo de longitud de cada bloque tiene 2 o 4 bytes de ancho y limita el
tamaño del bloque (~64 KB o ~2 GB). Reglas prácticas:

- **Canales String/Binary con muestras grandes** (imágenes, audio, blobs):
  en el `StreamingWriter` es obligatorio declarar `4` — no puede cambiar el
  valor después de `begin()`. El `BlockWriter` aumenta automáticamente de 2 a
  4 en un canal variable que puede dimensionar por sí mismo (auto-bump al
  emitir); allí `2` es siempre un valor inicial correcto.
- **Los canales numéricos** nunca se cambian — en su lugar se dividen en más
  bloques. En el `StreamingWriter`, `4` evita en canales de alta frecuencia
  el chunking en muchos bloques pequeños, cada uno con su propio fsync.
- En los demás casos, mantenga el valor predeterminado `2` (bloques más
  compactos).

## `StreamingWriter` — embebido, a prueba de fallos

```java
import com.optimeas.osf.*;

try (StreamingWriter w = StreamingWriter.create(Path.of("aufzeichnung.osf"))) {
    w.setMetadata("creator", "logger-fw/3.2");   // Metadaten vor begin()
    w.setMetadata("tag", "pruefstand-7");

    int rpm = w.addTimestampedChannel("motor.drehzahl", DataType.DOUBLE, 2);
    int gps = w.addTimestampedChannel("fahrzeug.gps", DataType.GPS_LOCATION, 2);
    w.begin();                                    // Header + Metablock auf Platte, fsync

    while (running) {
        w.writeSample(rpm, nowNs(), readRpm());   // je Block: kodieren, schreiben, fsync
    }
}                                                 // close() emittiert Restblöcke + fsync
```

Garantías y comportamiento:

- **Cada `writeSample` que retorna está en disco como bloque completo**
  (`FileChannel.force(true)` = fsync). Tras un corte de corriente, el archivo
  sigue siendo legible hasta el último bloque confirmado; el lector
  best-effort recupera todos los bloques anteriores al corte y marca un resto
  truncado mediante `ReaderStats.truncationSeen()` (véase
  [Lectura](./reading.md)).
- **Preámbulo lazy o eager:** `begin()` escribe una vez la línea del Magic
  Header (`OSF5 <len>\n`) y el metabloque JSON, y les aplica fsync; si no se
  invoca explícitamente, ocurre automáticamente con la primera muestra. A
  partir de entonces el metabloque no se vuelve a tocar — **los setters de
  metadatos solo surten efecto antes**.
- **`created_utc`** se sella automáticamente en `begin()` si no se ha
  establecido (ISO-8601 UTC, p. ej. `2026-07-11T08:30:00Z`).
- **Los canales quedan fijados a una familia de bloques:** quien escribe el
  mismo canal una vez con marca de tiempo y otra como equidistante obtiene
  una `OsfException`.
- `close()` es idempotente y emite los bloques restantes que pudiera haber en
  búfer; al ser `Closeable`, el escritor debe usarse en un
  try-with-resources. **No es thread-safe** — serialice los accesos desde
  fuera.

### Familias de escritura

```java
// Timestamped numerisch — Einzel- und Batch-Überladungen:
w.writeSample(ch, tsNs, 3.14);                        // double
w.writeSample(ch, tsNs, 42L);                         // beliebiger Integer-Kanal
w.writeSamples(ch, tsArray, werteArray);              // parallele Arrays (Bulk)

// GPS (eigene Überladung):
w.writeSample(ch, tsNs, new GpsLocation(lat, lon, alt));

// String / Binary (ein Sample pro Block; OSF5: kein 0x00-Terminator):
w.writeSample(ch, tsNs, "Ereignis: Tür offen");
w.writeSample(ch, tsNs, jpegBytes);                   // byte[]

// Äquidistant (nur float/double; Rate stammt aus addEquidistantChannel):
w.startEquidistantSegment(ch, t0Ns, daten);           // öffnet ein Segment
w.appendEquidistantSamples(ch, weitere);              // verlängert das offene Segment
```

`writeSample(int, long, long)` atiende cualquier canal entero (`int8…int64`,
`uint8…uint64`); el valor se estrecha al ancho del canal al codificarlo.
Cada llamada por lotes y cada llamada de segmento se divide automáticamente
(chunking) según la capacidad de bloque del canal — un fsync por bloque
emitido. Un nuevo `startEquidistantSegment` cierra el anterior y abre
deliberadamente un **nuevo** segmento; las lagunas entre segmentos son el
medio conforme a la especificación para las pausas de grabación.

## `BlockWriter` — recopilar y emitir

```java
BlockWriter w = new BlockWriter();
w.setMetadata("creator", "analyse-tool/1.0");

int ch  = w.addTimestampedChannel("messwert", DataType.DOUBLE);   // Auto-Bump-Form
int sig = w.addEquidistantChannel("signal", DataType.DOUBLE, 4, 100.0);
w.startEquidistantSegment(sig, t0Ns, samples);
w.writeSample(ch, tsNs, 42.0);

w.writeToFile(Path.of("ergebnis.osf"));

var mem = new java.io.ByteArrayOutputStream();   // oder ein beliebiger OutputStream
w.writeTo(mem);                                  // dieselbe Instanz erneut emittieren
```

- La familia `writeSample` / `startEquidistantSegment` replica la del
  escritor de streaming (mismos tipos, misma validación), pero solo recopila
  en memoria; la división en bloques conformes a la especificación se
  produce únicamente al emitir.
- `writeToFile` / `writeTo` pueden invocarse **varias veces** — la misma
  instancia puede escribirse a la vez en un archivo y en la red.
- **Auto-bump:** un canal variable dado de alta con la forma de dos
  argumentos `addTimestampedChannel(name, type)` comienza con
  `sizeOfLengthValue = 2` y, para la emisión, se eleva a `4` si su muestra
  más grande excede el campo de longitud u16. La forma de tres argumentos con
  un valor explícito se respeta y, en su lugar, rechaza una muestra
  demasiado grande (como el StreamingWriter, que no puede elevarlo). Los
  canales numéricos nunca se elevan.
- Sin fsync — la durabilidad corre a cargo de quien llama.
- `channelCount()` y `channelIndex("name")` ayudan cuando no se llevan los
  índices (`channelIndex` devuelve `-1` si el nombre es desconocido).

## Valores predeterminados automáticos de metadatos

Ambos escritores escriben las entradas establecidas con
`setMetadata(key, value)` **literalmente** en el objeto `osf.file` del
metabloque. La única intervención automática:

| Campo | Comportamiento si no se ha establecido |
|---|---|
| `created_utc` | **siempre** sellado (hora UTC actual, `YYYY-MM-DDTHH:MM:SSZ`) — mediante `putIfAbsent`, un valor ya existente permanece intacto |
| todos los demás (`creator`, `tag`, …) | solo se escriben si se han establecido; nunca como `null` |

Es decir, no hay ningún valor predeterminado forzado para `creator` ni para
`tag` — lo que no se establece no aparece en el metabloque.

## Perfil de integridad al escribir (`crc`)

Ambos escritores pueden generar opcionalmente el perfil de integridad de
OSF5 de nivel `crc` — de forma predeterminada está desactivado
(`IntegrityProfile.NONE`):

```java
w.setIntegrity(IntegrityProfile.CRC32C);   // vor begin() bzw. writeTo
```

Una vez activado, el escritor escribe

- un token `crc32c` en la línea del Magic Header, que lleva el **CRC del
  metabloque**, y
- por cada bloque de datos, un **CRC32C de trama** añadido (4 bytes, contados
  en el campo de longitud del bloque). En el `StreamingWriter`, el CRC de
  trama se hace duradero con el mismo `force(true)` que el propio bloque.

La suma de verificación es `java.util.zip.CRC32C` (nativa del JDK). Los 4
bytes del CRC de trama consumen parte del presupuesto de carga útil de cada
bloque, de modo que caben algunas muestras menos por bloque — la aritmética
de chunking lo tiene en cuenta automáticamente. Cómo verifica el lector estos
valores de comprobación en modo fail-closed se explica en
[Lectura](./reading.md) y [Gestión de errores](./error-handling.md).

El nivel de firma (`IntegrityProfile.ED25519`) **no** está admitido por el
escritor y se rechaza con una `OsfException` al escribir el preámbulo.

## Round-trip y conversión

Volver a escribir un `DataManager` cargado — al mismo tiempo, la conversión
OSF4 → OSF5 u OSFZ → OSF5:

```java
DataManager mgr = DataManager.loadFromFile(Path.of("alt.osf"));  // auch OSF4 / OSFZ
BlockWriter.fromManager(mgr).writeToFile(Path.of("neu.osf"));     // immer OSF5
```

`BlockWriter.fromManager(mgr)` construye un escritor a partir de los canales
y muestras tipados del gestor; quien quiera filtrar o renombrar antes de
escribir continúa trabajando sobre el escritor devuelto.

Se conservan: nombres de canal, tipos de datos, valores de muestra (bit a
bit), límites de segmento y los metadatos del archivo — **incluido
`created_utc`**, pues un valor cargado ya está establecido y no se vuelve a
sellar. **No** se conserva el `sizeOfLengthValue` (el escritor comienza en 2
y eleva los canales variables cuando es necesario); el índice de canal se
reasigna según la posición en la lista.

## Lo que los escritores deliberadamente no hacen

- **Sin salida OSF4** — OSF5 es el único formato de escritura.
- **Sin salida OSFZ** — la biblioteca lee de forma transparente los archivos
  empaquetados con gzip, pero no comprime por sí misma; la compresión es un
  paso posterior.
- **Sin marcas de tiempo relativas** — el formato de tiempo relativo es un
  legado de lectura de OSF4; los escritores emiten marcas de tiempo
  absolutas.
- **Sin validación de marcas de tiempo** — según la especificación no se
  exige monotonía y no se impone.
- **Sin firma** — el nivel Ed25519 se rechaza.
- **Sin seguridad entre hilos** — serialice desde fuera los accesos a una
  instancia de escritor.

Los detalles de tramado y chunking en los que se apoyan ambos escritores los
describe el capítulo [Aspectos internos](./internals.md); la arquitectura
general y las herramientas se encuentran en
[Arquitectura](./architecture.md), [Herramientas](./tools.md) y
[Compilación](./building.md). El [Recetario](./cookbook.md) recoge ejemplos
para principiantes; la definición vinculante del formato la proporciona el
capítulo [Formato OSF](../../osf_general.md), y el resumen, la
[implementación en Java](../java.md).

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
