---
title: OSF5 – Documentación específica
description: Detalles y especificaciones del Open Streaming Format versión 5 (OSF5)
#sidebar_label: OSF5
image: "/img/om_social_card.png"
keywords:
  - OSF5
  - Fileformat
  - JSON
last_update:
  date: 2026-07-07
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# OSF5 – Documentación específica

Este documento describe todos los aspectos del **Open Streaming Format versión 5 (OSF5)** que van más allá de la descripción general de OSF.  
Complementa el archivo [`osf_general.md`](../osf_general.md), en el que se explican todas las estructuras comunes a OSF4 y OSF5.

OSF5 es la evolución de OSF4. Utiliza **JSON** como formato estándar para el bloque de metadatos y simplifica el byte de control.  
Al mismo tiempo, OSF5 sigue siendo totalmente **compatible con versiones anteriores** respecto a OSF4.



## Magic Header en OSF5

- **Identificadores permitidos:**  
  - `OSF5`  
  - `OSF4` (por compatibilidad con versiones anteriores)  
  - `OCEAN_STREAM_FORMAT4` (identificador heredado, que los dispositivos suministrados siguen escribiendo)  
  - `OCEAN_STREAMING_FORMAT4` (grafía histórica más antigua)

- **Formato:**  
  ```
  OSF5 84512\n
  ```

- **Detección del bloque de metadatos:**  
  - Primer carácter `<` → XML (bloque de metadatos OSF4)  
  - Primer carácter `{` → JSON (bloque de metadatos OSF5)  

- **Propiedades:**  
  - De forma predeterminada, **bloque de metadatos JSON**.  
  - Los analizadores de OSF5 pueden leer XML de OSF4.



## Bloque de metadatos en OSF5 (JSON)

OSF5 utiliza **JSON** de forma predeterminada para el bloque de metadatos.  
Funcionalmente, la estructura se corresponde con la variante XML de OSF4, pero es más fácil de analizar y está optimizada para sistemas embebidos.

### Ejemplo:

```json
{
  "osf": {
    "version": 5,
    "created_utc": "2025-07-27T12:00:00Z",
    "creator": "smartdevice:15002000001",
    "file_uuid": "f47ac10b-58cc-4372-a567-0e02b2c3d479",
    "channels": [
      {
        "index": 0,
        "name": "Sensor.Temperature",
        "channeltype": "scalar",
        "datatype": "double",
        "timeincrement": 1000000,
        "sizeoflengthvalue": 2,
        "physicalunit": "°C"
      }
    ]
  }
}
```

- **Diferencia con OSF4:** JSON en lugar de XML, pero con el mismo contenido lógico.  
- **Compatibilidad:** OSF5 sigue pudiendo interpretar XML de OSF4.

#### Parámetro de ámbito de archivo `file_uuid`

El objeto `osf` puede incluir a nivel de archivo el parámetro **`file_uuid`**: un UUID (versión 4) almacenado como cadena JSON. Sirve para la **identidad del archivo** y es la interfaz a través de la cual el nivel superior del sistema lleva a cabo la comprobación de secuencia/repetición (replay) entre archivos (interfaz en el sentido de la norma EN 50159). `file_uuid` es **obligatorio** en el nivel de integridad *signed* y se **recomienda** en todos los demás niveles.


## Simplificaciones en el byte de control (tipos de bloque)

OSF5 adopta la estructura de bloques de OSF4, pero reduce el número de tipos de bloque utilizados y simplifica su interpretación.

### Cambios respecto a OSF4:

