---
title: osftool — Herramienta de línea de comandos
description: La herramienta de línea de comandos osftool para combinar, exportar, inspeccionar y convertir archivos OSF
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - osftool
  - CLI
  - Línea de comandos
  - merge
  - export
last_update:
  date: 2026-05-22
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# osftool — Herramienta de línea de comandos

**osftool** es una herramienta de línea de comandos basada en verbos para trabajar con archivos en Open Streaming Format. Lee y escribe OSF4 y OSF5, procesa de forma transparente archivos OSFZ comprimidos y reúne en un único ejecutable las tareas cotidianas relacionadas con los datos OSF: combinar archivos a lo largo de un intervalo de tiempo, exportar canales a CSV o HDF5, inspeccionar metadatos, calcular estadísticas, convertir entre versiones del formato y comprobar la integridad de los archivos.

osftool se basa en la biblioteca OSF de Delphi (`implementations/delphi/src/`). El destino de compilación principal es Windows de 64 bits; el proyecto incluye además configuraciones de compilación para macOS (Intel y Apple Silicon) y Linux de 64 bits. La versión actual es la **1.1.0**.

## Para qué sirve osftool

osftool cubre las tareas recurrentes de un flujo de trabajo basado en OSF desde un script o un terminal, sin IDE y sin programación:

- **Combinar datos de campo.** Se leen muchos archivos OSF/OSFZ repartidos por un árbol de directorios y se combinan en un único archivo para un intervalo de tiempo y una selección de canales determinados.
- **Llevar los datos a programas de análisis.** Los canales se exportan a CSV (como un bloque XY por canal o como un eje de tiempo común con una columna por canal) o, en la compilación para Windows, a un archivo HDF5.
- **Inspeccionar archivos rápidamente.** Los metadatos, el intervalo de tiempo, las listas de canales y las estadísticas por canal están disponibles sin abrir el archivo en una aplicación.
- **Migrar entre versiones del formato.** Los archivos OSF4 se reescriben como OSF5 y viceversa.
- **Comprobar la integridad.** Los archivos se recorren bloque a bloque y se comprueba su coherencia estructural.
- **Automatización.** Cada comando dispone de un modo `--json` legible por máquina y de un conjunto uniforme de códigos de salida, de modo que osftool se integra limpiamente en scripts y pipelines de CI.

## Instalación

### Instalador para Windows

La forma más rápida de obtener osftool en Windows es el instalador ya preparado:

