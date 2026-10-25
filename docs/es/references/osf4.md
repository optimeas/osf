---
title: OSF4 – Documentación específica
description: Detalles y especificaciones del Open Streaming Format versión 4 (OSF4)
#sidebar_label: OSF4
image: "/img/om_social_card.png"
keywords:
  - OSF4
  - Fileformat
  - XML
last_update:
  date: 2026-07-07
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# OSF4 – Documentación específica

Este documento describe todos los aspectos del **Open Streaming Format versión 4 (OSF4)** que van más allá de la descripción general de OSF.  
Complementa el archivo [`osf_general.md`](../osf_general.md), en el que se explican todas las estructuras comunes a OSF4 y OSF5.

OSF4 es la versión clásica del formato. Utiliza exclusivamente **XML** para el bloque de metadatos y constituye la base de la compatibilidad con versiones anteriores en OSF5.

El perfil de integridad (CRC32C / firma) existe a partir de OSF5; los archivos OSF4 no se ven afectados. Consulte el [perfil de integridad de OSF5](osf5_integrity.md).



## Magic Header en OSF4

- **Identificadores permitidos:**  
  - `OSF4`  
  - `OCEAN_STREAM_FORMAT4` — identificador heredado (legacy), que los dispositivos suministrados siguen escribiendo  
  - `OCEAN_STREAMING_FORMAT4` — grafía histórica más antigua

- **Formato:**  
  ```
  OSF4 173762\n
  ```

- **Propiedades:**  
  - Siempre **bloque de metadatos XML** inmediatamente después de la cabecera.  
  - Sin compatibilidad con JSON.  
  - Los analizadores de OSF5 pueden leer archivos OSF4 por completo.

---

## Bloque de metadatos en OSF4 (XML)

OSF4 utiliza exclusivamente **XML** para el bloque de metadatos.  
Contiene toda la información del archivo y de los canales y comienza con un prólogo estándar:

```xml
<?xml version="1.0" encoding="UTF-8"?>
```

### Elemento raíz `<osf>`

Ejemplo:

```xml
<osf version="4"
     created_utc="2019-08-12T12:23:01+02:00"
     creator="smartdevice:14001000078"
     created_at_longitude="50.2"
     created_at_latitude="8.65"
     created_at_altitude="193"
     reason="BOOT"
     total_seq_no="0"
     triggered_seq_no="0"
     namespacesep="."
     tag="preview"
     comment="">
```

#### Atributos:

- **version:** versión del formato OSF4 (valor predeterminado: `"1"`).  
- **created_utc:** momento de creación del archivo en formato ISO 8601 (UTC).  
- **creator:** identificación del dispositivo o programa que lo genera.  
- **created_at_longitude / latitude / altitude:** posición geográfica opcional.  
- **reason:** motivo de la creación del archivo (`BOOT`, `SEQUENCE`, `TRIGGERED`).  
- **total_seq_no:** número de secuencia absoluto desde el arranque del sistema.  
- **triggered_seq_no:** número de secuencia relativo desde el último disparador.  
- **namespacesep:** separador para nombres de canal jerárquicos (valor predeterminado: `"."`).  
- **tag:** etiqueta libre para clasificar el archivo (valor predeterminado: `"preview"`).  
- **comment:** comentario opcional.  

---

## Lista de canales `<channels>`

Contiene todos los canales del archivo.

```xml
<channels count="8">
    <channel index="0"
             name="Sensor.Temperature"
             channeltype="scalar"
             datatype="double"
             timeincrement="1000000"
             sizeoflengthvalue="2"
             physicalunit="°C"
             reference="uuid"
             physicaldimension="temperature"/>
</channels>
```

- **count:** número de canales del archivo.

---

## Descripción de canal `<channel>`

Todos los parámetros son los descritos en la documentación general de OSF; para OSF4 se aplica lo siguiente:

- **Bloque de metadatos:** siempre XML  
- **`channeltype` admitidos:** `scalar`, `vector`, `matrix`, `binary`  
- **Parámetros de vector y matriz:** en la definición del canal mediante `rows`, `columns` y los atributos correspondientes  
- **`sizeoflengthvalue`:** campo obligatorio (2 o 4 bytes)  

---

## Tipos de datos admitidos en OSF4

