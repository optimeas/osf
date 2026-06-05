---
title: Java-Implementierung
description: Geplante Java-Implementierung des Open Streaming Format — Architektur entschieden, noch kein Code
sidebar_position: 6
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - Maven
  - JPMS
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
---

🇬🇧 [English version](../../en/implementations/java.md)

# Java-Implementierung

:::info Status: geplant
Die Java-Implementierung ist **architektonisch entschieden, aber noch nicht
umgesetzt** — es existiert noch kein Code. Die folgenden Punkte
dokumentieren die getroffenen Entscheidungen; die verbindliche Quelle ist
[DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md).
:::

## Zielgruppen

Zwei Hauptzielgruppen: Enterprise-Backends (Spring, Microservices,
optiCloud) sowie Big-Data-/KI-Pipelines (Spark, Flink, Datenanalyse).
Hinzu kommt ein konkretes Embedded-Projekt, in dem eine Java-Anwendung
Betriebsdaten auf einem Industrie-Gateway aufzeichnet — dieselbe
„beide Welten"-Situation, die §7 bereits für C++ beschreibt.

## Geplante Architektur

- **Java 25** (aktuelles LTS) und **Maven** als Build-System. Ausgeliefert
  als ein einzelnes Maven-Artefakt (`groupId=com.optimeas.osf`,
  `artifactId=osf-java`).
- **JPMS** (Java Platform Module System): `module-info.java` exportiert nur
  `com.optimeas.osf`; interne Helfer unter `com.optimeas.osf.internal`
  bleiben gekapselt — auch gegen Reflection.
- **Beide Writer**, analog zu C++: ein `BlockWriter` (im Speicher sammeln,
  in einem Durchgang schreiben) für Batch-Workflows und ein
  `StreamingWriter` (`FileChannel.force(true)` pro Block) für die
  ausfallsichere Embedded-Aufzeichnung. Beide erzeugen on-disk-identische
  OSF5-Dateien.
- **Abhängigkeiten:** Jackson (OSF5-JSON), SLF4J (Logging-Fassade),
  `java.util.zip` (OSFZ, im JDK), StAX (OSF4-XML, im JDK). Binär-I/O über
  `ByteBuffer` auf `FileChannel` mit `LITTLE_ENDIAN`.

## Spezifikations-Konformität

Die Java-Implementierung folgt denselben semantischen Regeln wie Rust,
Python und C++ (Spec-Rev 2026-05-04): alle aktuellen Datentypen (unsigned
Typen über Java-Typ-Promotion bzw. `BigInteger` für den vollen Bereich),
explizite Ablehnung der entfernten Typen, `bytearray` als Lese-Alias für
`binary`, die versions­deterministische Null-Terminierungs-Regel für
`string`/`binary` und alle vier Magic-Header-Kennungen.

## Quellcode und weiterführende Informationen

- Architektur-Entscheidung: [DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
- Aktueller Status: [github.com/optimeas/osf](https://github.com/optimeas/osf)
- Format-Spezifikation: Kapitel [OSF-Format](../osf_general.md)
