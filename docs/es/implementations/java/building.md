---
title: Compilación e integración
description: Compilación con Maven de la biblioteca Java de OSF — requisitos, reactor y módulos, integración en el propio proyecto (coordenadas Maven + JPMS requires), dependencias, pruebas y publicación
sidebar_position: 6
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - Maven
  - JPMS
  - Build
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

# Compilación e integración

La guía de compilación de referencia, mantenida de forma continua, es el
`README.md` del directorio Java (`implementations/java/`). Esta página
resume lo más importante y añade los escenarios de integración para
proyectos propios. La estructura de la biblioteca se describe en
[Arquitectura](./architecture.md).

## Requisitos

- **JDK 21 (LTS)** — la biblioteca está compilada con Java 21 como base
  (`maven.compiler.release = 21`).
- **Maven 3.9+** — el reactor utiliza plugins estándar; se incluye un
  wrapper de Maven `./mvnw` que descarga Maven por sí mismo en la primera
  ejecución.
- **Internet en la primera compilación** — Maven descarga una sola vez las
  dependencias (Jackson, SLF4J, bibliotecas de pruebas) en la caché del
  repositorio local (`~/.m2`); después, el proyecto se compila sin conexión.

**Java 21 no es una opción,** sino la base definida de forma fija — pasar a
una versión más reciente sería una actualización deliberada de la
biblioteca, no un interruptor de compilación.

## Inicio rápido

```bash
git clone https://github.com/optimeas/osf.git
cd osf

# Alle Tests bauen und ausführen (Unit + Konformanz + Round-Trip + Fuzz)
mvn -f implementations/java/pom.xml test

# JARs bauen (inkl. der Shaded-CLI und der Bibliothek)
mvn -f implementations/java/pom.xml package
```

`package` genera para `osf-java` el JAR de la biblioteca, para `osf-cli`
además un Fat-JAR ejecutable (`osf-cli.jar`) y para `osf-viewer` el JAR del
visor. El visor se inicia normalmente de forma directa:

```bash
mvn -f implementations/java/pom.xml -pl osf-viewer javafx:run
```

## Reactor y módulos

La compilación es un reactor de Maven. El POM padre
(`com.optimeas.osf:osf-parent:0.1.0-SNAPSHOT`, empaquetado `pom`) agrupa
tres módulos y centraliza las versiones y la configuración de los plugins:

| Módulo (`artifactId`) | Empaquetado | Contenido |
|---|---|---|
| `osf-java` | jar | La biblioteca propiamente dicha — lector OSF4/OSF5, ambos escritores OSF5, OSFZ transparente, módulo JPMS `com.optimeas.osf` |
| `osf-cli` | jar | Herramienta de línea de comandos (picocli), compilada como Fat-JAR ejecutable mediante Shade; clase principal `com.optimeas.osf.cli.OsfCli` |
| `osf-viewer` | jar | Visor JavaFX para la representación multicanal; clase principal `com.optimeas.osf.viewer.ViewerApp` |

Solo `osf-java` está pensado como dependencia de biblioteca; `osf-cli` y
`osf-viewer` son aplicaciones finales — véase [Herramientas](./tools.md).

## Integración en el propio proyecto

La biblioteca se incorpora como una dependencia de Maven normal:

```xml
<dependency>
  <groupId>com.optimeas.osf</groupId>
  <artifactId>osf-java</artifactId>
  <version>0.1.0-SNAPSHOT</version>
</dependency>
```

`osf-java` es un **auténtico módulo JPMS**. Si el propio proyecto es
modular (`module-info.java`), el módulo OSF debe solicitarse de forma
explícita:

```java
module meine.app {
    requires com.optimeas.osf;
}
```

El módulo exporta exclusivamente el paquete `com.optimeas.osf`; los
paquetes internos están encapsulados deliberadamente y no forman parte de la
API pública — detalles en [Aspectos internos](./internals.md). En el
classpath clásico (sin `module-info.java`), la biblioteca funciona sin
cambios; en ese caso el módulo se carga como módulo automático.

