---
title: Build et intégration
description: Build Maven de la bibliothèque OSF Java — prérequis, réacteur et modules, intégration dans son propre projet (coordonnées Maven + JPMS requires), dépendances, tests et publication
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

# Build et intégration

Le guide de build de référence, tenu à jour en continu, est le fichier
`README.md` du répertoire Java (`implementations/java/`). Cette page en résume
l'essentiel et le complète avec des scénarios d'intégration dans ses propres
projets. La structure de la bibliothèque est décrite dans
[Architecture](./architecture.md).

## Prérequis

- **JDK 21 (LTS)** — la bibliothèque est compilée avec Java 21 comme base
  (`maven.compiler.release = 21`).
- **Maven 3.9+** — le réacteur utilise des plug-ins standard ; un wrapper
  Maven `./mvnw` est fourni et télécharge lui-même Maven lors de la première
  exécution.
- **Internet lors du premier build** — Maven télécharge une seule fois les
  dépendances (Jackson, SLF4J, bibliothèques de test) dans le cache du dépôt
  local (`~/.m2`) ; ensuite, le projet se construit hors ligne.

**Java 21 n'est pas une option,** mais la base définie de manière fixe —
passer à une version plus récente serait une mise à niveau délibérée de la
bibliothèque, et non un simple commutateur de build.

## Démarrage rapide

```bash
git clone https://github.com/optimeas/osf.git
cd osf

# Alle Tests bauen und ausführen (Unit + Konformanz + Round-Trip + Fuzz)
mvn -f implementations/java/pom.xml test

# JARs bauen (inkl. der Shaded-CLI und der Bibliothek)
mvn -f implementations/java/pom.xml package
```

`package` produit pour `osf-java` le JAR de la bibliothèque, pour `osf-cli`
en plus un fat JAR exécutable (`osf-cli.jar`) et pour `osf-viewer` le JAR de
la visionneuse. La visionneuse est généralement lancée directement :

```bash
mvn -f implementations/java/pom.xml -pl osf-viewer javafx:run
```

## Réacteur et modules

Le build est un réacteur Maven. Le POM parent
(`com.optimeas.osf:osf-parent:0.1.0-SNAPSHOT`, packaging `pom`) regroupe
trois modules et centralise les versions ainsi que la configuration des
plug-ins :

| Module (`artifactId`) | Packaging | Contenu |
|---|---|---|
| `osf-java` | jar | La bibliothèque proprement dite — lecteurs OSF4/OSF5, les deux writers OSF5, OSFZ transparent, module JPMS `com.optimeas.osf` |
| `osf-cli` | jar | Outil en ligne de commande (picocli), construit comme fat JAR exécutable via Shade ; classe principale `com.optimeas.osf.cli.OsfCli` |
| `osf-viewer` | jar | Visionneuse JavaFX pour l'affichage multicanal ; classe principale `com.optimeas.osf.viewer.ViewerApp` |

Seul `osf-java` est destiné à servir de dépendance de bibliothèque ;
`osf-cli` et `osf-viewer` sont des applications finales — voir
[Outils](./tools.md).

## Intégration dans son propre projet

La bibliothèque s'intègre comme une dépendance Maven ordinaire :

```xml
<dependency>
  <groupId>com.optimeas.osf</groupId>
  <artifactId>osf-java</artifactId>
  <version>0.1.0-SNAPSHOT</version>
</dependency>
```

`osf-java` est un **véritable module JPMS**. Si votre propre projet est
lui-même modulaire (`module-info.java`), le module OSF doit être demandé
explicitement :

```java
module meine.app {
    requires com.optimeas.osf;
}
```

Le module exporte exclusivement le paquet `com.optimeas.osf` ; les paquets
internes sont volontairement encapsulés et ne font pas partie de l'API
publique — détails dans [Éléments internes](./internals.md). Sur le classpath
classique (sans `module-info.java`), la bibliothèque fonctionne sans
changement ; le module est alors chargé comme module automatique.

