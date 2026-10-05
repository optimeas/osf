---
title: Écriture
description: Écrire de l'OSF5 avec osf-java — StreamingWriter (embarqué, tolérant aux pannes), BlockWriter (convivial pour les analystes), le profil d'intégrité crc et l'aller-retour
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - StreamingWriter
  - BlockWriter
  - crc32c
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Écriture

La bibliothèque écrit **exclusivement de l'OSF5** — même lorsque la source
était un fichier OSF4 ou OSFZ. Deux classes writer du paquet
`com.optimeas.osf` couvrent deux profils d'utilisation très différents :

| | `StreamingWriter` | `BlockWriter` |
|---|---|---|
| Usage | Enregistrement embarqué | Analyse, conversion, export |
| Mémoire | limitée (un tampon de bloc par canal, libéré après l'émission) | collecte tous les échantillons en RAM |
| Durabilité | `FileChannel.force(true)` par bloc — tolérant aux coupures de courant | pas de `force` ; le fichier est produit à la fin |
| Cible | chemin de fichier (`Path`) | `Path` **ou** `OutputStream` quelconque (mémoire, socket) |
| `sizeOfLengthValue` | fixe dès la déclaration du canal (le métabloc est déjà sur disque) | relèvement automatique de 2 à 4 pour les canaux variables |
| Cycle de vie | `create()` → canaux → `begin()` → écriture → `close()` | `new` → collecte → `writeToFile()` / `writeTo()` |
| Émission multiple | non (un fichier par instance, `Closeable`) | oui (`writeTo*` appelable à volonté) |

Les deux partagent les mêmes types de canaux, la même arithmétique de
chunking et les mêmes familles d'écriture (équidistant, numérique timestamped,
GPS, String/Binary). Pour les mêmes canaux, échantillons,
`sizeOfLengthValue` et `created_utc`, ils produisent des fichiers OSF5
**identiques à l'octet près**.

## Déclarer des canaux — `ChannelDef`

Un canal n'est pas construit directement, mais déclaré via une méthode
`add…Channel` du writer, qui crée en interne une `ChannelDef` et renvoie
l'**index de canal** (séquentiel à partir de 0) utilisé par tous les appels
d'écriture. La `ChannelDef` (un `record`) décrit le canal tel qu'il figure
dans le métabloc :

```java
public record ChannelDef(
    int index,               // Kanalindex (0..65535), vom Block-Strom referenziert
    String name,             // vollqualifizierter Kanalname (Pflicht)
    DataType dataType,       // aufgelöster Datentyp (Pflicht)
    ChannelType channelType, // Datenform: SCALAR/VECTOR/MATRIX/BINARY
    int sizeOfLengthValue,   // Breite des Längenpräfix: 2 oder 4
    long timeIncrementNs,    // äquidistante Periode in ns; 0 = timestamped
    String physicalUnit,     // physikalische Einheit oder null
    Map<String,String> attributes) { }  // z. B. displayname, comment, reference
```

Deux modes de déclaration par writer :

```java
// Timestamped-Kanal (jedes Sample trägt seinen eigenen Zeitstempel):
int rpm = writer.addTimestampedChannel("motor.drehzahl", DataType.DOUBLE, 2);
int evt = writer.addTimestampedChannel("ereignis", DataType.STRING, 2,
                                       "1/min", Map.of("displayname", "Ereignis"));

// Äquidistanter Kanal (feste Rate; nur der Segmentstart trägt einen Zeitstempel):
int sig = writer.addEquidistantChannel("beschleunigung", DataType.DOUBLE, 4,
                                       1000.0 /* Hz */);
```

Sont rejetés avec `IllegalArgumentException` : un nom vide, un type de
données `null` ou `UNSUPPORTED`, un `sizeOfLengthValue` ≠ 2/4, un canal
équidistant avec un type autre que `FLOAT`/`DOUBLE`, ainsi qu'une fréquence
d'échantillonnage non positive ou non finie. Pour le `StreamingWriter`, tout
appel `add…Channel` après `begin()` déclenche une `OsfException` (la phase de
configuration est alors terminée). Le `channeltype` est toujours normalisé en
`scalar` à l'écriture — l'équidistance n'est portée que par le
`timeincrement`, et non par le `channeltype`.

### Bien choisir `sizeOfLengthValue`

Le champ de longueur de chaque bloc fait 2 ou 4 octets de large et limite la
taille des blocs (~64 Ko ou ~2 Go). Règles pratiques :

