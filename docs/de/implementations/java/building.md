---
title: Bauen & Einbinden
description: Maven-Build der OSF-Java-Bibliothek — Voraussetzungen, Reaktor und Module, Einbindung ins eigene Projekt (Maven-Koordinaten + JPMS-requires), Abhängigkeiten, Tests und Veröffentlichung
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
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Bauen & Einbinden

Die maßgebliche, laufend gepflegte Bauanleitung ist die `README.md` im
Java-Verzeichnis (`implementations/java/`). Diese Seite fasst das
Wichtigste zusammen und ergänzt die Einbindungs-Szenarien für eigene
Projekte. Der Aufbau der Bibliothek ist in
[Architektur](./architecture.md) beschrieben.

## Voraussetzungen

- **JDK 21 (LTS)** — die Bibliothek ist auf Java 21 als Baseline
  kompiliert (`maven.compiler.release = 21`).
- **Maven 3.9+** — der Reaktor nutzt Standard-Plugins; ein `./mvnw`
  Maven-Wrapper liegt bei und lädt Maven beim ersten Lauf selbst nach.
- **Internet beim ersten Build** — Maven zieht die Abhängigkeiten
  (Jackson, SLF4J, Test-Bibliotheken) einmalig in den lokalen
  Repository-Cache (`~/.m2`); danach baut das Projekt offline.

**Java 21 ist keine Option,** sondern die fest definierte Baseline —
ein Wechsel auf ein neueres Release wäre ein bewusstes
Bibliotheks-Upgrade, kein Build-Schalter.

## Schnellstart

```bash
git clone https://github.com/optimeas/osf.git
cd osf

# Alle Tests bauen und ausführen (Unit + Konformanz + Round-Trip + Fuzz)
mvn -f implementations/java/pom.xml test

# JARs bauen (inkl. der Shaded-CLI und der Bibliothek)
mvn -f implementations/java/pom.xml package
```

`package` erzeugt für `osf-java` das Bibliotheks-JAR, für `osf-cli`
zusätzlich ein ausführbares Fat-JAR (`osf-cli.jar`) und für `osf-viewer`
das Viewer-JAR. Der Viewer wird typischerweise direkt gestartet:

```bash
mvn -f implementations/java/pom.xml -pl osf-viewer javafx:run
```

## Reaktor und Module

Der Build ist ein Maven-Reaktor. Das Eltern-POM
(`com.optimeas.osf:osf-parent:0.1.0-SNAPSHOT`, Packaging `pom`) bündelt
drei Module und zentralisiert Versionen sowie Plugin-Konfiguration:

| Modul (`artifactId`) | Packaging | Inhalt |
|---|---|---|
| `osf-java` | jar | Die eigentliche Bibliothek — OSF4/OSF5-Reader, beide OSF5-Writer, transparentes OSFZ, JPMS-Modul `com.optimeas.osf` |
| `osf-cli` | jar | Kommandozeilen-Werkzeug (picocli), gebaut als ausführbares Fat-JAR via Shade; Hauptklasse `com.optimeas.osf.cli.OsfCli` |
| `osf-viewer` | jar | JavaFX-Viewer für Mehrkanal-Darstellung; Hauptklasse `com.optimeas.osf.viewer.ViewerApp` |

Nur `osf-java` ist als Bibliotheks-Abhängigkeit gedacht; `osf-cli` und
`osf-viewer` sind Endanwendungen — siehe [Werkzeuge](./tools.md).

## In das eigene Projekt einbinden

Die Bibliothek wird als normale Maven-Abhängigkeit eingebunden:

```xml
<dependency>
  <groupId>com.optimeas.osf</groupId>
  <artifactId>osf-java</artifactId>
  <version>0.1.0-SNAPSHOT</version>
</dependency>
```

`osf-java` ist ein **echtes JPMS-Modul**. Wenn das eigene Projekt selbst
modular ist (`module-info.java`), muss das OSF-Modul explizit angefordert
werden:

```java
module meine.app {
    requires com.optimeas.osf;
}
```

Das Modul exportiert ausschließlich das Paket `com.optimeas.osf`; interne
Pakete sind bewusst gekapselt und nicht Teil der öffentlichen API — Details
in [Interna](./internals.md). Auf dem klassischen Classpath (ohne
`module-info.java`) funktioniert die Bibliothek unverändert, das Modul wird
dann als automatisches Modul geladen.

