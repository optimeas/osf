---
title: Build e integrazione
description: Build Maven della libreria OSF Java — prerequisiti, reactor e moduli, integrazione nel proprio progetto (coordinate Maven + requires JPMS), dipendenze, test e pubblicazione
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
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Build e integrazione

La guida di build di riferimento, costantemente aggiornata, è il file
`README.md` nella directory Java (`implementations/java/`). Questa
pagina riassume gli aspetti essenziali e integra gli scenari di
integrazione nei propri progetti. La struttura della libreria è
descritta in [Architettura](./architecture.md).

## Prerequisiti

- **JDK 21 (LTS)** — la libreria è compilata con Java 21 come baseline
  (`maven.compiler.release = 21`).
- **Maven 3.9+** — il reactor utilizza plugin standard; è incluso un
  Maven wrapper `./mvnw` che scarica Maven autonomamente alla prima
  esecuzione.
- **Internet alla prima build** — Maven scarica le dipendenze (Jackson,
  SLF4J, librerie di test) una sola volta nella cache del repository
  locale (`~/.m2`); successivamente il progetto si compila offline.

**Java 21 non è un'opzione,** ma la baseline definita in modo fisso —
il passaggio a una release più recente sarebbe un aggiornamento
deliberato della libreria, non un semplice parametro di build.

## Avvio rapido

```bash
git clone https://github.com/optimeas/osf.git
cd osf

# Alle Tests bauen und ausführen (Unit + Konformanz + Round-Trip + Fuzz)
mvn -f implementations/java/pom.xml test

# JARs bauen (inkl. der Shaded-CLI und der Bibliothek)
mvn -f implementations/java/pom.xml package
```

`package` genera per `osf-java` il JAR della libreria, per `osf-cli`
inoltre un fat JAR eseguibile (`osf-cli.jar`) e per `osf-viewer` il JAR
del viewer. Il viewer viene tipicamente avviato direttamente:

```bash
mvn -f implementations/java/pom.xml -pl osf-viewer javafx:run
```

## Reactor e moduli

La build è un reactor Maven. Il POM padre
(`com.optimeas.osf:osf-parent:0.1.0-SNAPSHOT`, packaging `pom`) riunisce
tre moduli e centralizza versioni e configurazione dei plugin:

| Modulo (`artifactId`) | Packaging | Contenuto |
|---|---|---|
| `osf-java` | jar | La libreria vera e propria — reader OSF4/OSF5, entrambi i writer OSF5, OSFZ trasparente, modulo JPMS `com.optimeas.osf` |
| `osf-cli` | jar | Strumento a riga di comando (picocli), compilato come fat JAR eseguibile tramite Shade; classe principale `com.optimeas.osf.cli.OsfCli` |
| `osf-viewer` | jar | Viewer JavaFX per la visualizzazione multicanale; classe principale `com.optimeas.osf.viewer.ViewerApp` |

Solo `osf-java` è concepito come dipendenza di libreria; `osf-cli` e
`osf-viewer` sono applicazioni finali — vedere [Strumenti](./tools.md).

## Integrazione nel proprio progetto

La libreria viene integrata come normale dipendenza Maven:

```xml
<dependency>
  <groupId>com.optimeas.osf</groupId>
  <artifactId>osf-java</artifactId>
  <version>0.1.0-SNAPSHOT</version>
</dependency>
```

`osf-java` è un **vero modulo JPMS**. Se il proprio progetto è a sua
volta modulare (`module-info.java`), il modulo OSF deve essere
richiesto esplicitamente:

```java
module meine.app {
    requires com.optimeas.osf;
}
```

Il modulo esporta esclusivamente il package `com.optimeas.osf`; i
package interni sono deliberatamente incapsulati e non fanno parte
dell'API pubblica — dettagli in [Interni](./internals.md). Sul
classpath classico (senza `module-info.java`) la libreria funziona
senza modifiche e il modulo viene caricato come modulo automatico.