**[Descargar osftool para Windows (64 bits)](https://www.optimeas.com/hubfs/Software%20Download/osftool-setup-latest.exe)**

El instalador configura `OsfTool.exe` junto con el entorno de ejecución de HDF5, añade el directorio del programa al `PATH` y permite elegir entre una instalación para todos los usuarios y una solo para el usuario actual. Una vez finalizada, `osftool` está disponible en cualquier terminal.

### Compilar desde el código fuente

osftool forma parte del repositorio de OSF y se compila con Embarcadero Delphi 12 (RAD Studio 23) o superior.

```
cd implementations/delphi/tools/osftool
dcc64 -B -Q OsfTool.dpr
```

Esto genera `OsfTool.exe` para Windows de 64 bits. El archivo de proyecto `OsfTool.dproj` incluye además las configuraciones de compilación `OSX64`, `OSXARM64` y `Linux64` para su uso desde el IDE de RAD Studio; el código fuente está libre de conflictos de configuración de compilación condicional para estos destinos.

### Añadir osftool al PATH

Para poder invocar `osftool` desde cualquier directorio, se añade su ubicación a la ruta de búsqueda:

```
osftool config install-path
```

En Windows, este comando registra el directorio del ejecutable en el `PATH` del usuario (`HKCU\Environment`); no se necesitan derechos de administrador. En macOS y Linux, el comando muestra el fragmento de shell que debe añadirse a `~/.zshrc` o `~/.bashrc`. El cambio se revierte con `osftool config uninstall-path`.

## Manejo general

```
osftool <befehl> [optionen] [argumente]
osftool <befehl> --help
```

`osftool` sin argumentos o `osftool --help` muestra la ayuda global con la lista de comandos. Si se añade `--help` a un comando, este muestra su ayuda detallada sin ejecutar ninguna acción. `osftool --version` (o `-V`) muestra la versión y la marca de tiempo de la compilación; con `--short`, solo el número de versión.

### Comandos

| Comando    | Finalidad |
|------------|-------|
| `merge`    | Combinar archivos OSF de un directorio a lo largo de un intervalo de tiempo |
| `export`   | Exportar canales OSF a CSV o a otros formatos |
| `info`     | Mostrar los metadatos y el intervalo de tiempo de un archivo |
| `channels` | Listar todos los canales de un archivo |
| `stat`     | Calcular estadísticas (mínimo, máximo, valor medio, …) por canal |
| `cache`    | Gestionar los archivos de caché sidecar `.json` |
| `config`   | Mostrar y editar los ajustes predeterminados |
| `convert`  | Convertir entre OSF4 y OSF5 |
| `verify`   | Comprobar la integridad del archivo y la coherencia de los bloques |

### Opciones globales

Todos los comandos entienden estas opciones:

| Opción      | Efecto |
|-------------|---------|
| `--json`    | Muestra en stdout un resultado JSON legible por máquina en lugar de texto legible por personas |
| `--quiet`   | Suprime las salidas informativas; las advertencias y los errores se siguen mostrando |
| `--verbose` | Muestra mensajes de depuración en stderr |

Los resultados van a **stdout**, y los mensajes de registro y los errores a **stderr**, de modo que ambos pueden redirigirse por separado. En el modo `--json`, stdout contiene exclusivamente el documento JSON.

### Códigos de salida

| Código | Significado |
|------|-----------|
| `0`  | Correcto |
| `1`  | Argumentos no válidos |
| `2`  | Archivo o directorio no encontrado |
| `3`  | Error de E/S |
| `4`  | Error de formato (archivo OSF no válido o dañado) |

## La caché sidecar

Varios comandos pueden utilizar un **archivo sidecar** `.json`, que se guarda junto al archivo OSF correspondiente. El archivo sidecar contiene metadatos por canal (número de valores, primera y última marca de tiempo, el intervalo de tiempo global) que el bloque de metadatos de OSF por sí solo no incluye. Si existe un archivo sidecar válido, comandos como `info` y `channels` responden de inmediato, en lugar de recorrer cada bloque.

El archivo sidecar se utiliza automáticamente; con `--no-cache` se ignora y se examina directamente el archivo de origen. El comando `cache` crea, renueva, comprueba y elimina archivos sidecar en grandes cantidades.

## Referencia de comandos

### merge

Busca de forma recursiva archivos `.osf` y `.osfz` en un directorio, selecciona los archivos que se solapan con un intervalo de tiempo y combina los canales elegidos en un único archivo de salida.

```
osftool merge <rootdir> <outputfile> [kanal ...] [optionen]
```

| Argumento    | Descripción |
|--------------|--------------|
| `rootdir`    | Directorio raíz; se busca de forma recursiva `.osf` y `.osfz` |
| `outputfile` | Ruta del archivo de salida (`.osf`) |
| `kanal`      | Nombres de canal opcionales; si se omiten, se combinan todos los canales |

| Opción         | Descripción |
|----------------|--------------|
| `--start <ts>` | Inicio del intervalo, ISO 8601 (valor predeterminado: `1970-01-01T00:00:00`) |
| `--end <ts>`   | Fin del intervalo, ISO 8601 (valor predeterminado: fecha y hora actuales) |
| `--osf4`       | Escribir la salida en OSF4 (valor predeterminado: de la configuración `output.format`) |
| `--overwrite`  | Sobrescribir las marcas de tiempo solapadas (valor predeterminado: de la configuración `output.overlap`) |
| `--no-cache`   | No leer ni escribir archivos sidecar `.json` |
| `-q`, `--quiet`   | Suprimir la visualización en vivo; solo errores, en stderr |
| `-v`, `--verbose` | Mostrar cada línea de registro (salida clásica con desplazamiento, sin barra en vivo) |
| `--json`          | Mostrar en stdout un flujo de eventos JSON Lines legible por máquina |
| `--log <pfad>`    | Escribir en un archivo el registro de diagnóstico completo (todos los niveles) |

`--start` y `--end` son opcionales e independientes: cada límite tiene su propio valor predeterminado, y una opción indicada sobrescribe únicamente ese límite. Sin ninguna de las dos opciones, `merge` abarca todo el intervalo disponible.

```
# Alles unter ./feld-daten in eine Datei zusammenführen
osftool merge ./feld-daten zusammen.osf

# Nur zwei Kanäle innerhalb eines Zeitfensters zusammenführen
osftool merge ./feld-daten fenster.osf Sensor/Temperatur Sensor/Druck \
  --start 2026-05-05T10:00:00 --end 2026-05-05T12:00:00
```

De forma predeterminada, `merge` muestra un **indicador de progreso en vivo**: un breve encabezado anuncia cada fase (examinar el directorio, leer los metadatos de los archivos, leer los archivos, escribir la salida) y debajo se redibuja en el mismo lugar una única línea de barra de progreso que muestra el porcentaje, el contador de archivos y el nombre del archivo que se está procesando en ese momento. Las advertencias y los errores aparecen por encima de la barra en cuanto se producen; los mensajes informativos de bajo nivel por canal permanecen suprimidos.

```
Reading files...
  [████████████████████░░░░░░░░░░░░░░░░░░░░] 50% (173/346) - 20260517_192802.osfz
```

Las opciones de salida modifican esta presentación y se excluyen mutuamente: `-v` / `--verbose` muestra en su lugar el registro completo con desplazamiento; `-q` / `--quiet` solo muestra errores en stderr y, por lo demás, permanece en silencio; `--json` proporciona un flujo de eventos JSON Lines para pipelines. `--log <pfad>` es ortogonal: escribe en un archivo el registro de diagnóstico completo en cualquier modo. Si stdout se redirige a una tubería (pipe) o a un archivo, osftool sustituye automáticamente la visualización en vivo por líneas de progreso sencillas periódicas.

### export

Exporta los canales de un único archivo OSF/OSFZ a CSV o, en la compilación para Windows, a HDF5.

```
osftool export <inputfile> <outputfile> [kanal ...] [optionen]
```

| Argumento    | Descripción |
|--------------|--------------|
| `inputfile`  | Archivo de origen `.osf` o `.osfz` |
| `outputfile` | Ruta del archivo de salida |
| `kanal`      | Nombres de canal opcionales; si se omiten, se exportan todos los canales |

| Opción                     | Descripción |
|----------------------------|--------------|
| `--format <fmt>`           | `csv` (predeterminado): un bloque XY por canal; `unified-csv`: un eje de tiempo común con una columna por canal; `hdf5`: un archivo HDF5, un dataset por canal (solo en la compilación para Windows) |
| `--timestamp-format <fmt>` | Formato de la marca de tiempo para `unified-csv`: `datetime` (predeterminado), `seconds`, `iso8601`, `nanoseconds` |
| `--start <zeit>`           | Exportar solo los valores a partir de esta hora UTC (ISO 8601) |
| `--end <zeit>`             | Exportar solo los valores hasta esta hora UTC (ISO 8601) |
| `--decimal-sep <c>`        | Separador decimal de CSV: `comma` (predeterminado) o `dot` |
| `--encoding <enc>`         | Codificación de CSV: `iso-8859-1` (predeterminado) o `utf-8` |
| `--chunk-size <n>`         | HDF5: valores por chunk del dataset (predeterminado `8192`) |
| `--deflate-level <n>`      | HDF5: nivel de compresión gzip `0`–`9` (predeterminado `4`) |
| `--no-shuffle`             | HDF5: desactivar el prefiltro shuffle |
| `--namespace-sep <c>`      | HDF5: carácter que descompone un nombre de canal en una ruta de grupo HDF5 (predeterminado `.`) |
| `--hdf5-lib-dir <pfad>`    | HDF5: directorio en el que se busca `hdf5.dll` |
| `--exclude-empty`          | Omitir los canales sin valores |

`--start` y `--end` deben indicarse conjuntamente: una opción sin la otra se rechaza. Los valores predeterminados de `--decimal-sep` y `--encoding` proceden del archivo de configuración.

El formato `hdf5` solo está disponible en la compilación para Windows. Cada canal se convierte en un dataset 1-D con chunks y comprimido con deflate, formado por registros `{int64 timestamp_ns; value}`; el nombre del canal se descompone en una jerarquía de grupos HDF5 según `--namespace-sep`, y los metadatos del nivel de archivo y de canal se escriben como atributos HDF5. La exportación requiere la biblioteca de ejecución de HDF5: `hdf5.dll` debe ser accesible a través de la ruta de búsqueda del sistema o del directorio indicado con `--hdf5-lib-dir`. Las opciones `--chunk-size`, `--deflate-level`, `--no-shuffle` y `--namespace-sep` solo afectan a `hdf5`; `--decimal-sep` y `--encoding` solo afectan a los formatos CSV.

```
osftool export motorbike.osf motorbike.csv --format unified-csv \
  --timestamp-format iso8601 --decimal-sep dot

# HDF5-Export (Windows-Build), stärkere Kompression
osftool export motorbike.osf motorbike.h5 --format hdf5 --deflate-level 6
```

### info

Muestra los metadatos y el intervalo de tiempo global de un archivo.

```
osftool info <file> [optionen]
```

| Opción       | Descripción |
|--------------|--------------|
| `--no-cache` | No utilizar el archivo sidecar `.json`; examinar directamente el archivo OSF |
| `--json`     | Salida en JSON |

El informe contiene la versión del formato, el creador y la hora de creación, la etiqueta (tag), el motivo (reason), el comentario, el número de canales, la primera y la última marca de tiempo de los datos, así como la duración resultante.

```
osftool info motorbike.osf
```

### channels

Lista todos los canales del bloque de metadatos.

```
osftool channels <file> [optionen]
```

| Opción               | Descripción |
|----------------------|--------------|
| `--filter <muster>`  | Filtro con comodines sobre el nombre del canal, p. ej., `GPS.*` |
| `--no-cache`         | No utilizar el archivo sidecar `.json` |
| `--json`             | Salida como matriz JSON |

Cada línea muestra el índice del canal, el nombre, el tipo de datos, la unidad física y, si existe un archivo sidecar válido, el número de valores y la primera/última marca de tiempo. Sin archivo sidecar, estas columnas muestran `?`.

```
osftool channels motorbike.osf --filter "Sensor/*"
```

### stat

Calcula el mínimo, el máximo, el valor medio y la desviación típica de cada canal numérico con el algoritmo en línea de una sola pasada de Welford.

```
osftool stat <file> [kanal ...] [optionen]
```

| Opción           | Descripción |
|------------------|--------------|
| `--start <zeit>` | Considerar solo los valores a partir de esta hora UTC (ISO 8601) |
| `--end <zeit>`   | Considerar solo los valores hasta esta hora UTC (ISO 8601) |
| `--json`         | Salida en JSON |

`--start` y `--end` son opcionales de forma independiente. Los canales de tipo string, binary y gpslocation se listan, pero se marcan como *no numéricos*. Para los canales Int64/UInt64 se advierte de una posible pérdida de precisión en la conversión a `Double` para el cálculo.

```
osftool stat motorbike.osf --start 2026-05-05T10:00:00
```

### cache

Gestiona los archivos de caché sidecar `.json` de cada archivo `.osf`/`.osfz` situado bajo un directorio.

```
osftool cache <subbefehl> <rootdir> [optionen]
```

| Subcomando | Descripción |
|-----------|--------------|
| `build`   | Crear los archivos sidecar que faltan; omitir los archivos que ya tienen un archivo sidecar válido |
| `rebuild` | Volver a crear a la fuerza todos los archivos sidecar |
| `clean`   | Eliminar todos los archivos sidecar bajo `rootdir` |
| `status`  | Mostrar qué archivos no tienen un archivo sidecar válido |

| Opción           | Descripción |
|------------------|--------------|
| `--no-recursive` | Limitar solo al directorio raíz (predeterminado: incluir subdirectorios) |
| `--json`         | Salida en JSON |

```
osftool cache build ./feld-daten
osftool cache status ./feld-daten
```

### config

Comprueba y modifica los ajustes permanentes que otros comandos utilizan como valores predeterminados, y gestiona la entrada del `PATH`.

```
osftool config                    Alle aktuellen Einstellungen anzeigen
osftool config set <key> <wert>   Einen Wert setzen
osftool config reset              Alle Einstellungen auf Vorgaben zurücksetzen
osftool config install-path       osftool zum Benutzer-PATH hinzufügen
osftool config uninstall-path     osftool aus dem Benutzer-PATH entfernen
osftool config --json             Einstellungen als JSON anzeigen
```

Las claves disponibles se describen en [Configuración](#konfiguration).

### convert

Convierte un único archivo entre OSF4 y OSF5 reescribiéndolo a través del merger.

```
osftool convert <inputfile> <outputfile> [optionen]
```

| Opción   | Descripción |
|----------|--------------|
| `--osf4` | Escribir la salida como OSF4 |
| `--osf5` | Escribir la salida como OSF5 |
| `--json` | Salida en JSON |

`--osf4` y `--osf5` se excluyen mutuamente. Si no se indica ninguna de las dos opciones, la versión de destino procede de la clave de configuración `output.format`. Cabe señalar que el conversor escribe siempre bloques de datos con marca de tiempo absoluta, con independencia de la estructura del archivo de origen.

```
osftool convert legacy.osf modern.osf --osf5
```

### verify

Recorre un archivo de principio a fin y comprueba su integridad estructural.

```
osftool verify <file> [optionen]
```

Se realizan las siguientes comprobaciones:

1. Magic Header legible y versión reconocida.
2. Bloque de metadatos procesable (XML o JSON válido).
3. El índice de canal de cada bloque está presente en el bloque de metadatos.
4. Ningún bloque indica una longitud que supere el tamaño del archivo.
5. Las marcas de tiempo aumentan de forma monótona por canal.
6. El archivo termina limpiamente: el último bloque no está truncado.

| Opción     | Descripción |
|------------|--------------|
| `--strict` | Tratar las advertencias como errores (afecta al código de salida) |
| `--json`   | Salida en JSON |

Los problemas que vulneran la integridad se notifican como errores y dan lugar al código de salida `4`; las anomalías subsanables (por ejemplo, un último bloque truncado) son advertencias. Con `--strict`, también las advertencias dan lugar al código de salida `4`.

```
osftool verify motorbike.osf --strict
```

## Configuración {#konfiguration}

osftool guarda los ajustes permanentes en un archivo JSON. Otros comandos toman de él sus valores predeterminados, de modo que la configuración actúa como una directriz específica del usuario.

| Plataforma | Ubicación |
|-----------|-------------|
| Windows   | `%APPDATA%\osftool\config.json` |
| macOS / Linux | `~/.config/osftool/config.json` |

El archivo es opcional: si no existe, cada ajuste recupera su valor predeterminado integrado.

| Clave                | Predeterminado | Significado |
|----------------------|--------------|-----------|
| `output.format`      | `osf5`       | Formato de salida predeterminado para `merge` y `convert` |
| `output.overlap`     | `skip`       | Estrategia de solapamiento: `skip` u `overwrite` |
| `export.decimal_sep` | `,`          | Separador decimal predeterminado para CSV |
| `export.encoding`    | `iso-8859-1` | Codificación predeterminada para CSV |
| `cache.enabled`      | `true`       | Utilizar archivos sidecar `.json` |
| `cache.auto_build`   | `true`       | Crear la caché automáticamente durante un escaneo |

Los valores se leen y se escriben con el comando `config`:

```
osftool config                          # alle Einstellungen anzeigen
osftool config set output.format osf4   # eine Vorgabe ändern
osftool config reset                    # Vorgaben wiederherstellen
```

Una opción de la línea de comandos sobrescribe el valor predeterminado configurado únicamente para esa invocación.

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