## Abhängigkeiten

Die Laufzeit-Abhängigkeiten sind bewusst schlank gehalten — zwei externe
Bibliotheken plus JDK-Bausteine:

| Abhängigkeit | Herkunft | Zweck |
|---|---|---|
| Jackson Databind 2.18.2 | extern (Maven) | OSF5-Metablock (JSON) parsen und serialisieren |
| SLF4J API 2.0.16 | extern (Maven) | Logging-Fassade — das Backend bringt die Anwendung mit |
| StAX (`java.xml`) | JDK | OSF4-Metablock (XML) streamend parsen |
| `java.util.zip` | JDK | transparente OSFZ-Dekompression (gzip/zlib) auf dem Lese-Pfad |

SLF4J ist nur eine Fassade: ohne eingebundenes Backend gibt die
Bibliothek nichts aus (No-op). `osf-cli` bindet `slf4j-simple` als
Laufzeit-Backend ein; eigene Anwendungen wählen ihr eigenes (Logback,
Log4j2, …). Fehlerbehandlung ist in [Fehlerbehandlung](./error-handling.md)
beschrieben.

Die Test-Abhängigkeiten (nur `test`-Scope, nicht transitiv) sind
**JUnit 5** (Jupiter), **AssertJ** (Assertions) und **jqwik 1.9.1**
(Property-basierte / Fuzz-Tests).

## Tests

```bash
mvn -f implementations/java/pom.xml test
```

Die Test-Ausführung übernimmt das **maven-surefire-plugin 3.5.2** mit dem
JUnit-5-Jupiter-Runner. Die Suite umfasst:

- **Unit-Tests** auf synthetischen Daten, die jede Schicht einzeln abdecken.
- **Konformanz-Tests** gegen die generierten Referenzdateien unter
  `examples/` — der Beweis, dass alle Implementierungen dieselben Dateien
  bitgenau lesen und schreiben (siehe [Lesen](./reading.md) und
  [Schreiben](./writing.md)).
- **Round-Trip-Tests**, die geschriebene Dateien wieder einlesen und
  Feld für Feld vergleichen.
- **Property-/Fuzz-Tests** (jqwik), die zufällige Kanal- und
  Block-Konstellationen erzeugen.

Erwartung: **alle Tests grün**. `mvn … verify` führt zusätzlich die
Verifikationsphase aus und entspricht dem CI-Lauf.

## Veröffentlichung

Die POMs sind **veröffentlichungsfertig** — Lizenz (MIT), SCM,
Entwickler-Metadaten und ein `release`-Profil sind hinterlegt. Das Profil
(`-Prelease`) hängt Sources- und Javadoc-JARs an, signiert alle Artefakte
per GPG (`maven-gpg-plugin`) und lädt sie über das
`central-publishing-maven-plugin` nach Maven Central hoch. Der
**Standard-Build signiert und deployt nichts.**

Die tatsächliche Veröffentlichung nach Maven Central ist derzeit
**zurückgestellt** — bis dahin wird die Bibliothek wie oben beschrieben
aus dem Quellcode gebaut. Praxisrezepte für den Einsatz stehen im
[Kochbuch](./cookbook.md); die Gesamtübersicht der Java-Implementierung
in der [Java-Übersicht](../java.md).

## Bekannte Stolpersteine

- **Firmen-Proxy / TLS-Interception:** Der `./mvnw`-Wrapper lädt Maven
  beim ersten Start selbst herunter; hinter einem TLS-abfangenden Proxy
  kann dieser Bootstrap mit Zertifikatsfehlern scheitern. Abhilfe: das
  system-installierte `mvn` verwenden (nutzt den System-Truststore) oder
  einen internen Mirror in `~/.m2/settings.xml` eintragen.
- **`module not found` / Split-Package:** Tritt der Fehler beim Bauen
  einer modularen Anwendung auf, fehlt meist die Zeile
  `requires com.optimeas.osf;` im eigenen `module-info.java`, oder es
  wird versucht, ein nicht exportiertes internes Paket zu importieren.
- **JavaFX beim Viewer:** `osf-viewer` benötigt die
  `javafx-controls`-Module (21.0.5); der Start erfolgt am einfachsten
  über das `javafx-maven-plugin` (`mvn -pl osf-viewer javafx:run`), das
  den Modulpfad korrekt setzt.

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
