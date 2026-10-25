---
title: Implementación en Java
description: La biblioteca osf-java — implementación completa en Java 21 del Open Streaming Format con ambos escritores OSF5, encapsulación JPMS, OSFZ transparente y perfil de integridad crc; además las herramientas osf-cli y osf-viewer
sidebar_position: 6
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - Maven
  - JPMS
  - JavaFX
  - crc32c
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Implementación en Java

:::info Estado: disponible
La implementación en Java está **completamente realizada y probada**. Lee
OSF4 y OSF5, escribe OSF5 con ambos modelos de escritor, lee OSFZ de forma
transparente y admite el perfil de integridad crc. Las fuentes de decisión
vinculantes son [DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
(arquitectura) y [§24](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
(perfil de integridad); el estado más reciente figura en
[STATUS.md](https://github.com/optimeas/osf/blob/main/STATUS.md).
:::

:::tip Manual para desarrolladores
Esta página es la visión general. La documentación detallada para
desarrolladores se encuentra en el subcapítulo **Java en detalle**:

| Página | Contenido |
|---|---|
| [Arquitectura](java/architecture.md) | Modelo de capas, módulos, encapsulación JPMS, modelos de datos, convenciones |
| [Lectura](java/reading.md) | `DataManager`, `DataChannel`, `BlockReader`, `ReaderStats`, OSFZ transparente |
| [Escritura](java/writing.md) | `StreamingWriter`, `BlockWriter`, `ChannelDef`, valores predeterminados de metadatos, perfil de integridad |
| [Gestión de errores](java/error-handling.md) | Jerarquía `OsfException`, `verificationStatus()`, lector de mejor esfuerzo |
| [Herramientas](java/tools.md) | `osf-cli` (verbos de picocli) y `osf-viewer` (JavaFX) |
| [Compilación e integración](java/building.md) | Maven, Java 21, JPMS, dependencias, pruebas |
| [Recetario](java/cookbook.md) | Recetas listas para copiar, desde la inspección hasta el bucle embebido |
| [Internos](java/internals.md) | Codificadores, chunking, utilidades de integridad, analizadores — para colaboradores |
:::

## Destinatarios

Dos destinatarios principales: backends empresariales (Spring,
microservicios, optiCloud) y canalizaciones de big data e IA (Spark, Flink,
análisis de datos). A ello se suma Java embebido, que registra datos de
funcionamiento en una pasarela industrial: la misma situación de «ambos
mundos» que §7 ya describe para C++.

## Alcance funcional

- **Lectura:** OSF4 (metabloque XML) y OSF5 (metabloque JSON), `DataManager`
  tipado con modelo de canales y segmentos; lector robusto de mejor esfuerzo.
- **Escritura (OSF5):** **ambos escritores**: un `BlockWriter` (recopila en
  memoria y escribe de una sola vez) para flujos de trabajo por lotes y un
  `StreamingWriter` (`FileChannel.force(true)` por bloque) para el registro
  embebido resistente a fallos. Ambos generan archivos OSF5 **idénticos en
  disco**.
- **OSFZ transparente:** los archivos empaquetados con gzip se descomprimen
  automáticamente al leer (`java.util.zip`, incluido en el JDK).
- **Perfil de integridad `crc`:** puede activarse opcionalmente en ambos
  escritores; el lector comprueba el CRC del metabloque y de las tramas
  (véase más abajo).

## Compilación y uso

**Java 21** y **Maven**. Se entrega como artefacto de Maven
(`groupId=com.optimeas.osf`, `artifactId=osf-java`); el POM está listo para
su publicación (el despliegue en un repositorio público está aún aplazado).

```bash
# Aus dem Repository-Wurzelverzeichnis: den Java-Reaktor bauen und testen
mvn -f implementations/java/pom.xml test
```

**JPMS** (Java Platform Module System): `module-info.java` exporta solo
`com.optimeas.osf`; las utilidades internas de `com.optimeas.osf.internal`
permanecen encapsuladas, también frente a la reflexión.

**Dependencias:** Jackson (JSON de OSF5), StAX (XML de OSF4, incluido en el
JDK), `java.util.zip` (OSFZ + CRC32C, incluido en el JDK), SLF4J (fachada de
registro). E/S binaria mediante `ByteBuffer` sobre `FileChannel` con
`LITTLE_ENDIAN`.

## Módulos

Además de la biblioteca principal, el reactor de Java incluye dos
herramientas:

| Módulo | Finalidad |
|---|---|
| **`osf-java`** | Biblioteca principal: lectura, ambos escritores, OSFZ, perfil de integridad. |
| **`osf-cli`** | Herramienta de línea de comandos (picocli): `info`, `channels`, `dump`, `convert`; se genera como jar ejecutable. |
| **`osf-viewer`** | Visor JavaFX para varios canales (mín./máx. por píxel). Inicio: `mvn -pl osf-viewer javafx:run`. |

## Perfil de integridad (`crc`)

Perfil de integridad opcional de OSF5 en el nivel `crc` (CRC32C,
`java.util.zip.CRC32C`, nativo del JDK; valor de comprobación `0xE3069283`,
idéntico byte a byte a Rust/C++/Delphi).

- **Lector:** `MagicHeaderParser` reconoce el token `crc32c`; `DataManager`
  comprueba el CRC del metabloque antes del análisis, `BlockReader` comprueba
  y elimina el CRC de trama de 4 bytes antes de la evaluación tipada
  (fail-closed). Los bloques de firma (canal `0xFFFE`) se omiten y se
  cuentan, de modo que los archivos firmados siguen siendo legibles.
  `ReaderStats` proporciona `integrity` + `verificationStatus()`
  (`none`/`crc_valid`/`invalid`/`signature_unverifiable`).
- **Escritor:** `setIntegrity(IntegrityProfile.CRC32C)` en **ambos**
  escritores (desactivado de forma predeterminada) escribe el token, el CRC
  del metabloque y un CRC de trama por cada bloque.

El nivel de firma (`signed`, Ed25519) aún no está implementado.
Fundamentos: [DECISIONS §24](https://github.com/optimeas/osf/blob/main/DECISIONS.md).

## Conformidad con la especificación

La implementación en Java sigue las mismas reglas semánticas que Rust,
Python, C++ y Delphi: todos los tipos de datos actuales (los tipos sin signo
mediante promoción de tipos de Java o `BigInteger` para el rango completo),
rechazo explícito de los tipos eliminados, `bytearray` como alias de lectura
de `binary`, `channeltype` como **forma de los datos**
(scalar/vector/matrix/binary), la regla de terminación en nulo determinista
según la versión para `string`/`binary` y los cuatro identificadores de la
cabecera mágica. La conformidad se comprueba con el contrato del manifiesto
de referencia compartido (`examples/reference_manifest.json`).

## Código fuente e información adicional

- Decisión de arquitectura: [DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
- Perfil de integridad: [DECISIONS §24](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
- Estado actual: [STATUS.md](https://github.com/optimeas/osf/blob/main/STATUS.md) · [github.com/optimeas/osf](https://github.com/optimeas/osf)
- Especificación del formato: capítulo [Formato OSF](../osf_general.md)

> Este documento se distribuye bajo la licencia [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
