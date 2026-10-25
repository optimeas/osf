---
title: Recetario
description: Recetas para tareas habituales con la biblioteca Java de OSF — desde la inspección de archivos y la exportación a CSV hasta el bucle de grabación embebido a prueba de fallos
sidebar_position: 7
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - Ejemplos
  - Recetario
  - Recetas
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Recetario

Recetas compactas y listas para copiar para la biblioteca Java. Todas
presuponen el módulo JPMS `com.optimeas.osf` en la ruta de módulos (Java 21)
e importan los tipos públicos del paquete `com.optimeas.osf`:

```java
import com.optimeas.osf.*;
import java.nio.file.Path;
```

La gestión de errores se ha reducido al mínimo. Las API de lectura y
escritura notifican los errores mediante la `OsfException` no comprobada
(véase [Gestión de errores](./error-handling.md)); para una visión de
conjunto de las clases, véase [Arquitectura](./architecture.md),
[Lectura](./reading.md) y [Escritura](./writing.md).

## Inspeccionar un archivo (metadatos + lista de canales)

`loadFromFile` reconoce de forma transparente OSF4, OSF5 y **OSFZ**
comprimido (gzip/zlib) por los primeros bytes — la misma llamada para
`.osf` y `.osfz`.

```java
DataManager mgr = DataManager.loadFromFile(Path.of(pfad));   // .osf oder .osfz

System.out.println("version:  " + mgr.version());            // OSF4 / OSF5
System.out.println("creator:  " + mgr.metadata().getOrDefault("creator", "-"));
System.out.println("created:  " + mgr.metadata().getOrDefault("created_utc", "-"));
System.out.println("channels: " + mgr.channels().size());

for (DataChannel ch : mgr.channels()) {
    System.out.printf("  [%d] %s  (%d Samples, Einheit: %s)%n",
            ch.index(), ch.name(), ch.sampleCount(),
            ch.physicalUnit() == null ? "-" : ch.physicalUnit());
}
```

## Obtener un canal como valores `double` con marcas de tiempo

`asDoubles()` amplía cualquier tipo de datos numérico a `double`;
`timestampsNs()` devuelve el array paralelo de marcas de tiempo — en los
canales equidistantes se reconstruye a partir del instante inicial y la
frecuencia de muestreo, en los canales con marca de tiempo es explícito.
Ambos arrays tienen la misma longitud.

```java
DataChannel ch = mgr.channelByName("Motor.Drehzahl").orElseThrow();

long[]   ts = ch.timestampsNs();
double[] v  = ch.asDoubles();          // wirft OsfException.UnsupportedType, wenn nicht numerisch

for (int i = 0; i < v.length; i++) {
    long   tNs   = ts[i];
    double wert  = v[i];
    // … tNs, wert verarbeiten
}
```

`channelByName` devuelve un `Optional<DataChannel>`; como alternativa,
`channelByIndex(int)` accede mediante el índice del metabloque.

## Iterar de forma genérica sobre tipos de datos mixtos

Un `switch` sobre `dataType()` hace que las herramientas de exportación
sean independientes del tipo de datos. Cada acceso `as…()` lanza
`OsfException.UnsupportedType` si no corresponde al canal — el `switch` lo
evita.

```java
long[] ts = ch.timestampsNs();
switch (ch.dataType()) {
    case DOUBLE, FLOAT, INT8, INT16, INT32, INT64,
         UINT8, UINT16, UINT32, UINT64, BOOL -> {
        double[] v = ch.asDoubles();
        for (int i = 0; i < v.length; i++) row(ts[i], v[i]);
    }
    case GPS_LOCATION -> {
        GpsLocation[] g = ch.asGps();
        for (int i = 0; i < g.length; i++) row(ts[i], g[i].latitude(), g[i].longitude());
    }
    case STRING -> {
        String[] s = ch.asStrings();
        for (int i = 0; i < s.length; i++) row(ts[i], s[i]);
    }
    case BINARY -> {
        byte[][] b = ch.asBinaries();
        for (int i = 0; i < b.length; i++) row(ts[i], b[i].length + " Bytes");
    }
    default -> { /* UNSUPPORTED: Kanal geladen, aber Werte nicht projizierbar */ }
}
```

