---
title: Java-Implementierung
description: Die osf-java-Bibliothek — vollständige Java-21-Implementierung des Open Streaming Format mit beiden OSF5-Writern, JPMS-Kapselung, transparentem OSFZ und dem crc-Integritätsprofil; dazu die Werkzeuge osf-cli und osf-viewer
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
  date: 2026-07-10
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

🇬🇧 [English version](../../en/implementations/java.md)

# Java-Implementierung

:::info Status: verfügbar
Die Java-Implementierung ist **vollständig umgesetzt und getestet**. Sie liest
OSF4 und OSF5, schreibt OSF5 mit beiden Writer-Modellen, liest OSFZ transparent
und unterstützt das crc-Integritätsprofil. Verbindliche Entscheidungsquellen
sind [DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
(Architektur) und [§24](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
(Integritätsprofil); der jeweils aktuelle Stand steht in
[STATUS.md](https://github.com/optimeas/osf/blob/main/STATUS.md).
:::

## Zielgruppen

Zwei Hauptzielgruppen: Enterprise-Backends (Spring, Microservices,
optiCloud) sowie Big-Data-/KI-Pipelines (Spark, Flink, Datenanalyse).
Hinzu kommt Embedded-Java, das Betriebsdaten auf einem Industrie-Gateway
aufzeichnet — dieselbe „beide Welten"-Situation, die §7 bereits für C++
beschreibt.

## Funktionsumfang

- **Lesen:** OSF4 (XML-Metablock) und OSF5 (JSON-Metablock), typisierter
  `DataManager` mit Kanal-/Segmentmodell; robuster Best-Effort-Reader.
- **Schreiben (OSF5):** **beide Writer** — ein `BlockWriter` (im Speicher
  sammeln, in einem Durchgang schreiben) für Batch-Workflows und ein
  `StreamingWriter` (`FileChannel.force(true)` pro Block) für die
  ausfallsichere Embedded-Aufzeichnung. Beide erzeugen **on-disk-identische**
  OSF5-Dateien.
- **Transparentes OSFZ:** gzip-verpackte Dateien werden beim Lesen automatisch
  dekomprimiert (`java.util.zip`, im JDK).
- **Integritätsprofil `crc`:** optional auf beiden Writern zuschaltbar; der
  Reader prüft Metablock- und Frame-CRC (siehe unten).

## Bauen und Verwenden

**Java 21** und **Maven**. Ausgeliefert als Maven-Artefakt
(`groupId=com.optimeas.osf`, `artifactId=osf-java`); die POM ist
veröffentlichungsfertig (der Deploy in ein öffentliches Repository ist noch
zurückgestellt).

```bash
# Aus dem Repository-Wurzelverzeichnis: den Java-Reaktor bauen und testen
mvn -f implementations/java/pom.xml test
```

**JPMS** (Java Platform Module System): `module-info.java` exportiert nur
`com.optimeas.osf`; interne Helfer unter `com.optimeas.osf.internal` bleiben
gekapselt — auch gegen Reflection.

**Abhängigkeiten:** Jackson (OSF5-JSON), StAX (OSF4-XML, im JDK), `java.util.zip`
(OSFZ + CRC32C, im JDK), SLF4J (Logging-Fassade). Binär-I/O über `ByteBuffer`
auf `FileChannel` mit `LITTLE_ENDIAN`.

## Module

Der Java-Reaktor umfasst neben der Kernbibliothek zwei Werkzeuge:

| Modul | Zweck |
|---|---|
| **`osf-java`** | Kernbibliothek — Lesen, beide Writer, OSFZ, Integritätsprofil. |
| **`osf-cli`** | Kommandozeilenwerkzeug (picocli): `info`, `channels`, `dump`, `convert`; als ausführbares Jar gebaut. |
| **`osf-viewer`** | JavaFX-Betrachter für mehrere Kanäle (Min/Max pro Pixel). Start: `mvn -pl osf-viewer javafx:run`. |

## Integritätsprofil (`crc`)

Optionales OSF5-Integritätsprofil auf Stufe `crc` (CRC32C, `java.util.zip.CRC32C`,
JDK-nativ; Prüfwert `0xE3069283`, byte-identisch zu Rust/C++/Delphi).

- **Reader:** `MagicHeaderParser` erkennt das `crc32c`-Token; `DataManager`
  prüft die Metablock-CRC vor dem Parsen, `BlockReader` prüft und entfernt die
  4-Byte-Frame-CRC vor der typisierten Auswertung (fail-closed). Signaturblöcke
  (Kanal `0xFFFE`) werden übersprungen und gezählt, sodass signierte Dateien
  lesbar bleiben. `ReaderStats` liefert `integrity` + `verificationStatus()`
  (`none`/`crc_valid`/`invalid`/`signature_unverifiable`).
- **Writer:** `setIntegrity(IntegrityProfile.CRC32C)` auf **beiden** Writern
  (Standard aus) schreibt das Token, die Metablock-CRC und je Block eine
  Frame-CRC.

Die Signaturstufe (`signed`, Ed25519) ist noch nicht implementiert.
Grundlagen: [DECISIONS §24](https://github.com/optimeas/osf/blob/main/DECISIONS.md).

## Spezifikations-Konformität

Die Java-Implementierung folgt denselben semantischen Regeln wie Rust, Python,
C++ und Delphi: alle aktuellen Datentypen (unsigned Typen über Java-Typ-Promotion
bzw. `BigInteger` für den vollen Bereich), explizite Ablehnung der entfernten
Typen, `bytearray` als Lese-Alias für `binary`, `channeltype` als **Datenform**
(scalar/vector/matrix/binary), die versionsdeterministische
Null-Terminierungs-Regel für `string`/`binary` und alle vier
Magic-Header-Kennungen. Die Konformität wird gegen den geteilten
Referenzmanifest-Kontrakt (`examples/reference_manifest.json`) geprüft.

## Quellcode und weiterführende Informationen

- Architektur-Entscheidung: [DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
- Integritätsprofil: [DECISIONS §24](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
- Aktueller Status: [STATUS.md](https://github.com/optimeas/osf/blob/main/STATUS.md) · [github.com/optimeas/osf](https://github.com/optimeas/osf)
- Format-Spezifikation: Kapitel [OSF-Format](../osf_general.md)

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
