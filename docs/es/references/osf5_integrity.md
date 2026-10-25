---
title: Perfil de integridad de OSF5
description: Especificación normativa del perfil de integridad opcional de OSF5 — detección de corrupción con CRC32C, cadena de firmas Ed25519 y prueba de procedencia X.509/PKI
image: "/img/om_social_card.png"
keywords:
  - OSF5
  - Integridad
  - CRC32C
  - Ed25519
  - Firma
  - PKI
last_update:
  date: 2026-07-08
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Perfil de integridad de OSF5

Este documento es la especificación **normativa** del **perfil de integridad**
opcional del Open Streaming Format versión 5 (OSF5). Complementa el formato base
con tres garantías escalonadas: detección de corrupción por bloque (CRC32C),
detección de manipulaciones y pruebas de procedencia aptas para streaming (una
cadena de hash SHA-256 con anclas de firma Ed25519 periódicas), así como una
verificabilidad por terceros sin conexión (un modelo de certificados X.509/PKI).

El perfil es una **propiedad exclusiva de OSF5**. Los archivos OSF4 no se ven
afectados y nunca incluyen ninguna parte del mismo. El diseño es totalmente
compatible con versiones anteriores: los archivos OSF5 *sin* declaración de
integridad siguen siendo válidos sin cambios.