## Exportación mínima a CSV

```java
import java.io.BufferedWriter;
import java.nio.file.Files;

DataChannel ch = mgr.channelByName(name).orElseThrow();
long[]   ts = ch.timestampsNs();
double[] v  = ch.asDoubles();

try (BufferedWriter w = Files.newBufferedWriter(Path.of("kanal.csv"))) {
    w.write("timestamp_ns,value\n");
    for (int i = 0; i < v.length; i++) {
        w.write(ts[i] + "," + v[i] + "\n");
    }
}
```

Una tabla ancha (una columna por canal, una fila por marca de tiempo) la
genera la herramienta `osf-cli` con `osf convert --to csv` — véase
[Herramientas](./tools.md).

## Convertir OSF4 → OSF5 (también con entrada OSFZ)

`BlockWriter.fromManager` reconstruye un escritor a partir de un archivo
cargado — metadatos, definiciones de canal y todas las muestras. La salida
es **siempre OSF5**, con independencia del formato de origen; las muestras
se conservan bit a bit.

```java
DataManager mgr = DataManager.loadFromFile(Path.of("alt_osf4.osf")); // oder .osfz
BlockWriter.fromManager(mgr).writeToFile(Path.of("neu_osf5.osf"));
```

Para una **salida OSFZ**, escriba el `BlockWriter` en un `GZIPOutputStream`:

```java
import java.util.zip.GZIPOutputStream;

try (var os = new GZIPOutputStream(Files.newOutputStream(Path.of("neu.osfz")))) {
    BlockWriter.fromManager(mgr).writeTo(os);
}
```

## Escribir un archivo nuevo con datos de análisis

El `BlockWriter` recopila todas las muestras en memoria y escribe el archivo
de una sola vez. Los canales se declaran primero (el valor devuelto es el
índice del canal) y después se rellenan.

```java
BlockWriter w = new BlockWriter();
w.setMetadata("creator", "mein-tool/1.0");

int fftPeak = w.addTimestampedChannel("ergebnis.fft_peak", DataType.DOUBLE);

for (var e : ergebnisse) {
    w.writeSample(fftPeak, e.timestampNs(), e.peakHz());
}

w.writeToFile(Path.of("ergebnis.osf"));   // created_utc wird automatisch gesetzt
```

Para series de análisis equidistantes (frecuencia fija, solo
`float`/`double`) en lugar de con marca de tiempo:

```java
int spektrum = w.addEquidistantChannel("ergebnis.psd", DataType.DOUBLE, 2, 1000.0); // 1 kHz
w.startEquidistantSegment(spektrum, startNs, block1);   // double[]
w.appendEquidistantSamples(spektrum, block2);           // hängt an dasselbe Segment an
```

## Bucle de grabación embebido a prueba de fallos

El `StreamingWriter` escribe de inmediato el preámbulo y cada bloque
terminado, y llama a `force(true)` (fsync) tras cada bloque. La durabilidad
es, por tanto, **por bloque**: tras un corte de corriente, el `DataManager`
lee el archivo hasta el último bloque completo y establece
`stats().truncationSeen()` para los últimos bytes truncados. Implementa
`Closeable` — `try`-with-resources escribe los bloques restantes, ejecuta
force y cierra.