| Tipo de datos | Tamaño    | Descripción |
|-------------|----------|--------------|
| `bool`      | 1 byte   | Verdadero/falso |
| `int8`      | 1 byte   | Entero con signo |
| `int16`     | 2 bytes  | Entero con signo |
| `int32`     | 4 bytes  | Entero con signo |
| `int64`     | 8 bytes  | Entero con signo |
| `uint8`     | 1 byte   | Entero sin signo, intervalo de valores 0 … 255 |
| `uint16`    | 2 bytes  | Entero sin signo, intervalo de valores 0 … 65 535 |
| `uint32`    | 4 bytes  | Entero sin signo, intervalo de valores 0 … 4 294 967 295 |
| `uint64`    | 8 bytes  | Entero sin signo, intervalo de valores 0 … 18 446 744 073 709 551 615 |
| `float`     | 4 bytes  | IEEE 754 de precisión simple |
| `double`    | 8 bytes  | IEEE 754 de precisión doble |
| `string`    | variable | Codificado en UTF-8, longitud definida por el tamaño del bloque. En disco siempre se le añade un byte nulo final (`0x00`); consulte en [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary) las reglas de escritura y lectura que dependen de la versión. |
| `gpslocation` | 24 bytes | Estructura para posiciones GPS |

## Terminación de cadenas

Para `bcAbsTimeStampData` con `datatype=string` o `datatype=binary` en OSF4:

- **Los escritores DEBEN** añadir un byte nulo final (`0x00`) a cada carga útil.
- **Los lectores DEBEN** eliminar incondicionalmente el último byte de la carga útil; este byte está garantizado.

Consulte en [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary) la justificación y la regla completa para varias versiones.

---

## Bloques de datos en OSF4

OSF4 utiliza las estructuras de bloques de datos descritas en la especificación general.  
Particularidades de OSF4:

- **Byte de control:** se admiten todos los tipos de bloque originales `0–8`.  
- **`bcContinuedRelStampData`:** en OSF4 todavía se utiliza (eliminado a partir de OSF5).  
- **`bcStatusEvent` y `bcMessageEvent`:** presentes; los escritores NO DEBEN generarlos. No obstante, `bcMessageEvent` debe **leerse**: los dispositivos en uso en campo siguen escribiendo así los canales `string` de OSF4, y un lector que omita este tipo de bloque pierde esos canales sin aviso. Consulte [`osf_general.md`](../osf_general.md#bcmessageevent).  
- **Metadatos:** siempre en formato XML.

### `bcStartData` con frecuencia de muestreo

A partir de esta revisión de la especificación, `bcStartData` en OSF4 incluye, igual que en OSF5, un campo `double` con la **frecuencia de muestreo (Hz)** inmediatamente después de la marca de tiempo inicial `int64`. La frecuencia es válida para todos los bloques `bcContinuedData` posteriores del mismo canal, hasta que se escriba un nuevo bloque `bcStartData`.

**Ejemplo de `bcStartData` en OSF4:**
`[int64 ZeitStart] [double SampleRate] [uint32 N] [double Wert1] [double Wert2] ... [double WertN]`

Los nuevos escritores de OSF4 **deben** escribir este campo. Los lectores que encuentren un archivo OSF4 sin este campo (archivos antiguos existentes) fallan con un error de formato; no existe ningún valor de reserva implícito a partir de `timeincrement`.

### Restricciones:

- **Canales equidistantes (`bcStartData`, `bcContinuedData`):** solo tipos numéricos (`int*`, `float`, `double`).  
- **Canales con marca de tiempo (`bcAbsTimeStampData`, `bcContinuedRelStampData`):** se permiten todos los tipos.

---

## Tráiler XML y Magic Trailer

OSF4 admite de forma opcional un bloque de información XML con estadísticas de canal y un Magic Trailer.

### Ejemplo de tráiler XML:

```xml
<trailer finalized_utc="2019-08-12T12:23:01+02:00" reason="fileStartGrid_min">
    <channels count="8">
        <channel index="0" samples="29452" last_ns="1384899599997800000"/>
        <channel index="1" samples="29452" last_ns="1384899599997800000"/>
    </channels>
</trailer>
```

### Magic Trailer:

```
OSF_STREAM_END 321316454==============
```

- 40 bytes de longitud; el número indica la posición del bloque `0xFFFF`.

---

## Particularidades y limitaciones de OSF4

- Solo bloque de metadatos XML, sin compatibilidad con JSON.  
- Byte de control más complejo, más tipos de bloque que OSF5.  
- Canales de vector y matriz mediante parámetros XML (`rows`, `columns`).  
- `bcContinuedRelStampData` se admite, pero ya no está incluido en OSF5.  

---

## Ejemplo de un archivo OSF4

```text
OSF4 30269
<?xml version="1.0" encoding="UTF-8"?>
<osf version="4" created_utc="2019-08-12T12:23:01+02:00" creator="smartdevice:14001000021">
  <channels count="2">
    <channel index="0" name="Sensor.Temperature" channeltype="scalar" datatype="double" timeincrement="1000000"/>
    <channel index="1" name="Sensor.Pressure" channeltype="scalar" datatype="double" timeincrement="1000000"/>
  </channels>
</osf>
[BEGIN OF BINARY DATA]
```

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