## Dependencias

Las dependencias en tiempo de ejecución se han mantenido deliberadamente
reducidas — dos bibliotecas externas más componentes del JDK:

| Dependencia | Origen | Finalidad |
|---|---|---|
| Jackson Databind 2.18.2 | externa (Maven) | Analizar y serializar el metabloque OSF5 (JSON) |
| SLF4J API 2.0.16 | externa (Maven) | Fachada de registro — el backend lo aporta la aplicación |
| StAX (`java.xml`) | JDK | Análisis en streaming del metabloque OSF4 (XML) |
| `java.util.zip` | JDK | Descompresión OSFZ transparente (gzip/zlib) en la ruta de lectura |

SLF4J es solo una fachada: sin un backend incorporado, la biblioteca no
emite nada (no-op). `osf-cli` incorpora `slf4j-simple` como backend en
tiempo de ejecución; las aplicaciones propias eligen el suyo (Logback,
Log4j2, …). La gestión de errores se describe en
[Gestión de errores](./error-handling.md).

Las dependencias de prueba (solo ámbito `test`, no transitivas) son
**JUnit 5** (Jupiter), **AssertJ** (aserciones) y **jqwik 1.9.1** (pruebas
basadas en propiedades / fuzz).

## Pruebas

```bash
mvn -f implementations/java/pom.xml test
```

La ejecución de las pruebas corre a cargo del **maven-surefire-plugin
3.5.2** con el runner JUnit 5 Jupiter. La suite incluye:

- **Pruebas unitarias** con datos sintéticos que cubren cada capa por
  separado.
- **Pruebas de conformidad** frente a los archivos de referencia generados
  en `examples/` — la prueba de que todas las implementaciones leen y
  escriben los mismos archivos bit a bit (véase [Lectura](./reading.md) y
  [Escritura](./writing.md)).
- **Pruebas round-trip**, que vuelven a leer los archivos escritos y los
  comparan campo a campo.
- **Pruebas de propiedades/fuzz** (jqwik), que generan constelaciones
  aleatorias de canales y bloques.

Resultado esperado: **todas las pruebas en verde**. `mvn … verify` ejecuta
además la fase de verificación y equivale a la ejecución de CI.

## Publicación

Los POM están **listos para su publicación** — están definidos la licencia
(MIT), el SCM, los metadatos de desarrollo y un perfil `release`. El perfil
(`-Prelease`) adjunta los JAR de fuentes y de Javadoc, firma todos los
artefactos con GPG (`maven-gpg-plugin`) y los sube a Maven Central mediante
el `central-publishing-maven-plugin`. La **compilación estándar no firma ni
despliega nada.**

La publicación efectiva en Maven Central está actualmente **aplazada** —
hasta entonces, la biblioteca se compila a partir del código fuente como se
ha descrito anteriormente. Las recetas prácticas para su uso se encuentran
en el [Recetario](./cookbook.md); la visión de conjunto de la implementación
Java, en el [Resumen de Java](../java.md).

## Obstáculos conocidos

- **Proxy corporativo / interceptación TLS:** El wrapper `./mvnw` descarga
  Maven por sí mismo en el primer inicio; detrás de un proxy que intercepta
  TLS, este arranque puede fallar con errores de certificado. Solución:
  utilizar el `mvn` instalado en el sistema (usa el almacén de confianza del
  sistema) o registrar un mirror interno en `~/.m2/settings.xml`.
- **`module not found` / paquete dividido (split package):** Si el error
  aparece al compilar una aplicación modular, normalmente falta la línea
  `requires com.optimeas.osf;` en el propio `module-info.java`, o se
  intenta importar un paquete interno no exportado.
- **JavaFX en el visor:** `osf-viewer` necesita los módulos
  `javafx-controls` (21.0.5); lo más sencillo es iniciarlo mediante el
  `javafx-maven-plugin` (`mvn -pl osf-viewer javafx:run`), que establece
  correctamente la ruta de módulos.

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