```java
try (StreamingWriter w = StreamingWriter.create(Path.of("/data/rec_0001.osf"))) {
    w.setMetadata("creator", "logger-fw/3.2");

    int temp = w.addTimestampedChannel("temp", DataType.DOUBLE, 2, "degC", null);
    int tuer = w.addTimestampedChannel("tuer", DataType.BOOL, 2);   // Event-Kanal
    w.begin();   // Präambel früh festschreiben (sonst lazy beim ersten Sample)

    while (laeuft) {
        long now = jetztNs();

        if (neuerMesswert)  w.writeSample(temp, now, wert);
        if (tuerGeaendert)  w.writeSample(tuer, now, offen);

        // Optional: w.flush() erzwingt die noch offenen Teilblöcke sofort.
        warteAufNaechstenTick();
    }
}   // close(): Restblöcke schreiben, force, schließen
```

El `StreamingWriter` fija `sizeoflengthvalue` por canal y no puede
aumentarlo posteriormente — por tanto, declare directamente con `4` los
canales con muestras variables grandes (véase más abajo).

## Imágenes/blobs como canal binario

Las muestras variables grandes (JPEG, búferes en bruto) superan fácilmente
el campo de longitud de 2 bytes; por eso el canal se crea con
`sizeoflengthvalue = 4`. Mediante el mapa de atributos se puede escribir un
`mimetype` en el metabloque. Las muestras variables nunca se agrupan — un
bloque por muestra.

```java
// Schreiben (StreamingWriter — sov=4 wegen Sample-Größe):
int kamera = w.addTimestampedChannel(
        "kamera.snapshots", DataType.BINARY, 4,
        null, java.util.Map.of("mimetype", "image/jpeg"));

w.writeSample(kamera, tsNs, jpegBytes);   // byte[]
```

```java
// Lesen:
DataChannel ch = mgr.channelByName("kamera.snapshots").orElseThrow();
long[]   ts    = ch.timestampsNs();
byte[][] blobs = ch.asBinaries();

for (int i = 0; i < blobs.length; i++) {
    long   t    = ts[i];
    byte[] jpeg = blobs[i];
    // … jpeg speichern/decodieren
}
```

El `BlockWriter` conoce de antemano el tamaño máximo de muestra: allí basta
la forma abreviada `addTimestampedChannel(name, DataType.BINARY)`, que
aumenta `sizeoflengthvalue` de 2 a 4 automáticamente cuando es necesario.

## Escribir y comprobar el perfil de integridad `crc`

Ambos escritores pueden generar el nivel `crc`: un CRC32C sobre el
metabloque en el Magic Header más un CRC32C de trama por bloque.

```java
BlockWriter w = new BlockWriter();
w.setIntegrity(IntegrityProfile.CRC32C);
// … Kanäle + Samples …
w.writeToFile(Path.of("gesichert.osf"));
```

Al leer, el `DataManager` verifica en modo fail-closed: un CRC del
metabloque incorrecto lanza `OsfException.MetablockCrcMismatch`, y los
bloques defectuosos se omiten y se contabilizan.

```java
DataManager mgr = DataManager.loadFromFile(Path.of("gesichert.osf"));
ReaderStats s = mgr.stats();

System.out.println(s.verificationStatus());   // "crc_valid" oder "invalid"
if (s.blocksCrcFailed() > 0) {
    System.out.println("ACHTUNG: " + s.blocksCrcFailed() + " Blöcke mit CRC-Fehler");
}
```

## Mostrar las estadísticas de lectura

```java
ReaderStats s = mgr.stats();
System.out.printf("Blöcke gelesen: %d%n", s.blocksRead());
if (s.truncationSeen()) {
    System.out.println("ACHTUNG: Datei war abgeschnitten");
}
if (s.compressed()) {
    System.out.println("Quelle war OSFZ (" + s.compressionFormat() + ")");
}
```

Los detalles sobre los aspectos internos (flujo de bloques, ensamblador,
chunking) se encuentran en [Aspectos internos](./internals.md); la
compilación y las coordenadas de Maven, en [Compilación](./building.md). El
resumen de todas las herramientas se encuentra en la
[página de inicio de Java](../java.md).

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