## Dipendenze

Le dipendenze di runtime sono deliberatamente ridotte — due librerie
esterne più componenti del JDK:

| Dipendenza | Origine | Scopo |
|---|---|---|
| Jackson Databind 2.18.2 | esterna (Maven) | Parsing e serializzazione del metablock OSF5 (JSON) |
| SLF4J API 2.0.16 | esterna (Maven) | Facade di logging — il backend è fornito dall'applicazione |
| StAX (`java.xml`) | JDK | Parsing in streaming del metablock OSF4 (XML) |
| `java.util.zip` | JDK | Decompressione OSFZ trasparente (gzip/zlib) sul percorso di lettura |

SLF4J è solo una facade: senza un backend integrato la libreria non
produce alcun output (no-op). `osf-cli` integra `slf4j-simple` come
backend di runtime; le applicazioni proprie scelgono il proprio
(Logback, Log4j2, …). La gestione degli errori è descritta in
[Gestione degli errori](./error-handling.md).

Le dipendenze di test (solo scope `test`, non transitive) sono
**JUnit 5** (Jupiter), **AssertJ** (asserzioni) e **jqwik 1.9.1**
(test basati su proprietà / fuzz).

## Test

```bash
mvn -f implementations/java/pom.xml test
```

L'esecuzione dei test è affidata al **maven-surefire-plugin 3.5.2** con
il runner JUnit 5 Jupiter. La suite comprende:

- **Unit test** su dati sintetici, che coprono singolarmente ogni
  livello.
- **Test di conformità** rispetto ai file di riferimento generati in
  `examples/` — la prova che tutte le implementazioni leggono e
  scrivono gli stessi file con precisione al bit (vedere
  [Lettura](./reading.md) e [Scrittura](./writing.md)).
- **Test round-trip**, che rileggono i file scritti e li confrontano
  campo per campo.
- **Test basati su proprietà / fuzz** (jqwik), che generano
  configurazioni casuali di canali e blocchi.

Risultato atteso: **tutti i test superati**. `mvn … verify` esegue
inoltre la fase di verifica e corrisponde all'esecuzione CI.

## Pubblicazione

I POM sono **pronti per la pubblicazione** — sono presenti licenza
(MIT), SCM, metadati degli sviluppatori e un profilo `release`. Il
profilo (`-Prelease`) aggiunge i JAR di sorgenti e Javadoc, firma tutti
gli artefatti tramite GPG (`maven-gpg-plugin`) e li carica su Maven
Central tramite il `central-publishing-maven-plugin`. La **build
standard non firma né distribuisce nulla.**

L'effettiva pubblicazione su Maven Central è attualmente
**rimandata** — fino ad allora la libreria viene compilata dal codice
sorgente come descritto sopra. Le ricette pratiche per l'impiego sono
nel [Cookbook](./cookbook.md); la panoramica complessiva
dell'implementazione Java si trova nella [panoramica Java](../java.md).

## Insidie note

- **Proxy aziendale / intercettazione TLS:** il wrapper `./mvnw` scarica
  Maven autonomamente al primo avvio; dietro un proxy che intercetta il
  TLS questo bootstrap può fallire con errori di certificato. Rimedio:
  utilizzare il `mvn` installato nel sistema (usa il truststore di
  sistema) oppure inserire un mirror interno in `~/.m2/settings.xml`.
- **`module not found` / split package:** se l'errore compare durante
  la compilazione di un'applicazione modulare, di solito manca la riga
  `requires com.optimeas.osf;` nel proprio `module-info.java`, oppure
  si sta tentando di importare un package interno non esportato.
- **JavaFX per il viewer:** `osf-viewer` richiede i moduli
  `javafx-controls` (21.0.5); il modo più semplice per avviarlo è il
  `javafx-maven-plugin` (`mvn -pl osf-viewer javafx:run`), che imposta
  correttamente il module path.

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
