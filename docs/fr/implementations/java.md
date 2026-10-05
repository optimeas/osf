---
title: Implémentation Java
description: La bibliothèque osf-java — implémentation Java 21 complète du Open Streaming Format avec les deux writers OSF5, encapsulation JPMS, OSFZ transparent et profil d'intégrité crc ; ainsi que les outils osf-cli et osf-viewer
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
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Implémentation Java

:::info État : disponible
L'implémentation Java est **entièrement réalisée et testée**. Elle lit
OSF4 et OSF5, écrit de l'OSF5 avec les deux modèles de writer, lit OSFZ de
manière transparente et prend en charge le profil d'intégrité crc. Les sources
de décision faisant foi sont [DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
(architecture) et [§24](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
(profil d'intégrité) ; l'état le plus récent figure dans
[STATUS.md](https://github.com/optimeas/osf/blob/main/STATUS.md).
:::

:::tip Manuel du développeur
Cette page est la vue d'ensemble. La documentation détaillée pour développeurs
se trouve dans le sous-chapitre **Java en détail** :

| Page | Contenu |
|---|---|
| [Architecture](java/architecture.md) | Modèle en couches, modules, encapsulation JPMS, modèles de données, conventions |
| [Lecture](java/reading.md) | `DataManager`, `DataChannel`, `BlockReader`, `ReaderStats`, OSFZ transparent |
| [Écriture](java/writing.md) | `StreamingWriter`, `BlockWriter`, `ChannelDef`, valeurs par défaut des métadonnées, profil d'intégrité |
| [Gestion des erreurs](java/error-handling.md) | hiérarchie `OsfException`, `verificationStatus()`, reader best effort |
| [Outils](java/tools.md) | `osf-cli` (verbes picocli) et `osf-viewer` (JavaFX) |
| [Build et intégration](java/building.md) | Maven, Java 21, JPMS, dépendances, tests |
| [Livre de recettes](java/cookbook.md) | recettes prêtes à copier, de l'inspection à la boucle embarquée |
| [Éléments internes](java/internals.md) | Encodeur, chunking, utilitaires d'intégrité, parseurs — pour les contributeurs |
:::

## Publics cibles

Deux publics principaux : les backends d'entreprise (Spring, microservices,
optiCloud) ainsi que les pipelines big data / IA (Spark, Flink, analyse de
données). S'y ajoute le Java embarqué, qui enregistre des données
d'exploitation sur une passerelle industrielle — la même situation « des deux
mondes » que celle déjà décrite au §7 pour C++.

## Étendue fonctionnelle

- **Lecture :** OSF4 (métabloc XML) et OSF5 (métabloc JSON), `DataManager`
  typé avec modèle de canaux/segments ; reader best effort robuste.
- **Écriture (OSF5) :** **les deux writers** — un `BlockWriter` (collecte en
  mémoire, écriture en une seule passe) pour les workflows par lots et un
  `StreamingWriter` (`FileChannel.force(true)` par bloc) pour
  l'enregistrement embarqué tolérant aux pannes. Les deux produisent des
  fichiers OSF5 **identiques sur disque**.
- **OSFZ transparent :** les fichiers empaquetés en gzip sont décompressés
  automatiquement à la lecture (`java.util.zip`, dans le JDK).
- **Profil d'intégrité `crc` :** activable en option sur les deux writers ; le
  reader vérifie le CRC du métabloc et celui des trames (voir ci-dessous).

## Build et utilisation

**Java 21** et **Maven**. Livré sous forme d'artefact Maven
(`groupId=com.optimeas.osf`, `artifactId=osf-java`) ; le POM est
prêt pour la publication (le déploiement dans un dépôt public est encore
différé).

```bash
# Aus dem Repository-Wurzelverzeichnis: den Java-Reaktor bauen und testen
mvn -f implementations/java/pom.xml test
```

**JPMS** (Java Platform Module System) : `module-info.java` n'exporte que
`com.optimeas.osf` ; les utilitaires internes sous `com.optimeas.osf.internal`
restent encapsulés — y compris face à la réflexion.

**Dépendances :** Jackson (JSON OSF5), StAX (XML OSF4, dans le JDK), `java.util.zip`
(OSFZ + CRC32C, dans le JDK), SLF4J (façade de journalisation). E/S binaires
via `ByteBuffer` sur `FileChannel` avec `LITTLE_ENDIAN`.

## Modules

Outre la bibliothèque principale, le réacteur Java comprend deux outils :

| Module | Objet |
|---|---|
| **`osf-java`** | Bibliothèque principale — lecture, les deux writers, OSFZ, profil d'intégrité. |
| **`osf-cli`** | Outil en ligne de commande (picocli) : `info`, `channels`, `dump`, `convert` ; construit comme jar exécutable. |
| **`osf-viewer`** | Visionneuse JavaFX pour plusieurs canaux (min/max par pixel). Démarrage : `mvn -pl osf-viewer javafx:run`. |

## Profil d'intégrité (`crc`)

Profil d'intégrité OSF5 facultatif au niveau `crc` (CRC32C, `java.util.zip.CRC32C`,
natif au JDK ; valeur de contrôle `0xE3069283`, identique à l'octet près à Rust/C++/Delphi).

- **Reader :** `MagicHeaderParser` reconnaît le jeton `crc32c` ; `DataManager`
  vérifie le CRC du métabloc avant l'analyse, `BlockReader` vérifie et retire
  le CRC de trame de 4 octets avant l'exploitation typée (fail-closed). Les
  blocs de signature (canal `0xFFFE`) sont ignorés et comptabilisés, de sorte
  que les fichiers signés restent lisibles. `ReaderStats` fournit `integrity` +
  `verificationStatus()` (`none`/`crc_valid`/`invalid`/`signature_unverifiable`).
- **Writer :** `setIntegrity(IntegrityProfile.CRC32C)` sur **les deux** writers
  (désactivé par défaut) écrit le jeton, le CRC du métabloc et, pour chaque
  bloc, un CRC de trame.

Le niveau de signature (`signed`, Ed25519) n'est pas encore implémenté.
Bases : [DECISIONS §24](https://github.com/optimeas/osf/blob/main/DECISIONS.md).

## Conformité à la spécification

L'implémentation Java suit les mêmes règles sémantiques que Rust, Python,
C++ et Delphi : tous les types de données actuels (types non signés via la
promotion de types Java ou `BigInteger` pour la plage complète), rejet
explicite des types supprimés, `bytearray` comme alias de lecture de `binary`,
`channeltype` comme **forme de données** (scalar/vector/matrix/binary), la
règle de terminaison par octet nul déterministe selon la version pour
`string`/`binary` et les quatre identifiants d'en-tête magique. La conformité
est vérifiée par rapport au contrat partagé du manifeste de référence
(`examples/reference_manifest.json`).

## Code source et informations complémentaires

- Décision d'architecture : [DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
- Profil d'intégrité : [DECISIONS §24](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
- État actuel : [STATUS.md](https://github.com/optimeas/osf/blob/main/STATUS.md) · [github.com/optimeas/osf](https://github.com/optimeas/osf)
- Spécification du format : chapitre [Format OSF](../osf_general.md)

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
