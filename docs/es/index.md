---
title: Formato OSF
description: Introducción al Open Streaming Format
#hide_title: true/false
#hide_table_of_contents: true/false
#sidebar_label: Label shown in the sidebar
#sidebar_position: Position relative to its parent if needed
image: "/img/om_social_card.png"
keywords:
  - Formato de archivo
  - OSF4
  - OSF5
  - OSF
last_update:
  date: 2026-07-07
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH y optiMEAS Switzerland GmbH
---

## El formato OSF – Introducción

OSF está concebido como un formato de streaming universal para datos de medición y de proceso. Responde a las preguntas esenciales de la adquisición de datos moderna: ¿cómo pueden almacenarse sin pérdidas datos que fluyen de forma continua? ¿Cómo se mantiene el formato abierto y extensible, sin complejidad innecesaria? ¿Y cómo se combinan los requisitos de los sistemas embebidos con la eficiencia de potentes plataformas de análisis?

Actualmente OSF está disponible en la versión 4, mientras que la versión 5 se encuentra en fase de implementación. Aunque OSF4 y OSF5 se diferencian solo en unos pocos puntos, hemos optado deliberadamente por un salto de versión: sobre todo el cambio a un nuevo formato de cabecera (JSON como estándar) y el manejo simplificado del byte de control en los datos justifican esta nueva versión principal. Al mismo tiempo, OSF5 sigue siendo totalmente retrocompatible con OSF4 y puede leer y procesar sus archivos —salvo algunas diferencias de detalle— sin restricciones.

### Qué aporta OSF en el uso práctico

OSF está desarrollado para la realidad de los sistemas de medición modernos: flujos de datos que fluyen de forma continua y que deben adquirirse con rapidez y almacenarse de forma segura. Su eje central es uno: el registro sin lagunas y sin pérdida de datos, incluso cuando los sistemas se apagan bruscamente o se interrumpe la alimentación eléctrica.

Pero OSF no termina en la escritura: también está optimizado para el **procesamiento posterior.** En lugar de almacenar los valores individuales uno tras otro, OSF guarda los datos por bloques. Esto permite:

-   **almacenamiento eficiente y rápido** incluso con altas tasas de datos,
-   **carga y análisis rápidos** de grandes volúmenes de datos,
-   estructuras coherentes que funcionan con buen rendimiento tanto en sistemas embebidos como en plataformas de análisis.

### Ventajas de un vistazo:

-   **Registro de datos continuo y sin pérdidas** en dispositivos embebidos.
-   **Robustez** frente a cortes de corriente o apagados inesperados.
-   **Estructura de bloques eficiente** para un almacenamiento y una carga rápidos.
-   **Flexible** para todos los datos con referencia temporal: valores individuales, vectores, matrices, imágenes y datos de audio.
-   **Abierto y fácil de implementar**, con una estructura clara.
-   **Unificado** – un único formato para el edge, el laboratorio y la nube.
-   **Perfil de integridad opcional** (a partir de OSF5) con tres niveles – *none*, *crc*, *signed*: detección de corrupción de datos mediante CRC32C por bloques, una cadena de firmas Ed25519 y verificabilidad por terceros basada en X.509/PKI. Véase [Perfil de integridad de OSF5](references/osf5_integrity.md); documento conceptual: [doi.org/10.5281/zenodo.21227941](https://doi.org/10.5281/zenodo.21227941).

OSF no es, por tanto, solo un formato de almacenamiento, sino una herramienta para acompañar **los flujos de datos de forma eficiente y sin pérdidas a lo largo de todo su ciclo de vida** – desde el primer valor medido hasta el análisis.


## Una solución abierta para datos con referencia temporal

La respuesta de OSF a estos requisitos es una estructura clara y abierta, que se aplica por igual a **OSF4** y **OSF5**:

-   **Magic Header** para la identificación y el procesamiento rápido.
-   **Metabloque (XML o JSON)** para describir canales, tipos de datos e información de contexto.
-   **Bloques de datos binarios** para un streaming continuo y robusto.
-   **Almacenamiento orientado a bloques**, para escribir grandes volúmenes de datos de forma eficiente y volver a cargarlos con rapidez.

### Estructura básica de archivo de OSF

La arquitectura central permite almacenar todos los datos con referencia temporal en un único formato coherente. OSF está **documentado abiertamente**, es fácil de implementar y escala desde los dispositivos edge hasta la nube.

![Estructura de archivo de OSF.](media/dfa207f25f79c84ec79e2e2f6c2c42cc.jpg)

### Qué es diferente en OSF5

-   JSON como estándar para la cabecera (con retrocompatibilidad con OSF4/XML).
-   Uso simplificado del byte de control en el bloque de datos, para reducir el esfuerzo de implementación.
-   Supresión del tráiler en favor de una estructura de archivo más clara.
-   Magic Header flexibles (`OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) con detección automática de XML (`<`) o JSON (`{`).

A continuación se describe qué caracteriza a OSF como formato – **con validez general para OSF4 y OSF5**. Los detalles sobre funciones específicas y sobre canales de vectores o matrices se encuentran en documentos propios.

> Este documento está licenciado bajo [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Atribución: optiMEAS GmbH y optiMEAS Switzerland GmbH.
