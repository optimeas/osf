---
title: Lectura
description: Lectura de archivos OSF y OSFZ en Java — DataManager, DataChannel, el flujo de bloques, ReaderStats y descompresión transparente
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - DataManager
  - DataChannel
  - OSFZ
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Lectura

La implementación en Java lee **OSF4**, **OSF5** y, de forma transparente,
**OSFZ** (gzip/zlib) con la misma API. Además lee el perfil de integridad
`crc` y comprueba cada suma de verificación
([Gestión de errores](./error-handling.md)). El punto de entrada público es
exactamente una clase:

- **`com.optimeas.osf.DataManager`** — la vía de lectura estándar y la única
  pública. Carga el archivo por completo, compone canales tipados a partir
  del flujo de bloques y resuelve todos los límites de bloque. Para análisis,
  exportación y herramientas.

El decodificador del flujo de bloques propiamente dicho trabaja por debajo,
en el paquete no exportado `com.optimeas.osf.internal` — es un detalle de
implementación del `DataManager` y no una superficie pública (véase la
sección [El nivel del flujo de bloques](#die-blockstrom-ebene)).

## Inicio rápido

```java
import com.optimeas.osf.DataManager;
import com.optimeas.osf.DataChannel;
import java.nio.file.Path;

DataManager mgr = DataManager.loadFromFile(Path.of("messung.osf")); // auch .osfz

// Alle Kanäle auflisten (Metablock-Reihenfolge)
for (DataChannel ch : mgr.channels()) {
    System.out.printf("%-30s %-12s %d Samples%n",
            ch.name(), ch.dataType(), ch.sampleCount());
}

// Einen Kanal über den Namen ansprechen (primäre Zugriffsform)
mgr.channelByName("Sensor.Temperatur").ifPresent(ch -> {
    double[] werte     = ch.asDoubles();     // Werte, auf double geweitet
    long[]   zeitstamp = ch.timestampsNs();  // parallele Zeitstempel (ns)
    // …
});
```

## `DataManager`

### Carga

| Método | Fuente | Notas |
|---|---|---|
| `DataManager.loadFromFile(Path)` | Archivo | OSF, OSFZ; abre y cierra el stream por sí mismo |
| `DataManager.load(InputStream)` | `InputStream` cualquiera | se consume por completo; debe estar situado al inicio del archivo |

Ambas vías recorren el mismo pipeline: detección de OSFZ → Magic Header →
(con `crc`, la suma de verificación del metabloque) → analizador del
metabloque (JSON para OSF5, XML/StAX para OSF4) → decodificador del flujo de
bloques hasta EOF → ensamblado de canales. El resultado es inmutable y puede
ser leído simultáneamente por cualquier número de hilos.

### Acceso

```java
mgr.version();                 // OsfVersion — OSF4 oder OSF5, aus dem Magic-Header
mgr.metadata();                // Map<String,String> — Datei-Metadaten aus dem "file"-Block
mgr.stats();                   // ReaderStats — Telemetrie des Ladevorgangs
mgr.channels();                // List<DataChannel> — Metablock-Reihenfolge
mgr.channelByName("a.b.c");    // Optional<DataChannel> — leer, wenn unbekannt
mgr.channelByIndex(7);         // Optional<DataChannel> — Index aus dem Metablock
```

`channelByName` es la forma de acceso principal; `channelByIndex` es una
comodidad. Ambos devuelven un `Optional` vacío en lugar de un error, porque
«el canal no existe» es un caso normal al explorar archivos ajenos. Si un
nombre está asignado dos veces, prevalece la **primera** definición.

Las claves de `metadata()` corresponden exactamente a los nombres de campo
de transmisión del bloque `file` (por ejemplo `creator`, `created_utc`,
`tag`, `reason`).

### Qué puede ocurrir durante la carga

- **Archivo truncado:** ninguna excepción. Todos los bloques completamente
  legibles acaban en los canales, `mgr.stats().truncationSeen() == true`.
- **Tipo de datos (futuro) desconocido:** el canal se **omite** de
  `channels()` (sus bloques se omitieron a nivel del lector). En cambio, un
  *tipo de canal* desconocido (la forma de los datos) conserva el canal — la
  legibilidad depende únicamente del tipo de datos y de los tipos de bloque,
  nunca del `channeltype`.
- **Error de suma de verificación del metabloque** (solo con `crc`): la carga
  se interrumpe en modo **fail-closed** con
  `OsfException.MetablockCrcMismatch` — un metabloque manipulado nunca se
  analiza.
- **Errores estructurales:** una cabecera o un metabloque defectuosos, una
  longitud de metabloque superior a `Integer.MAX_VALUE` o un error de E/S
  interrumpen la carga con `OsfException.MalformedFile` — véase
  [Gestión de errores](./error-handling.md).

## `DataChannel` — los canales tipados

`DataChannel` es **una** clase; su disposición en memoria se distingue
mediante `kind()`, a través del enum `DataChannel.Kind`:

```java
enum Kind { EQUIDISTANT, TIMESTAMPED, VARIABLE }
```

Los límites de bloque en disco están resueltos; las muestras aparecen como
una única secuencia plana con marcas de tiempo absolutas paralelas.

### Metadatos comunes

```java
ch.index();          // int    — On-Disk-Kanalindex aus dem Metablock
ch.name();           // String — vollqualifizierter Name
ch.dataType();       // DataType — aufgelöster Datentyp der Samples
ch.channelType();    // ChannelType — Datenform (scalar/vector/matrix/binary)
ch.physicalUnit();   // String — physikalische Einheit, oder null
ch.kind();           // DataChannel.Kind — Speicherlayout
ch.sampleCount();    // long   — Anzahl Samples (Summe über alle Segmente)
ch.timestampsNs();   // long[] — absolute Zeitstempel, parallel zu den Werten
ch.segments();       // List<DataChannel.Segment> — nur bei EQUIDISTANT belegt
```

`timestampsNs()` devuelve el array de respaldo propio del canal — no lo
modifique.

### `EQUIDISTANT` — segmentos en lugar de una marca de tiempo por muestra

Los canales equidistantes **no almacenan una marca de tiempo por muestra**.
En su lugar llevan una secuencia plana de valores más una lista de
segmentos. Cada bloque `bcStartData` del archivo abre un segmento y cada
bloque `bcContinuedData` siguiente prolonga el segmento más reciente:

```java
public record Segment(long startTimestampNs, double sampleRateHz,
                      int startIndex, int sampleCount) {}
```

`timestampsNs()` **reconstruye** las marcas de tiempo: la muestra `i` de un
segmento se sitúa en `startTimestampNs + (long)(i * 1e9 / sampleRateHz)`
(truncada hacia cero, suma saturante). Las lagunas **entre** segmentos no se
interpolan — cada segmento comienza en su propio `startTimestampNs`; una
pausa de grabación sigue siendo una pausa.

```java
DataChannel ch = mgr.channelByName("Beschleunigung.X").orElseThrow();
for (DataChannel.Segment seg : ch.segments()) {
    // seg.startTimestampNs(), seg.sampleRateHz(), seg.startIndex(), seg.sampleCount()
}
double[] werte = ch.asDoubles();      // flacher Lauf über alle Segmente
long[]   zeit  = ch.timestampsNs();   // dazu passende, rekonstruierte Zeitstempel
```

### `TIMESTAMPED` — secuencias paralelas

Canales numéricos o GPS con marcas de tiempo explícitas: `timestampsNs()` y
la secuencia de valores discurren en paralelo. Los bloques
`bcAbsTimeStampData` acaban directamente aquí; los deltas
`bcContinuedRelStampData` de OSF4 se suman durante la carga para dar marcas
de tiempo absolutas (ancla = última marca de tiempo absoluta observada del
canal).

### `VARIABLE` — String y Binary

Canales String y Binary: siempre mediante `bcAbsTimeStampData` con una marca
de tiempo por muestra.

```java
String[] texte = ch.asStrings();     // string-Kanal
byte[][] blobs = ch.asBinaries();    // binary-Kanal
```

El tratamiento del terminador nulo es determinista según la versión: en
**OSF4** el lector ya ha recortado el último byte; en **OSF5** la carga útil
llega sin modificar.

### Accesores tipados

Cada accesor proyecta los valores almacenados en un array nuevo y lanza
`OsfException.UnsupportedType` si el `dataType()` no encaja:

| Accesor | Retorno | válido para |
|---|---|---|
| `asDoubles()` | `double[]` | cualquier tipo numérico (bool→0/1, todos los anchos de entero, float, double) |
| `asLongs()` | `long[]` | tipos enteros `int8`…`int64`, `uint8`…`uint64`, `bool` (los sin signo se extienden con ceros) |
| `asBooleans()` | `boolean[]` | solo `bool` |
| `asStrings()` | `String[]` | solo `string` |
| `asBinaries()` | `byte[][]` | solo `binary` |
| `asGps()` | `GpsLocation[]` | solo `gpslocation` |

Con `uint64`, `asLongs()` devuelve los bits en bruto — para la salida de
texto utilice `Long.toUnsignedString(...)`. `GpsLocation` es un record
compuesto por `latitude`, `longitude` (grados) y `altitude` (metros).

### Tipos de datos {#datentypen}

`DataType` abarca `bool`, `int8`…`int64`, `uint8`…`uint64`, `float`,
`double`, `string`, `binary` y `gpslocation`; `bytearray` se acepta al leer
como alias de `binary`. Un tipo de datos desconocido, pero no eliminado, pasa
a ser `DataType.UNSUPPORTED` (el archivo se carga, el canal se omite); los
tipos `pair`, `triple`, `candata` y `gpsdata`, **eliminados** del estándar
OSF, provocan deliberadamente `OsfException.UnsupportedType` al resolverse.

## El nivel del flujo de bloques {#die-blockstrom-ebene}

Por debajo del `DataManager`, un lector interno del flujo de bloques
(`com.optimeas.osf.internal.BlockReader`) decodifica los bloques binarios que
siguen al metabloque. Este paquete **no** se exporta desde el módulo JPMS —
es decir, en esta versión no existe ninguna API de streaming pública; en el
lado de la aplicación, el `DataManager` es el único punto de entrada. Aun
así conviene conocer su comportamiento, porque explica la telemetría en
`ReaderStats`:

- **Truncamiento best-effort:** un último bloque corto o corrupto termina la
  lectura en silencio — todo lo decodificado antes se conserva y se activa
  `stats().truncationSeen()`. Nunca se lanza una excepción por truncamiento.
- **Los bloques omitidos siguen siendo visibles — solo a través de
  `ReaderStats`:** los bytes de control deprecated
  (`blocksSkippedDeprecatedType()`) o reservados
  (`blocksSkippedReservedType()`), así como los bloques `bcStatusEvent`
  (`blocksSkippedStatusEvent()`), se descartan según su longitud sin
  analizarlos y se contabilizan. `ReaderStats` es el *único* lugar donde
  puede observarse un suceso así: `com.optimeas.osf.internal` no está
  exportado, por lo que los valores `Block.Skipped` subyacentes nunca llegan
  al código de la aplicación. Los bloques de canales de tipo `UNSUPPORTED`
  se descartan igualmente, pero **todavía no** los contabiliza ningún campo
  de `ReaderStats`.
- **Tráiler de OSF4:** el bloque de información opcional `0xFFFF` junto con
  el tráiler de 40 bytes se consume en silencio.
- **Integridad:** con el perfil `crc` activo, cada bloque lleva un CRC32C
  final sobre la trama completa; se comprueba antes del análisis tipado
  (fail-closed). Un fallo omite el bloque e incrementa `blocksCrcFailed()`.
  Los bloques de firma en el canal reservado `0xFFFE` se omiten y se
  contabilizan, para que un archivo firmado siga siendo legible.

El lector del flujo de bloques **no** descomprime por sí mismo — las
entradas OSFZ se descomprimen antes (véase más abajo).

## OSFZ transparente

`loadFromFile("x.osfz")` funciona sin ninguna acción adicional: antes del
Magic Header, la cadena de lectura comprueba los dos primeros bytes y, si es
necesario, antepone un descompresor. Se reconocen gzip (`1F 8B`) y zlib
(`78` seguido de `01`/`5E`/`9C`/`DA`); un OSF auténtico comienza con
`O` = `0x4F`, por lo que nunca colisiona. La descompresión utiliza los
medios propios del JDK `java.util.zip` (`GZIPInputStream` o
`InflaterInputStream`) y trabaja en streaming. El hallazgo se documenta en
`stats().compressed()` y `stats().compressionFormat()` (`"gzip"` o
`"zlib"`); para archivos sin comprimir, la etiqueta permanece en `"none"`.

## `ReaderStats` — telemetría

Tras cada carga, mediante `mgr.stats()`:

| Campo | Significado |
|---|---|
| `blocksRead()` | Número de bloques completamente decodificados |
| `truncationSeen()` | El flujo terminó en un bloque parcial/corrupto |
| `compressed()` / `compressionFormat()` | Detección de OSFZ (`"none"` / `"gzip"` / `"zlib"`) |
| `integrity()` | Perfil de integridad declarado por la cabecera (`NONE` / `CRC32C` / `ED25519`) |
| `blocksCrcFailed()` | Bloques de datos cuyo CRC32C de trama no se verificó (omitidos) |
| `blocksSignatureSkipped()` | Bloques de firma omitidos (canal reservado `0xFFFE`) |
| `blocksSkippedZeroLength()` | Bloques omitidos por tener el campo de longitud a `0` |
| `blocksSkippedStatusEvent()` | Bloques `bcStatusEvent` omitidos (byte de control 3) |
| `blocksSkippedReservedType()` | Bloques omitidos con byte de control reservado (0, 2, ≥9), así como las dos variantes no especificadas de `bcMessageEvent` |
| `blocksSkippedDeprecatedType()` | Bloques omitidos con byte de control deprecated (`bcTrustedTimestamp`, byte de control 1) |
| `verificationStatus()` | Estado de comprobación resumido (véase más abajo) |

`verificationStatus()` resume el resultado de integridad en una cadena de
texto:

- `"none"` — sin perfil de integridad;
- `"crc_valid"` — nivel `crc`, todos los CRC de bloque se verificaron;
- `"invalid"` — nivel `crc`, al menos un bloque falló en el CRC;
- `"signature_unverifiable"` — un archivo firmado cuyas firmas este lector
  `crc` no puede comprobar.

```java
ReaderStats s = mgr.stats();
System.out.printf("%d Blöcke, Integrität=%s, komprimiert=%s (%s)%n",
        s.blocksRead(), s.verificationStatus(),
        s.compressed(), s.compressionFormat());
```

## Notas de rendimiento

- El `DataManager` mantiene todas las muestras en memoria, en arrays
  primitivos (`double[]`, `long[]`, …) sin boxing. Como regla general, un
  archivo necesita aproximadamente su tamaño descomprimido en RAM. No existe
  ninguna API de streaming pública — por ello, los volúmenes muy grandes se
  procesan mejor archivo a archivo o filtrando antes de la carga.
- Los accesores tipados (`asDoubles()`, `asLongs()`, …) **copian** en un
  array nuevo en cada llamada. En bucles, conserve el resultado una sola vez
  en una variable local en lugar de llamar repetidamente al accesor.
- Los archivos de campo reales en el rango de un solo dígito de MB se cargan
  en milisegundos. La descompresión OSFZ transparente trabaja en streaming y
  no necesita un segundo búfer para todo el archivo.

Continúe con la [Escritura](./writing.md), la
[Gestión de errores](./error-handling.md), las
[Herramientas](./tools.md) o la [Arquitectura](./architecture.md); el
formato binario en sí lo describe la
[especificación de OSF](../../osf_general.md).

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