- `bcContinuedRelStampData` ya no se utiliza.  
- `bcStatusEvent` y `bcMessageEvent` ya no se generan. Que ya no se generen no significa que ya no se lean: los lectores deben seguir admitiendo `bcContinuedRelStampData` y `bcMessageEvent` **en cualquier versión**, porque existen en campo archivos con estos bloques; consulte [`osf_general.md`](../osf_general.md#bcmessageevent).  
- El bit 7 para valor único/múltiple permanece sin cambios.  

### Tipos de bloque admitidos en OSF5:

| Valor | Enum               | Descripción |
|------|--------------------|--------------|
| 0    | `bcReserved`       | Reservado para fines internos. |
| 2    | `bcTimebaseRealign`| Ajuste del eje de tiempo (poco utilizado). |
| 5    | `bcContinuedData`  | Continuar datos con frecuencia de muestreo fija. |
| 6    | `bcStartData`      | Bloque de inicio con frecuencia de muestreo fija. |
| 8    | `bcAbsTimeStampData` | Datos con marca de tiempo absoluta. |
| 9    | `bcIntegritySignature` | Ancla de firma de integridad (solo nivel *signed*, canal `0xFFFE`). Consulte el [perfil de integridad](osf5_integrity.md). Bit 7 = 0. |


## Perfil de integridad

OSF5 define un **perfil de integridad opcional de tres niveles** (none ⊂ crc ⊂ signed), que se declara mediante un token opcional en el Magic Header. El nivel *crc* añade a cada bloque una suma de verificación de trama (CRC32C); el nivel *signed* añade además una cadena de firmas Ed25519 con certificados X.509 incrustados en el bloque de metadatos. Sin un token declarado, un archivo permanece en el nivel *none* y se comporta exactamente como hasta ahora. La descripción normativa completa se encuentra en la especificación del [perfil de integridad](osf5_integrity.md).


## Tipos de datos admitidos en OSF5

OSF5 admite los mismos tipos de datos que OSF4.

| Tipo de datos | Tamaño   | Descripción |
|------------|----------|--------------|
| `bool`     | 1 byte   | Verdadero/falso |
| `int8`     | 1 byte   | Entero con signo |
| `int16`    | 2 bytes  | Entero con signo |
| `int32`    | 4 bytes  | Entero con signo |
| `int64`    | 8 bytes  | Entero con signo |
| `uint8`    | 1 byte   | Entero sin signo, intervalo de valores 0 … 255 |
| `uint16`   | 2 bytes  | Entero sin signo, intervalo de valores 0 … 65 535 |
| `uint32`   | 4 bytes  | Entero sin signo, intervalo de valores 0 … 4 294 967 295 |
| `uint64`   | 8 bytes  | Entero sin signo, intervalo de valores 0 … 18 446 744 073 709 551 615 |
| `float`    | 4 bytes  | IEEE 754 de precisión simple |
| `double`   | 8 bytes  | IEEE 754 de precisión doble |
| `string`   | variable | Codificado en UTF-8, longitud definida por el tamaño del bloque. Los escritores de OSF5 almacenan la carga útil sin byte nulo final; los lectores de OSF5 no deben eliminar ningún byte final; consulte [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary). |
| `binary` *(alias: `bytearray`)* | variable | Secuencias de bytes arbitrarias para imágenes, audio u otros datos binarios con tipo MIME. La longitud máxima se determina mediante `sizeoflengthvalue`. Los escritores de OSF5 almacenan la carga útil sin byte nulo final; los lectores de OSF5 no deben eliminar ningún byte final; consulte [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary). |
| `gpslocation` | 24 bytes | Estructura para posiciones GPS |

## Terminación de cadenas

Para `bcAbsTimeStampData` con `datatype=string` o `datatype=binary` en OSF5:

- **Los escritores NO DEBEN** añadir un byte nulo final (`0x00`). La carga útil termina en el último byte de datos; `sizeoflengthvalue` define la longitud exacta.
- **Los lectores NO DEBEN** eliminar ningún byte final. Un `0x00` final se trata como un byte de datos normal.

Consulte [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary) para la justificación.

### `bcStartData` con frecuencia de muestreo

Como en la especificación general y en OSF4, `bcStartData` en OSF5 incluye un campo `double` con la **frecuencia de muestreo (Hz)** inmediatamente después de la marca de tiempo inicial `int64`. Es válida para todos los bloques `bcContinuedData` posteriores del mismo canal, hasta que se escriba un nuevo bloque `bcStartData`. Se permiten expresamente varios bloques `bcStartData` por canal con huecos temporales entre ellos.

**Ejemplo de `bcStartData` en OSF5:**
`[int64 ZeitStart] [double SampleRate] [uint32 N] [double Wert1] [double Wert2] ... [double WertN]`

## Tráiler y bloque de información

OSF5 **ya no admite ningún bloque de datos de información (0xFFFF) ni Magic Trailer.**  
El final del archivo se define exclusivamente por el último bloque de datos escrito por completo.

- **Consecuencia:**  
  - Los analizadores leen el archivo hasta el último bloque consistente.  
  - No hay estadísticas ni metainformación adicionales al final del archivo.  
  - Para obtener información sobre el intervalo de tiempo es necesario analizar el archivo total o parcialmente.


### Por qué OSF5 ya no utiliza tráiler ni bloque de información

En OSF4 había opcionalmente al final del archivo un **bloque de datos de información (índice 0xFFFF)** y un **Magic Trailer**, para disponer rápidamente de metainformación y del intervalo de tiempo del archivo.  
En OSF5 hemos eliminado ambos elementos deliberadamente.

### Motivo principal: menor esfuerzo de implementación

- Escribir el bloque de información requiere una preparación final de todos los canales y de la información temporal.  
- Para el lado de lectura supone código adicional que debe mantenerse y probarse junto al flujo de datos normal.  
- Dado que OSF está concebido de todos modos para leer los archivos hasta el último bloque completo, el tráiler y el bloque de información no aportan ninguna ventaja técnica que justifique el esfuerzo.

### Ventajas adicionales

- **Especificación más sencilla:** menos casos especiales, implementación más ligera tanto en sistemas embebidos como en PC.  
- **Sin duplicación de información:** todos los metadatos relevantes están presentes en el bloque de metadatos y en los propios bloques de datos.  
- **Las herramientas de análisis pueden lograr lo mismo:** los intervalos de tiempo y las estadísticas pueden derivarse de los bloques normales al leer el archivo.

> **Conclusión:**  
> Prescindir del tráiler y del bloque de información en OSF5 es sobre todo una decisión en favor de una implementación más sencilla y fácil de mantener.  
> La robustez del formato se mantiene por completo: se deriva de la estructura de bloques y no de elementos adicionales al final del archivo.


## Particularidades y limitaciones de OSF5

- **JSON** como formato principal, XML solo por compatibilidad con versiones anteriores.  
- **Byte de control simplificado** con menos tipos de bloque.  
- Sin tráiler ni bloques de datos de información al final del archivo.  
- `bcContinuedRelStampData`, `bcStatusEvent` y `bcMessageEvent` ya no se generan; sin embargo, `bcContinuedRelStampData` y `bcMessageEvent` deben seguir leyéndose (consulte [`osf_general.md`](../osf_general.md#bcmessageevent)).



## Ejemplo de un archivo OSF5

```text
OSF5 2048
{
  "osf": {
    "version": 5,
    "created_utc": "2025-07-27T12:00:00Z",
    "creator": "smartdevice:15002000001",
    "channels": [
      {
        "index": 0,
        "name": "Sensor.Temperature",
        "channeltype": "scalar",
        "datatype": "double",
        "timeincrement": 1000000
      },
      {
        "index": 1,
        "name": "Sensor.Force",
        "channeltype": "scalar",
        "datatype": "double",
        "physicalunit": "N"
      },
      {
        "index": 2,
        "name": "Sensor.Path",
        "channeltype": "scalar",
        "datatype": "double",
        "physicalunit": "mm"
      }
    ]
  }
}
[BEGIN OF BINARY DATA]
```

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