- **Canaux String/Binary avec de grands échantillons** (images, audio,
  blobs) : pour le `StreamingWriter`, déclarer impérativement `4` — il ne peut
  plus modifier la valeur après `begin()`. Le `BlockWriter` relève
  automatiquement de 2 à 4 pour un canal variable qu'il peut dimensionner
  lui-même (relèvement automatique à l'émission) ; pour lui, `2` comme valeur
  de départ est toujours correct.
- **Les canaux numériques** ne sont jamais modifiés — ils sont au contraire
  répartis sur davantage de blocs. Sur le `StreamingWriter`, `4` épargne aux
  canaux à haute fréquence le découpage en de nombreux petits blocs soumis
  chacun à un fsync.
- Sinon, rester sur la valeur par défaut `2` (blocs plus compacts).

## `StreamingWriter` — embarqué, tolérant aux pannes

```java
import com.optimeas.osf.*;

try (StreamingWriter w = StreamingWriter.create(Path.of("aufzeichnung.osf"))) {
    w.setMetadata("creator", "logger-fw/3.2");   // Metadaten vor begin()
    w.setMetadata("tag", "pruefstand-7");

    int rpm = w.addTimestampedChannel("motor.drehzahl", DataType.DOUBLE, 2);
    int gps = w.addTimestampedChannel("fahrzeug.gps", DataType.GPS_LOCATION, 2);
    w.begin();                                    // Header + Metablock auf Platte, fsync

    while (running) {
        w.writeSample(rpm, nowNs(), readRpm());   // je Block: kodieren, schreiben, fsync
    }
}                                                 // close() emittiert Restblöcke + fsync
```

Garanties et comportement :

- **Chaque `writeSample` revenu est sur disque sous forme de bloc complet**
  (`FileChannel.force(true)` = fsync). Après une coupure de courant, le
  fichier reste lisible jusqu'au dernier bloc confirmé ; le reader best effort
  restitue chaque bloc avant la coupure et signale un reste tronqué via
  `ReaderStats.truncationSeen()` (voir [Lecture](./reading.md)).
- **Préambule différé ou immédiat :** `begin()` écrit une fois la ligne
  d'en-tête magique (`OSF5 <len>\n`) et le métabloc JSON, et les soumet à
  fsync ; s'il n'est pas appelé explicitement, cela se fait automatiquement au
  premier échantillon. Ensuite, le métabloc n'est plus jamais touché — **les
  setters de métadonnées n'ont d'effet qu'avant**.
- **`created_utc`** est horodaté automatiquement lors de `begin()` s'il n'est
  pas défini (ISO-8601 UTC, par ex. `2026-07-11T08:30:00Z`).
- **Les canaux sont attachés à une famille de blocs :** écrire le même canal
  une fois en timestamped et une fois en équidistant provoque une
  `OsfException`.
- `close()` est idempotent et émet les éventuels blocs restants en tampon ;
  en tant que `Closeable`, le writer appartient à un try-with-resources. Il
  n'est **pas thread-safe** — sérialiser les accès de l'extérieur.

### Familles d'écriture

```java
// Timestamped numerisch — Einzel- und Batch-Überladungen:
w.writeSample(ch, tsNs, 3.14);                        // double
w.writeSample(ch, tsNs, 42L);                         // beliebiger Integer-Kanal
w.writeSamples(ch, tsArray, werteArray);              // parallele Arrays (Bulk)

// GPS (eigene Überladung):
w.writeSample(ch, tsNs, new GpsLocation(lat, lon, alt));

// String / Binary (ein Sample pro Block; OSF5: kein 0x00-Terminator):
w.writeSample(ch, tsNs, "Ereignis: Tür offen");
w.writeSample(ch, tsNs, jpegBytes);                   // byte[]

// Äquidistant (nur float/double; Rate stammt aus addEquidistantChannel):
w.startEquidistantSegment(ch, t0Ns, daten);           // öffnet ein Segment
w.appendEquidistantSamples(ch, weitere);              // verlängert das offene Segment
```

`writeSample(int, long, long)` dessert tout canal entier (`int8…int64`,
`uint8…uint64`) ; la valeur est rétrécie à la largeur du canal lors de
l'encodage. Chaque appel de lot et de segment est automatiquement découpé en
chunks selon la capacité de bloc du canal — un fsync par bloc émis. Un nouvel
appel `startEquidistantSegment` clôt le précédent et ouvre volontairement un
**nouveau** segment ; les lacunes entre segments sont le moyen conforme à la
spécification de représenter les pauses d'enregistrement.

## `BlockWriter` — collecter et émettre

```java
BlockWriter w = new BlockWriter();
w.setMetadata("creator", "analyse-tool/1.0");

int ch  = w.addTimestampedChannel("messwert", DataType.DOUBLE);   // Auto-Bump-Form
int sig = w.addEquidistantChannel("signal", DataType.DOUBLE, 4, 100.0);
w.startEquidistantSegment(sig, t0Ns, samples);
w.writeSample(ch, tsNs, 42.0);

w.writeToFile(Path.of("ergebnis.osf"));

var mem = new java.io.ByteArrayOutputStream();   // oder ein beliebiger OutputStream
w.writeTo(mem);                                  // dieselbe Instanz erneut emittieren
```

- La famille `writeSample` / `startEquidistantSegment` reflète celle du
  writer en streaming (mêmes types, même validation), mais ne collecte qu'en
  mémoire ; le découpage en blocs conformes à la spécification n'intervient
  qu'à l'émission.
- `writeToFile` / `writeTo` peuvent être appelés **plusieurs fois** — la même
  instance peut être écrite simultanément dans un fichier et sur le réseau.
- **Relèvement automatique :** un canal variable déclaré avec la forme à deux
  arguments `addTimestampedChannel(name, type)` démarre à
  `sizeOfLengthValue = 2` et est relevé à `4` pour l'émission si son plus
  grand échantillon dépasse le champ de longueur u16. La forme à trois
  arguments avec valeur explicite est respectée et rejette au contraire un
  échantillon trop grand (comme le StreamingWriter, qui ne peut pas relever).
  Les canaux numériques ne sont jamais relevés.
- Pas de fsync — la durabilité incombe à l'appelant.
- `channelCount()` et `channelIndex("name")` aident lorsque les index ne sont
  pas conservés (`channelIndex` renvoie `-1` pour un nom inconnu).

## Valeurs par défaut automatiques des métadonnées

Les deux writers écrivent les entrées définies par `setMetadata(key, value)`
**telles quelles** dans l'objet `osf.file` du métabloc. La seule intervention
automatique :

| Champ | Comportement si non défini |
|---|---|
| `created_utc` | **toujours** horodaté (heure UTC actuelle, `YYYY-MM-DDTHH:MM:SSZ`) — via `putIfAbsent`, une valeur déjà présente reste intacte |
| tous les autres (`creator`, `tag`, …) | écrits uniquement s'ils sont définis ; jamais en tant que `null` |

Il n'y a donc pas de valeur par défaut imposée pour `creator` ou `tag` — ce
qui n'est pas défini n'apparaît pas dans le métabloc.

## Profil d'intégrité à l'écriture (`crc`)

Les deux writers peuvent produire en option le profil d'intégrité OSF5 au
niveau `crc` — il est désactivé par défaut (`IntegrityProfile.NONE`) :

```java
w.setIntegrity(IntegrityProfile.CRC32C);   // vor begin() bzw. writeTo
```

Lorsqu'il est activé, le writer écrit

- un jeton `crc32c` dans la ligne d'en-tête magique, qui porte le **CRC du
  métabloc**, et
- pour chaque bloc de données, un **CRC32C de trame** ajouté (4 octets,
  compté dans le champ de longueur du bloc). Pour le `StreamingWriter`, le CRC
  de trame est rendu durable avec le même `force(true)` que le bloc lui-même.

La somme de contrôle est `java.util.zip.CRC32C` (native au JDK). Les 4
octets de CRC de trame prennent sur le budget de charge utile de chaque bloc,
de sorte qu'un peu moins d'échantillons tiennent par bloc — l'arithmétique de
chunking en tient compte automatiquement. La manière dont le reader vérifie
ces valeurs en mode fail-closed est décrite sous [Lecture](./reading.md) et
[Gestion des erreurs](./error-handling.md).

Le niveau de signature (`IntegrityProfile.ED25519`) n'est **pas** pris en
charge par le writer et est rejeté avec une `OsfException` lors de l'écriture
du préambule.

## Aller-retour et conversion

Réécrire un `DataManager` chargé — c'est aussi la conversion
OSF4 → OSF5 ou OSFZ → OSF5 :

```java
DataManager mgr = DataManager.loadFromFile(Path.of("alt.osf"));  // auch OSF4 / OSFZ
BlockWriter.fromManager(mgr).writeToFile(Path.of("neu.osf"));     // immer OSF5
```

`BlockWriter.fromManager(mgr)` construit un writer à partir des canaux typés
et des échantillons du manager ; pour filtrer ou renommer avant l'écriture, on
continue de travailler sur le writer renvoyé.

Sont conservés : noms de canaux, types de données, valeurs d'échantillons (à
l'octet près), limites de segments et métadonnées du fichier — **y compris
`created_utc`**, car une valeur chargée est déjà définie et n'est pas
horodatée à nouveau. N'est **pas** repris : le `sizeOfLengthValue` (le writer
commence à 2 et relève les canaux variables si nécessaire) ; l'index de canal
est réattribué selon la position dans la liste.

## Ce que les writers ne font volontairement pas

- **Pas de sortie OSF4** — OSF5 est le seul format d'écriture.
- **Pas de sortie OSFZ** — la bibliothèque lit de manière transparente les
  fichiers empaquetés en gzip, mais ne compresse pas elle-même ; la
  compression est une étape en aval.
- **Pas d'horodatages relatifs** — le format temporel relatif est un héritage
  de lecture d'OSF4 ; les writers émettent des horodatages absolus.
- **Pas de validation des horodatages** — la monotonie n'est pas exigée par la
  spécification et n'est pas imposée.
- **Pas de signature** — le niveau Ed25519 est rejeté.
- **Pas de sécurité des threads** — sérialiser de l'extérieur les accès à une
  instance de writer.

Les détails de trame et de chunking sur lesquels reposent les deux writers
sont décrits au chapitre [Éléments internes](./internals.md) ; l'architecture
globale et les outils se trouvent sous [Architecture](./architecture.md),
[Outils](./tools.md) et [Build](./building.md). Le
[Livre de recettes](./cookbook.md) rassemble des exemples pour débuter ; la
définition de format de référence est fournie par le chapitre
[Format OSF](../../osf_general.md), la vue d'ensemble par
l'[implémentation Java](../java.md).

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