:::info Documento conceptual
La justificación del diseño de este perfil (la escalera de tres niveles, la
semántica fail-closed de los tokens, la cadena de hash con anclas de firma y el
modelo de confianza PKI) figura en el documento conceptual *Integridad y firma en
formatos de datos de medición en streaming: el enfoque de OSF5*, publicado en
Zenodo en **alemán e inglés** (DOI
[10.5281/zenodo.21227941](https://doi.org/10.5281/zenodo.21227941), CC BY 4.0)
y en este repositorio en
[`docs/papers/`](https://github.com/optimeas/osf/tree/main/docs/papers/).
:::

El documento conceptual ofrece el contexto; **este documento es la referencia
normativa** para los implementadores.

El formato base (Magic Header, bloque de metadatos JSON, bloques de datos
autocontenidos) se describe en [`osf_general.md`](../osf_general.md) y en
[`osf5.md`](osf5.md); este documento solo añade la capa de integridad.

---

## 1. Modelo de tres niveles

El perfil es una **escalera estrictamente ordenada**; cada nivel incluye el
inferior:

```
none  ⊂  crc  ⊂  signed
```

El nivel se aplica **por archivo** y se declara **exactamente una vez**.

| Nivel | Protege contra | Uso típico |
|---|---|---|
| **none** | — | Laboratorio, archivos intermedios, escritores embebidos mínimos |
| **crc** | Corrupción (errores de bit, truncamiento, fallos del medio) | Estándar para datos de campo |
| **signed** | Corrupción **y** manipulación; procedencia demostrable ante terceros | Datos de explotación con valor probatorio |

Dos reglas de diseño evitan una fragmentación del formato:

1. **Los escritores eligen el nivel libremente**: los sistemas embebidos conservan
   la opción de un escritor mínimo.
2. **Todo lector OSF5 conforme DEBE procesar todos los niveles.** El resultado es
   un único formato con perfil, no tres dialectos.

La granularidad es siempre el **archivo completo**: o bien todos los bloques
llevan el mecanismo, o bien ninguno.

---

## 2. Gramática de los tokens de cabecera (normativa)

La línea del Magic Header se amplía, tras la longitud del bloque de metadatos,
con cero o más tokens opcionales:

```abnf
header-line     = identifier SP metablock-len *(SP token) LF
token           = key ":" value
key             = 1*(a-z / 0-9 / "-")          ; nur Kleinbuchstaben
value           = 1*(VCHAR ohne SP)
```

- Un único `SP` (0x20) separa los campos; antes del `LF` **no hay ningún espacio
  final**.
- Los tokens son **«must understand»**: un lector que no conozca una `key`
  **DEBE rechazar el archivo**, con un diagnóstico de la forma
  `unbekanntes Header-Token '<key>'`, **no** como error de análisis numérico. (Los
  analizadores actuales rechazan un token inesperado solo de forma incidental,
  mediante un engañoso mensaje «la longitud no es un número válido»; consulte el
  anexo de migración.)
- Los tokens de cabecera son una característica **exclusiva de OSF5**. Los
  identificadores de OSF4 (`OSF4`, `OCEAN_STREAM_FORMAT4`,
  `OCEAN_STREAMING_FORMAT4`) NO DEBEN llevar tokens; un token tras un
  identificador de OSF4 es un archivo defectuoso.

### Keys definidas en esta revisión

| Key | Nivel | Valor |
|---|---|---|
| `crc32c` | crc | `crc32c:<8 HEXDIG en mayúsculas>`: la CRC32C (Castagnoli) sobre los **bytes sin procesar del bloque de metadatos**, exactamente como están en el archivo |
| `ed25519` | signed | `ed25519:<keyid>`: `keyid` = 16 HEXDIG **en minúsculas** = los primeros 8 bytes de SHA-256 sobre el `SubjectPublicKeyInfo` codificado en DER del certificado del dispositivo |

- **El uso de mayúsculas y minúsculas en hexadecimal es estricto**: `crc32c`
  utiliza hexadecimal en **mayúsculas**; la `keyid` de `ed25519` utiliza
  hexadecimal en **minúsculas**. Los lectores DEBEN comprobar estrictamente el
  uso de mayúsculas y minúsculas.
- El token `ed25519` solo está permitido **además de** `crc32c` y solo en este
  orden: primero `crc32c`, después `ed25519`. Por tanto, `signed` implica siempre
  `crc`.
- El bloque de metadatos es la única parte del archivo que no puede protegerse a
  sí misma; el token `crc32c` lleva su suma de verificación para que todo el
  orden de verificación sea determinista.

### Orden de verificación (normativo)

1. Leer los tokens de cabecera.
2. Comprobar la CRC del bloque de metadatos (a partir del token `crc32c`).
3. Analizar el bloque de metadatos.
4. Comprobar los bloques de datos (CRC de trama y, en el nivel signed, la cadena de firmas).

---

## 3. CRC de trama (nivel crc)

En el nivel crc, **cada** bloque lleva una **CRC32C** (Castagnoli; acelerada por
hardware en x86/SSE4.2 y ARMv8) sobre la **trama completa**: índice de canal,
campo de longitud, byte de control y datos útiles.

Incluir deliberadamente el **campo de longitud** en el alcance es importante: un
campo de longitud alterado es el error individual más grave, porque después el
lector pierde todos los límites de bloque.

La CRC ocupa los **últimos cuatro bytes del área de datos** y **se cuenta en el
campo de longitud**, de modo que el entramado permanece intacto para cualquier
lector. La longitud efectiva de la carga útil es la longitud del bloque **menos
cuatro**.

```
ohne Profil:   [Kanalindex][Längenfeld][Steuerbyte][ Nutzdaten ................ ]
                                       |<------------- LEN ------------------>|

mit  Profil:   [Kanalindex][Längenfeld][Steuerbyte][ Nutzdaten ....... ][CRC32C]
                                       |<------------- LEN ------------------>|
               |<================ Scope der Frame-CRC ====================>|
```

### Requisito normativo de implementación (entramado fail-closed)

Con el perfil activo, la CRC de trama es **parte del entramado** y DEBE
separarse **antes** de la evaluación tipada de los datos útiles. Una comprobación
«resto == 0» *posterior* a la decodificación **no es suficiente**: con tipos de
datos de longitud variable (`string`, `binary`), un lector que no tenga en cuenta
la integridad no puede distinguir los bytes de CRC añadidos de la carga útil real
y entregaría valores corruptos. De este modo, el comportamiento fail-open queda
excluido por construcción: el lector separa los últimos cuatro bytes basándose
en el conocimiento del perfil obtenido de la cabecera/bloque de metadatos y los
verifica.

### Comportamiento ante errores

| Condición | Reacción |
|---|---|
| Error de CRC de un bloque de datos | el bloque es **inválido** → omitirlo, **contarlo en las estadísticas de lectura** y continuar (más vale datos parciales que ninguno) |
| Error de CRC del bloque de metadatos | **rechazar el archivo** (sin un bloque de metadatos fiable no se puede interpretar nada) |
| Token de cabecera desconocido | **rechazar el archivo** (consulte §2) |

**Recomendación para las implementaciones:** además de la separación obligatoria,
una comprobación estricta de consumo completo para los bloques **numéricos**
(`N × tamaño de muestra (+ campos de cabecera)` debe corresponder a la longitud
efectiva de la carga útil). Es una mejora de la calidad del diagnóstico, no un
requisito de corrección.

---

## 4. Bloque de firma `bcIntegritySignature = 9` (nivel signed)

El nivel signed añade un nuevo tipo de byte de control y una cadena de hash
continua.

### El bloque

- Nuevo valor de byte de control **9** (`bcIntegritySignature`); solo es válido
  si el archivo declara el nivel **signed**; **bit 7 = 0** (semántica de valor
  único).
- **Índice de canal: el valor reservado `0xFFFE`**: un canal de integridad de
  ámbito de archivo que **no** se declara en el bloque de metadatos. Los lectores
  sin compatibilidad con el nivel signed omiten el bloque mediante el campo de
  longitud, igual que cualquier otro tipo de bloque desconocido (consulte §5).
  `0xFFFE` es distinto del canal de información/tráiler `0xFFFF` de OSF4.
- Los bloques del canal reservado `0xFFFE` utilizan **siempre un campo de
  longitud de 4 bytes (`uint32`)**, con independencia de las declaraciones de
  canal, de forma análoga al histórico bloque de información `0xFFFF`.
- Los propios bloques de firma llevan una **CRC de trama** como cualquier otro
  bloque.

### Carga útil (little-endian; orden normativo)

| # | Campo | Tipo | Significado |
|---|---|---|---|
| 1 | `anchor_seq` | `uint32` | Número de secuencia del ancla, consecutivo a partir de 0 |
| 2 | `signing_time_ns` | `int64` | Momento de la firma (base de la semántica de validez) |
| 3 | `chain_hash` | `byte[32]` | H(i), el hash de cadena actual |
| 4 | `signature` | `byte[64]` | Ed25519 sobre `SHA-256(anchor_seq ‖ signing_time_ns ‖ chain_hash)`, con los campos en el orden y la codificación de la transmisión (wire) |
| 5 | `keyid_len` + `keyid` | `uint8` + `byte[keyid_len]` | Referencia de clave/certificado; `keyid` como en el token de cabecera (8 bytes) |

### Cadena de hash

```
H(0) = SHA256(Header-Zeile ‖ Metablock)
H(i) = SHA256(H(i−1) ‖ Frame_i)
```

- Las tramas entran en la cadena **incluyendo** su CRC de trama.
- **Los propios bloques de firma también entran en la cadena** (como un `Frame_i`).
- Por tanto, **la primera ancla cubre también la cabecera y el bloque de metadatos** a través de H(0).

Además de la comprobación de bloques individuales, la cadena detecta también la
**eliminación**, la **inserción** y la **reordenación** de bloques; los números
de secuencia de las anclas detectan la **repetición dentro del archivo**.

### Cadencia

Configurable (basada en tiempo o en bloques). El **valor predeterminado
normativo es basado en tiempo, 10 segundos**. Además, es **obligatoria** un ancla
en el **cierre regular del archivo**, de modo que un archivo cerrado
correctamente esté firmado por completo.

### Semántica ante un corte de alimentación (normativa)

Si la grabación se interrumpe bruscamente, el archivo está **firmado hasta la
última ancla válida**; la parte posterior es **válida según CRC, pero sin
firmar**. El informe de verificación DEBE indicar ambas cosas (p. ej., «firmado
hasta la marca de tiempo X, resto válido según CRC, sin firmar»).

---

## 5. Certificados y PKI (nivel signed)

Para que **cualquier tercero**, y no solo el fabricante, pueda comprobar la
procedencia, las claves de dispositivo son certificadas por una autoridad de
certificación (X.509 con Ed25519 según RFC 8410), de forma análoga al modelo de
confianza de HTTPS, pero como jerarquía privada y documentada públicamente: una
**CA raíz** protegida por hardware y sin conexión, una CA emisora (vinculada a la
producción o a la nube de dispositivos) y, por debajo, certificados de
dispositivo cuyo Subject contiene el número de serie del dispositivo.

### Lugar de incrustación: el bloque de metadatos

La cadena de certificados se incrusta una vez por archivo como un nuevo **objeto
opcional en el nivel `osf`** del bloque de metadatos:

```json
"integrity": {
  "certificates": ["<base64 DER Gerätezertifikat>", "<base64 DER Zwischenzertifikat>"]
}
```

- **Primero la hoja (leaf)**; **la raíz nunca se incrusta**: el ancla de
  confianza debe proceder por principio del exterior (el certificado raíz
  publicado públicamente, suministrado con las herramientas de verificación y
  listado en el sitio web y en el repositorio abierto, en cada caso con su
  huella digital).
- Dado que el objeto se encuentra en el bloque de metadatos, los certificados
  quedan cubiertos por la **CRC del bloque de metadatos** y por **H(0)**.
- **Nota:** este objeto son **datos, no una declaración de perfil.** La
  declaración sigue siendo únicamente el token de cabecera (§2). Un archivo puede
  llevar un objeto `integrity.certificates` y, aun así, estar en el nivel crc si
  no hay ningún token de cabecera `ed25519`.

### Validez: «válido en el momento de la firma»

Los datos de medición sobreviven a la vigencia de los certificados. Por ello, la
semántica de verificación es: **¿era válido el certificado en el momento de la
firma (`signing_time_ns`)?** Un archivo grabado en 2027 sigue pudiendo
verificarse positivamente en 2040. Junto con vigencias largas de los certificados
de dispositivo, esto resulta adecuado en la práctica para datos de medición. La
debilidad residual teórica (retrodatación con una clave de dispositivo sustraída
y caducada) está documentada y, si es necesario, puede cerrarse mediante **sellos
de tiempo RFC 3161** sin modificar el formato: el sello de tiempo se asocia a la
firma, no a los bloques de datos.

### Revocación

Las revocaciones se realizan mediante **listas de revocación firmadas** con
semántica de **mejor esfuerzo (best effort)**; el informe de verificación indica
explícitamente el estado de la comprobación de revocación.

### Clases de certificados y política de transformación

- Los **certificados de dispositivo** y los **certificados de organización o de
  herramienta** forman **clases separadas y distinguibles.**
- **Las transformaciones finalizan el dominio de firma, a propósito.**
  Combinar, convertir y exportar generan por principio archivos sin firma de
  dispositivo. Las herramientas se comportan de forma definida: el **resultado
  retrocede al nivel crc** y registra su **procedencia** (archivos de origen con
  su estado de verificación) en los metadatos (`infos`). Opcionalmente, una
  organización puede **volver a firmar** el resultado con un certificado propio;
  esta es una **declaración distinta, identificada como tal**, respecto a la
  firma del dispositivo.

---

## 6. Estado de verificación (vocabulario normativo para herramientas)

Las herramientas de verificación notifican exactamente uno de los siguientes
estados:

| Estado | Significado |
|---|---|
| `valid_signed` | firmado por completo y válido |
| `partially_signed` | firmado hasta el ancla X; el resto es válido según CRC, pero está sin firmar |
| `crc_valid` | el nivel CRC se cumple; sin firma (válida) |
| `invalid` | ha fallado una comprobación de CRC o de firma donde debe cumplirse |
| `signature_unverifiable` | las firmas no pueden verificarse (p. ej., falta la biblioteca criptográfica o el certificado raíz); el archivo sigue siendo legible y sigue siendo posible una declaración del nivel CRC |

---

## 7. Clasificación respecto a DIN EN 50159 (informativa)

> Esta sección es **informativa**. La EN 50159 considera la comunicación
> relevante para la seguridad en sistemas de transmisión de la señalización
> ferroviaria; la comparación se ofrece porque el perfil es relevante para
> entornos ferroviarios, aunque se deriva de la Ley de Ciberresiliencia (Cyber
> Resilience Act) y aborda **archivos en reposo** en lugar de canales de
> transmisión.

| Amenaza (EN 50159) | Mecanismo en el perfil OSF5 |
|---|---|
| Corrupción | CRC32C de trama por bloque; criptográficamente: cadena de hash |
| Supresión | Cadena de hash (un bloque ausente rompe la cadena) |
| Inserción | Cadena de hash y firma (un bloque ajeno rompe la cadena) |
| Reordenación | Cadena de hash (el orden forma parte de la construcción de la cadena) |
| Suplantación | Firma Ed25519 con certificado de dispositivo y cadena de CA |
| Repetición | Números de secuencia de ancla (dentro del archivo); identidad del archivo mediante un **UUID de archivo** único en el bloque de metadatos como ancla para la comprobación a nivel de sistema |
| Retardo | no aplicable a datos en reposo |

Cabe señalar dos delimitaciones:

1. El perfil aborda la **repetición y la supresión a nivel de archivo**
   (reintroducción de archivos antiguos, desaparición de archivos completos)
   deliberadamente solo mediante la **interfaz del UUID de archivo**. La
   continuidad sin lagunas entre los límites de los archivos es tarea del **nivel
   de sistema** superior (el entorno de medición, su supervisión de secuencia y su
   fuente de tiempo segura) y se resuelve allí. El parámetro `file_uuid` del
   bloque de metadatos está especificado en [`osf5.md`](osf5.md): obligatorio en
   el nivel signed, recomendado en los demás.
2. El perfil es un mecanismo de **seguridad informática (security)** (en el
   sentido de un entorno de categoría 3 / redes abiertas) y **no pretende ofrecer
   ninguna garantía de seguridad funcional (safety)**: no sustituye una
   transmisión orientada a la seguridad según EN 50159 y no justifica ninguna
   idoneidad SIL.

---

## 8. Indicaciones de migración (informativas)

Resumido a partir de la auditoría de analizadores `AUDIT_INTEGRITY_O1.md` (raíz
del repositorio). El esfuerzo que implica el perfil para cada implementación:

**Las cuatro implementaciones (Rust, Delphi, C++, Java)**
- Separar la CRC de trama **antes** del analizador tipado, controlado por la
  declaración del perfil (entramado fail-closed, §3). Hoy en día todos los
  lectores descartan en silencio los bytes numéricos sobrantes o absorben lo
  sobrante en un valor string/binary, de modo que una CRC añadida de forma ingenua
  sería ignorada o corrompería el valor.

**Delphi**
- **Endurecer el tokenizador de cabecera**: hoy **no** rechaza tokens finales
  desconocidos (separa por espacios y descarta en silencio todo lo que sigue a la
  longitud). Debe rechazar conforme a §2.
- **Cambiar los bytes de control desconocidos a omitir y continuar (skip-and-continue)**:
  hoy un tipo de bloque desconocido es un abandono suave (soft abort), etiquetado
  erróneamente como «truncamiento»; debe omitir un bloque `bcIntegritySignature`
  mediante el campo de longitud y continuar.

**Rust / C++ / Java**
- Hacer que el rechazo de tokens de cabecera desconocidos sea **intencionado** y
  mejorar el diagnóstico («unexpected trailing token / unbekanntes Header-Token»
  en lugar de un error de análisis numérico). Las tres ya omiten correctamente los
  bytes de control desconocidos (valor 9) mediante el campo de longitud, por lo
  que ahí no es necesario ningún cambio.

**Java**
- Planificar el trabajo de integridad como una **implementación completa** (el
  núcleo de Java es un lector completo de OSF4+OSF5, no un stub): reestructuración
  del tokenizador de cabecera (esfuerzo **M**) y la ruta greedy de string/binary
  (esfuerzo **L**) para la separación de la CRC.

---

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
