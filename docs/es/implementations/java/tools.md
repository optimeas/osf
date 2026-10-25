---
title: Herramientas — CLI y visor
description: Las herramientas Java incluidas osf-cli (línea de comandos picocli con info/channels/dump/convert) y osf-viewer (trazador multicanal JavaFX con diezmado mín./máx. por píxel)
sidebar_position: 5
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - osf-cli
  - osf-viewer
  - picocli
  - JavaFX
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Herramientas — CLI y visor

Además de la biblioteca, el reactor de Java incluye dos aplicaciones
independientes: **`osf-cli`** — una línea de comandos programable por
scripts para inspeccionar, exportar y convertir archivos OSF — y
**`osf-viewer`** — un visor JavaFX que traza muchos canales a la vez. Ambas
se basan exclusivamente en la API pública de la biblioteca (véanse
[Lectura](./reading.md) y [Escritura](./writing.md)) y son, al mismo tiempo,
ejemplos completos de su uso.

Base: **Java 21**, reactor de Maven. La compilación completa (`mvn -f
implementations/java/pom.xml package`) genera ambas herramientas de una sola
vez; los detalles sobre el reactor se encuentran en
[Compilación](./building.md).

## osf-cli

`osf-cli` es una aplicación [picocli](https://picocli.info/) con cuatro
subcomandos. La clase de entrada
[`OsfCli`](https://github.com/optimeas/osf/blob/main/implementations/java/osf-cli/src/main/java/com/optimeas/osf/cli/OsfCli.java)
registra `info`, `channels`, `dump` y `convert`; cada comando dispone de
`-h`/`--help` y `-V`/`--version` (de `mixinStandardHelpOptions`).

### Compilación como JAR ejecutable

El módulo `osf-cli` se empaqueta mediante el `maven-shade-plugin` en un
**JAR ejecutable autosuficiente** (clase principal
`com.optimeas.osf.cli.OsfCli`, `finalName` `osf-cli`):

```bash
mvn -f implementations/java/pom.xml -pl osf-cli -am package
java -jar implementations/java/osf-cli/target/osf-cli.jar --help
```

El JAR Shade contiene la biblioteca OSF y picocli, por lo que se ejecuta sin
ningún classpath adicional. Por brevedad, a continuación `osf` representa
`java -jar …/osf-cli.jar`.

### `info` — metadatos y resumen de canales

```bash
osf info messung.osf
```

Carga el archivo (OSF4, OSF5 u OSFZ de forma transparente) y muestra el
formato, los metadatos del archivo, el estado de compresión y una línea por
canal:

```text
format: OSF5
creator: optiMEAS
created_utc: 2026-01-15T09:30:00Z
compressed: false
channels: 3
  [0] temperature  type=double  mode=equidistant  samples=10000  unit=°C
  [1] status       type=string  mode=variable     samples=42     unit=
  [2] position     type=gps_location  mode=timestamped  samples=500  unit=
```

La línea `format:` es `OSF4` u `OSF5`; los metadatos proceden sin
modificar del metabloque (`creator`, `created_utc`, `location`, …); con
entradas comprimidas aparece `compressed: true (gzip)`.

### `channels` — tabla de canales

```bash
osf channels messung.osf --sort NAME
```

Muestra una tabla alineada (columnas *index, name, datatype, mode, samples,
unit*). `--sort` acepta `INDEX` (predeterminado) o `NAME`:

```text
index   name                                      datatype      mode          samples   unit
------------------------------------------------------------------------------------------
0       temperature                               double        equidistant   10000     °C
2       position                                  gps_location  timestamped   500
1       status                                    string        variable      42
```

### `dump` — datos de canal a CSV

`dump` escribe los valores de los canales como CSV — de forma
predeterminada **todos los canales representables** (numéricos + `bool`;
`string`/`binary`/`gps` se omiten).

| Opción | Efecto |
|---|---|
| `--channel <name\|index>` | Elegir el canal por nombre **o** por índice entero; repetible. Sin indicación: todos los canales representables |
| `--format <csv\|unified-csv>` | `csv` (predeterminado): un bloque por canal; `unified-csv`: una tabla ancha |
| `--timestamp-format <…>` | `DATETIME` (predeterminado), `SECONDS`, `ISO8601`, `NANOSECONDS` |
| `--out <archivo>` | Salida a un archivo en lugar de a stdout |

Formatos de marca de tiempo: `DATETIME` = `uuuu-MM-dd HH:mm:ss.SSS` (UTC,
ms), `SECONDS` = segundos decimales con 9 decimales, `ISO8601` =
`uuuu-MM-dd'T'HH:mm:ss'Z'`, `NANOSECONDS` = entero en nanosegundos sin
procesar. Los valores `double` enteros se representan sin punto decimal
(`1` en lugar de `1.0`).

**CSV por canal** (predeterminado) — cada bloque con una cabecera
`# channel:` y líneas `timestamp,value`:

```bash
osf dump messung.osf --channel temperature --timestamp-format SECONDS
```
```text
# channel: temperature
timestamp,value
0.000000000,21.5
0.001000000,21.6
```

**CSV unificado** — una tabla ancha con una fila por marca de tiempo
distinta; celdas vacías cuando un canal no tiene muestra en ese instante:

```bash
osf dump messung.osf --format unified-csv --channel 0 --channel 3 --out werte.csv
```
```text
timestamp,temperature,humidity
1970-01-01 00:00:00.000,21.5,48
1970-01-01 00:00:00.001,21.6,
```

Los nombres de canal con coma, comillas o salto de línea se entrecomillan
conforme a CSV.

### `convert` — a OSF5 (opcionalmente comprimido)

`convert` lee cualquier entrada (OSF4/OSF5, eventualmente comprimida) y la
escribe como **OSF5**:

```bash
osf convert alt-osf4.osf neu.osf                 # OSF4 → OSF5
osf convert messung.osf messung.osfz --compress  # OSF5 → gzip (OSFZ)
```

| Opción | Efecto |
|---|---|
| `--compress` | Empaquetar la salida en gzip (genera un archivo OSFZ) |
| `--writer <BLOCK\|STREAMING>` | Backend de escritura; `BLOCK` (predeterminado) almacena en memoria y escribe de una sola vez, `STREAMING` reproduce muestra a muestra a través de un `FileChannel` |

`STREAMING` no admite `--compress`; con esa combinación, el comando recurre
a `BLOCK` con un aviso. Tras el éxito, `convert` notifica
`wrote <archivo> (N channels)`. Con ello, el comando es al mismo tiempo el
conversor más sencillo de OSF4 a OSF5.

Código fuente completo:
[`osf-cli/src/main/java/com/optimeas/osf/cli/`](https://github.com/optimeas/osf/tree/main/implementations/java/osf-cli/src/main/java/com/optimeas/osf/cli).

## osf-viewer

`osf-viewer` es una aplicación **JavaFX** que representa a la vez muchos
canales de un archivo OSF. La idea central es un **diezmado mín./máx. por
píxel**: da igual cuántos millones de muestras caigan en una columna de la
pantalla, nunca se pierde un valor atípico.

### Inicio

La forma más sencilla de ejecutar el visor es mediante el plugin de Maven de
JavaFX (clase principal `com.optimeas.osf.viewer.ViewerApp`):

```bash
mvn -pl osf-viewer -f implementations/java/pom.xml javafx:run
```

Opcionalmente, se puede abrir un archivo directamente al iniciar — el primer
argumento de inicio se interpreta como ruta:

```bash
mvn -pl osf-viewer -f implementations/java/pom.xml javafx:run \
    -Djavafx.args="messung.osf"
```

Se abre una ventana «OSF Viewer» de 1000×700 con barra de herramientas,
lista de canales, área de trazado y línea de estado.

### Interfaz de usuario

- **Barra de herramientas** — *Open…* abre un cuadro de diálogo de archivo
  (filtro `*.osf`, `*.osfz`); *Zoom Reset* restablece el área visible a la
  extensión temporal completa de todos los canales representables.
- **Lista de canales** (a la izquierda) — una tabla con las columnas *Plot,
  Name, DataType, Mode, Samples, Unit*. La casilla Plot muestra un canal en
  el gráfico; para los canales no representables (String, Binary, GPS) está
  desactivada y lleva el tooltip «not plotted in v1».
- **Área de trazado** (centro) — la representación propiamente dicha de las
  curvas; ocupa el resto del espacio y se redibuja con cada cambio de
  tamaño.
- **Línea de estado** (abajo) — estado de carga e información de lectura del
  cursor.

La carga se realiza siempre **en segundo plano** (`Task` de JavaFX), de modo
que la interfaz sigue respondiendo mientras se leen archivos grandes.

### Diezmado — mín./máx. por píxel

Para cada columna de píxeles, el
[`Decimator`](https://github.com/optimeas/osf/blob/main/implementations/java/osf-viewer/src/main/java/com/optimeas/osf/viewer/Decimator.java)
determina el **mínimo Y el máximo** de todas las muestras que caen en esa
ventana de tiempo y traza una línea vertical de `minY` a `maxY`. Así, incluso
con una compresión extrema, todo pico permanece visible — a diferencia del
simple descarte de puntos intermedios. La asignación de muestras utiliza una
búsqueda binaria (`lowerBound`) sobre las marcas de tiempo ascendentes, por
lo que es rápida incluso con millones de muestras. Cada canal seleccionado
escala su eje Y de forma independiente (autoescala sobre el rango de
valores almacenado en caché); los colores rotan por una paleta de seis tonos
bien distinguibles.

### Interacción

- **Desplazar (pan)** — arrastrar horizontalmente con el botón del ratón
  pulsado desplaza la ventana de tiempo.
- **Zoom** — rueda del ratón; desplazarla hacia arriba acerca (factor 0,8),
  hacia abajo aleja (factor 1,25), siempre **alrededor de la posición del
  cursor**, de modo que el instante situado bajo el cursor permanece fijo.
- **Información de lectura del cursor** — al mover el ratón, la línea de
  estado muestra el instante del cursor y, para cada canal seleccionado, el
  valor de muestra más próximo.

La transformación de coordenadas (tiempo↔X, valor↔Y con eje Y invertido)
está encapsulada en `AxisTransform`, y la lógica de lectura y de dibujo en
`PlotCanvas`; ambas están deliberadamente separadas de la capa de modelo
pura (`ViewerModel`, `Decimator`, `AxisTransform` — sin importaciones de
JavaFX) y, por tanto, pueden probarse sin un entorno JavaFX en ejecución.
Los detalles de este diseño se encuentran en
[Arquitectura](./architecture.md) y [Aspectos internos](./internals.md).

Código fuente completo:
[`osf-viewer/src/main/java/com/optimeas/osf/viewer/`](https://github.com/optimeas/osf/tree/main/implementations/java/osf-viewer/src/main/java/com/optimeas/osf/viewer).

## Continuar

- [Lectura](./reading.md) y [Escritura](./writing.md) — la API de la
  biblioteca utilizada por ambas herramientas.
- [Gestión de errores](./error-handling.md) — cómo `OsfException` llega
  hasta la salida de la CLI.
- [Recetario](./cookbook.md) — recetas breves y copiables.
- [Volver al resumen de Java](../java.md).

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