## Dépendances

Les dépendances d'exécution sont volontairement réduites — deux bibliothèques
externes plus des composants du JDK :

| Dépendance | Origine | Finalité |
|---|---|---|
| Jackson Databind 2.18.2 | externe (Maven) | analyser et sérialiser le métabloc OSF5 (JSON) |
| SLF4J API 2.0.16 | externe (Maven) | façade de journalisation — le backend est fourni par l'application |
| StAX (`java.xml`) | JDK | analyse en flux du métabloc OSF4 (XML) |
| `java.util.zip` | JDK | décompression OSFZ transparente (gzip/zlib) sur le chemin de lecture |

SLF4J n'est qu'une façade : sans backend intégré, la bibliothèque n'émet
rien (no-op). `osf-cli` intègre `slf4j-simple` comme backend d'exécution ;
les applications personnalisées choisissent le leur (Logback, Log4j2, …). La
gestion des erreurs est décrite dans
[Gestion des erreurs](./error-handling.md).

Les dépendances de test (portée `test` uniquement, non transitives) sont
**JUnit 5** (Jupiter), **AssertJ** (assertions) et **jqwik 1.9.1**
(tests basés sur les propriétés / fuzz).

## Tests

```bash
mvn -f implementations/java/pom.xml test
```

L'exécution des tests est assurée par le **maven-surefire-plugin 3.5.2** avec
le runner JUnit 5 Jupiter. La suite comprend :

- des **tests unitaires** sur des données synthétiques, qui couvrent chaque
  couche séparément ;
- des **tests de conformité** par rapport aux fichiers de référence générés
  sous `examples/` — la preuve que toutes les implémentations lisent et
  écrivent les mêmes fichiers à l'octet près (voir [Lecture](./reading.md) et
  [Écriture](./writing.md)) ;
- des **tests aller-retour** qui relisent les fichiers écrits et les comparent
  champ par champ ;
- des **tests de propriétés / fuzz** (jqwik) qui génèrent des constellations
  aléatoires de canaux et de blocs.

Résultat attendu : **tous les tests au vert**. `mvn … verify` exécute en plus
la phase de vérification et correspond à l'exécution CI.

## Publication

Les POM sont **prêts pour la publication** — licence (MIT), SCM, métadonnées
de développeur et un profil `release` sont renseignés. Le profil
(`-Prelease`) joint les JAR de sources et de Javadoc, signe tous les
artefacts avec GPG (`maven-gpg-plugin`) et les téléverse vers Maven Central
via le `central-publishing-maven-plugin`. Le **build standard ne signe ni ne
déploie rien.**

La publication effective vers Maven Central est actuellement **différée** —
d'ici là, la bibliothèque est construite à partir du code source comme décrit
ci-dessus. Des recettes pratiques d'utilisation figurent dans le
[Livre de recettes](./cookbook.md) ; la vue d'ensemble de l'implémentation
Java se trouve dans la [Présentation Java](../java.md).

## Écueils connus

- **Proxy d'entreprise / interception TLS :** le wrapper `./mvnw` télécharge
  Maven lui-même au premier lancement ; derrière un proxy qui intercepte TLS,
  cette phase d'amorçage peut échouer avec des erreurs de certificat.
  Remède : utiliser le `mvn` installé sur le système (il utilise le truststore
  du système) ou déclarer un miroir interne dans `~/.m2/settings.xml`.
- **`module not found` / split package :** si l'erreur survient lors de la
  construction d'une application modulaire, il manque le plus souvent la
  ligne `requires com.optimeas.osf;` dans son propre `module-info.java`, ou
  l'on tente d'importer un paquet interne non exporté.
- **JavaFX pour la visionneuse :** `osf-viewer` nécessite les modules
  `javafx-controls` (21.0.5) ; le plus simple est de le lancer via le
  `javafx-maven-plugin` (`mvn -pl osf-viewer javafx:run`), qui définit
  correctement le module path.

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
