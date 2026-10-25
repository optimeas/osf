---
title: Gestión de errores
description: La jerarquía de excepciones OsfException, el catálogo completo de errores, el modelo de lectura best-effort y el estado de integridad en ReaderStats
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - OsfException
  - Best-Effort
  - Integridad
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Gestión de errores

La implementación en Java notifica los errores mediante **excepciones**.
Todos los errores de la biblioteca son instancias de `OsfException`, que
hereda de `RuntimeException` — son, por tanto, excepciones **no
comprobadas**: sin obligación de cláusula `throws` y sin `try/catch`
forzados. Quien quiera capturarlas, captura de forma específica
`OsfException` (o una de sus subclases); quien no, las deja propagarse hasta
un manejador central.

Aparte de esto está el **lector best-effort**: un bloque de datos
individual defectuoso o truncado *no* detiene la lectura con una excepción,
sino que se omite y se contabiliza en `ReaderStats`. Solo lanzan excepción
los errores estructurales *anteriores* al flujo de bloques (cabecera,
metabloque) y las violaciones graves de precondiciones. Esta separación es
el núcleo de la gestión de errores: fallar con firmeza mientras el archivo
sea en principio ininterpretable — aguantar en cuanto solo se pierdan
algunos bloques al final.

## La jerarquía de excepciones `OsfException`

```java
package com.optimeas.osf;

public class OsfException extends RuntimeException {
    // Nachricht (und optional Ursache) — die Nachricht ist nur zur Anzeige
    public static final class UnsupportedType       extends OsfException { }
    public static final class MalformedFile         extends OsfException { }
    public static final class UnknownHeaderToken    extends OsfException { }
    public static final class MetablockCrcMismatch  extends OsfException { }
}
```

Reglas:

- **Ramificar según el tipo, mostrar el mensaje solo como información.**
  `getMessage()` es un detalle legible por personas y no forma parte de la
  API — el texto puede cambiar. Quien quiera reaccionar de forma
  programática comprueba la clase (`instanceof` / orden de los `catch`).
- Una `OsfException` **sin** una subclase más específica corresponde al caso
  «API válida, pero mal utilizada o error de E/S» — sobre todo procedente de
  los escritores.
- Cuando la causa es una `IOException` subyacente, se conserva como `cause`
  (`getCause()`), de modo que el stacktrace permanece completo.

## Catálogo de errores

### Al abrir y analizar (errores graves)

Estas excepciones se producen antes de que se llegue a leer ningún bloque de
datos útiles — el archivo no es interpretable como OSF y se rechaza por
completo.

| Excepción | Significado | Origen habitual |
|---|---|---|
| `MalformedFile` | Defecto estructural: Magic Header mal formado, identificador de versión desconocido, campo obligatorio ausente en el metabloque, número no analizable, `sizeoflengthvalue` no válido (≠ 2/4), fin de flujo inesperado, ausencia de salto de línea en la ventana de la cabecera, JSON (OSF5) o XML (OSF4) no válido. Si la causa es de E/S, conserva la `IOException` como `cause`. | Analizadores de cabecera, metabloque y bloques |
| `UnknownHeaderToken` | Un token del Magic Header cuya clave la biblioteca no conoce (regla must-understand). Se separa deliberadamente de `MalformedFile` para que un token desconocido de integridad/extensión no aparezca como un engañoso error de formato numérico. | Magic Header |
| `MetablockCrcMismatch` | El CRC32C declarado en el token `crc32c` de la cabecera no coincide con los bytes en bruto del metabloque. Con un perfil de integridad activo, el archivo se rechaza en modo fail-closed — los metadatos se consideran comprometidos. | Verificación del metabloque |
| `UnsupportedType` | El archivo utiliza un tipo de datos **eliminado** de la especificación (`pair`, `triple`, `candata`, `gpsdata`). Rechazo grave — el antiguo diseño de la carga útil no es reproducible desde una compilación actual. Sirve también como error de **acceso**: un getter de tipo incorrecto en `DataChannel` (p. ej. doubles de un canal de cadenas de texto). | Analizador de metabloque, `DataChannel` |

### Al escribir y al usar la API

Los escritores (`StreamingWriter`, `BlockWriter`) lanzan una `OsfException`
simple ante cualquier precondición incumplida — señalan un error de
programación, no un defecto de los datos:

| Causa | Significado de ejemplo |
|---|---|
| Índice de canal desconocido | Se escribió una muestra en un canal no declarado |
| Discrepancia de tipo | El tipo de escritura no coincide con el tipo de datos declarado del canal |
| Tipos de bloque mezclados | Un canal proporciona bloques equidistantes *y* con marca de tiempo — prohibido por la especificación |
| Fase incorrecta del ciclo de vida | Se escribió una muestra aunque el escritor aún se estaba configurando o ya estaba cerrado |
| Sin canal / longitud inconsistente | `begin`/`writeTo` sin canales declarados; `timestamps.length ≠ values.length` |
| Perfil firmado solicitado | Esta biblioteca de nivel CRC rechaza al escribir el perfil `ed25519` (firmado) |
| Error de E/S | El archivo no se puede abrir, error de escritura/`force` — `IOException` como `cause` |

## Lector best-effort: lo que deliberadamente **no** es un error

El lector del flujo de bloques se detiene allí donde un archivo todavía es
*en principio* legible, en lugar de lanzar una excepción. El resultado se
registra en `ReaderStats`, que se consulta mediante `manager.stats()`:

| Situación | Comportamiento |
|---|---|
| El archivo termina a mitad de un bloque (corte de corriente, truncamiento) | Se entregan todos los bloques completos, `stats.truncationSeen()` pasa a `true` y la iteración termina limpiamente |
| Cuerpo de bloque defectuoso/corrupto | Parada best-effort exactamente en ese punto: `truncationSeen()` = `true`, los demás bloques leídos siguen siendo válidos |
| Índice de canal desconocido en el flujo de bloques | Sin definición se desconoce el ancho del campo de longitud → parada (`truncationSeen()`) en lugar de intentar adivinarlo |
| Tipo de datos futuro desconocido | El canal es `UNSUPPORTED`; sus bloques se omiten según su longitud, todos los demás canales se cargan con normalidad |
| El CRC de trama de un bloque no coincide | El bloque se descarta, `stats.blocksCrcFailed()` se incrementa y la lectura continúa |
| Bloque de firma (canal reservado `0xFFFE`) | Esta biblioteca de nivel CRC no puede verificarlo → se omite, `stats.blocksSignatureSkipped()` se incrementa |
| Tipos de bloque reservados/vacíos | Se consumen como omitidos; no es un error |

Solo lanzan excepción los errores *anteriores* al flujo de bloques
(cabecera, CRC del metabloque, análisis del metabloque) — allí no es
defendible ninguna interpretación parcial.

## Estado de integridad — `ReaderStats.verificationStatus()`

Tras la carga, `stats.verificationStatus()` resume el resultado de
integridad en una cadena de texto estable (vocabulario de la
especificación):

| Valor | Significado | Reacción recomendada |
|---|---|---|
| `"none"` | El archivo no lleva perfil de integridad | Ninguna — procesamiento normal |
| `"crc_valid"` | Perfil `crc`, **todos** los CRC de bloque verificados | Tratar los datos como íntegros |
| `"invalid"` | Perfil `crc`, al menos un CRC de bloque falló (`blocksCrcFailed() > 0`) | Avisar/rechazar; los bloques afectados faltan en los datos |
| `"signature_unverifiable"` | Archivo firmado (`ed25519`) que esta biblioteca de nivel CRC **no** puede verificar | No presentarlo como «firmado de confianza»; los datos útiles siguen siendo legibles |

`verificationStatus()` se deriva únicamente del perfil declarado y de los
contadores — un `"invalid"` significa concretamente que los bloques con
error de CRC se omitieron (no se entregaron).

## `try/catch` en la práctica

```java
import com.optimeas.osf.*;

try {
    DataManager mgr = DataManager.loadFromFile(Path.of("messung.osf"));

    ReaderStats st = mgr.stats();
    if (st.truncationSeen()) {
        log.warn("Datei am Ende abgeschnitten — {} Blöcke gelesen", st.blocksRead());
    }
    switch (st.verificationStatus()) {
        case "invalid" -> log.error("CRC-Fehler: {} Blöcke verworfen", st.blocksCrcFailed());
        case "signature_unverifiable" -> log.warn("Signatur nicht prüfbar");
        default -> { /* none / crc_valid — ok */ }
    }

    double[] werte = mgr.channelByName("temperatur")
                        .orElseThrow()
                        .asDoubles();          // wirft UnsupportedType bei Typ-Mismatch
} catch (OsfException.MetablockCrcMismatch e) {
    // Metadaten kompromittiert — Datei ablehnen
} catch (OsfException e) {
    // MalformedFile, UnknownHeaderToken, UnsupportedType, I/O …
    log.error("OSF konnte nicht geladen werden: {}", e.getMessage(), e);
}
```

Reglas prácticas:

- **Capturar primero lo específico y luego lo general.** Primero las
  subclases específicas (`MetablockCrcMismatch`, `UnknownHeaderToken`) y
  después `OsfException` como red de seguridad.
- **Comprobar `stats()` tras la carga** — un `load` correcto no significa
  que hayan llegado todos los bloques. El truncamiento y los fallos de CRC
  son silenciosos y solo aparecen en los contadores.
- **Las llamadas a getters en `DataChannel` pueden lanzar
  `UnsupportedType`** si el tipo solicitado no corresponde al canal —
  compruebe `dataType()` antes o capture de forma específica.

## Ciclo de vida del escritor

El `StreamingWriter` impone una máquina de estados `CONFIGURE →
STREAMING → CLOSED`. Una llamada en la fase incorrecta —muestra antes de
`begin()`, escritura después de `close()`— lanza `OsfException`. `close()`
es idempotente (una segunda llamada es una no-op) y, incluso ante un error
de E/S en el flush final, pasa de forma fiable a `CLOSED`, de modo que
`try-with-resources` (el escritor es `AutoCloseable`) no deja el archivo
abierto.

Para más detalles, véanse [Lectura](./reading.md),
[Escritura](./writing.md), [Arquitectura](./architecture.md) y el
[manual del formato OSF](../../osf_general.md). Volver al
[resumen de Java](../java.md).

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
