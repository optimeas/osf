---
title: Lecture
description: Lire des fichiers OSF et OSFZ en Java — DataManager, DataChannel, le flux de blocs, ReaderStats et la décompression transparente
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - DataManager
  - DataChannel
  - OSFZ
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Lecture

L'implémentation Java lit **OSF4**, **OSF5** et, de manière transparente,
**OSFZ** (gzip/zlib) via la même API. Elle lit en outre le profil d'intégrité
`crc` et vérifie alors chaque somme de contrôle
([Gestion des erreurs](./error-handling.md)). Le point d'entrée public est
exactement une classe :

- **`com.optimeas.osf.DataManager`** — la voie de lecture standard et seule
  voie publique. Il charge le fichier en entier, assemble des canaux typés à
  partir du flux de blocs et résout toutes les limites de bloc. Pour
  l'analyse, l'export et l'outillage.

Le décodeur de flux de blocs proprement dit travaille en dessous, dans le
paquet non exporté `com.optimeas.osf.internal` — c'est un détail
d'implémentation du `DataManager` et non une surface publique (voir la
section [Le niveau du flux de blocs](#die-blockstrom-ebene)).

## Démarrage rapide

```java
import com.optimeas.osf.DataManager;
import com.optimeas.osf.DataChannel;
import java.nio.file.Path;

DataManager mgr = DataManager.loadFromFile(Path.of("messung.osf")); // auch .osfz

// Alle Kanäle auflisten (Metablock-Reihenfolge)
for (DataChannel ch : mgr.channels()) {
    System.out.printf("%-30s %-12s %d Samples%n",
            ch.name(), ch.dataType(), ch.sampleCount());
}

// Einen Kanal über den Namen ansprechen (primäre Zugriffsform)
mgr.channelByName("Sensor.Temperatur").ifPresent(ch -> {
    double[] werte     = ch.asDoubles();     // Werte, auf double geweitet
    long[]   zeitstamp = ch.timestampsNs();  // parallele Zeitstempel (ns)
    // …
});
```

## `DataManager`

### Chargement

| Méthode | Source | Remarques |
|---|---|---|
| `DataManager.loadFromFile(Path)` | fichier | OSF, OSFZ ; ouvre et ferme lui-même le flux |
| `DataManager.load(InputStream)` | `InputStream` quelconque | est consommé entièrement ; doit se trouver au début du fichier |

Les deux voies passent par le même pipeline : détection OSFZ →
en-tête magique → (avec `crc`, la somme de contrôle du métabloc) → parseur de
métabloc (JSON pour OSF5, XML/StAX pour OSF4) → décodeur de flux de blocs
jusqu'à EOF → assemblage des canaux. Le résultat est immuable et peut être lu
simultanément par un nombre quelconque de threads.

### Accès

```java
mgr.version();                 // OsfVersion — OSF4 oder OSF5, aus dem Magic-Header
mgr.metadata();                // Map<String,String> — Datei-Metadaten aus dem "file"-Block
mgr.stats();                   // ReaderStats — Telemetrie des Ladevorgangs
mgr.channels();                // List<DataChannel> — Metablock-Reihenfolge
mgr.channelByName("a.b.c");    // Optional<DataChannel> — leer, wenn unbekannt
mgr.channelByIndex(7);         // Optional<DataChannel> — Index aus dem Metablock
```

`channelByName` est la forme d'accès principale ; `channelByIndex` est une
commodité. Les deux renvoient un `Optional` vide plutôt qu'une erreur, car
« canal inexistant » est un cas normal lorsqu'on explore des fichiers
inconnus. En cas de nom attribué deux fois, c'est la **première** définition
qui l'emporte.

Les clés de `metadata()` correspondent exactement aux noms de champs wire du
bloc `file` (par ex. `creator`, `created_utc`, `tag`, `reason`).

### Ce qui peut se produire au chargement

- **Fichier tronqué :** pas d'exception. Tous les blocs entièrement lisibles
  aboutissent dans les canaux, `mgr.stats().truncationSeen() == true`.
- **Type de données inconnu (futur) :** le canal est **omis** de
  `channels()` (ses blocs ont été ignorés au niveau du reader). Un *type de
  canal* inconnu (la forme des données), en revanche, laisse le canal
  conservé — la lisibilité ne dépend que du type de données et des types de
  blocs, jamais du `channeltype`.
- **Erreur de somme de contrôle du métabloc** (uniquement avec `crc`) :
  le chargement s'interrompt en mode **fail-closed** avec
  `OsfException.MetablockCrcMismatch` — un métabloc manipulé n'est jamais
  analysé.
- **Erreurs de structure :** un en-tête ou un métabloc défectueux, une
  longueur de métabloc supérieure à `Integer.MAX_VALUE` ou une erreur d'E/S
  interrompent le chargement avec `OsfException.MalformedFile` — voir
  [Gestion des erreurs](./error-handling.md).

## `DataChannel` — les canaux typés

`DataChannel` est **une seule** classe ; sa disposition en mémoire est
distinguée par `kind()` via l'énumération `DataChannel.Kind` :

```java
enum Kind { EQUIDISTANT, TIMESTAMPED, VARIABLE }
```

Les limites de bloc sur disque sont résolues ; les échantillons apparaissent
sous forme d'une suite plate avec des horodatages absolus parallèles.

### Métadonnées communes

```java
ch.index();          // int    — On-Disk-Kanalindex aus dem Metablock
ch.name();           // String — vollqualifizierter Name
ch.dataType();       // DataType — aufgelöster Datentyp der Samples
ch.channelType();    // ChannelType — Datenform (scalar/vector/matrix/binary)
ch.physicalUnit();   // String — physikalische Einheit, oder null
ch.kind();           // DataChannel.Kind — Speicherlayout
ch.sampleCount();    // long   — Anzahl Samples (Summe über alle Segmente)
ch.timestampsNs();   // long[] — absolute Zeitstempel, parallel zu den Werten
ch.segments();       // List<DataChannel.Segment> — nur bei EQUIDISTANT belegt
```

`timestampsNs()` renvoie le tableau sous-jacent propre au canal — ne pas le
modifier.

### `EQUIDISTANT` — des segments au lieu d'un horodatage par échantillon

Les canaux équidistants ne stockent **aucun horodatage par échantillon**. Ils
portent à la place une suite plate de valeurs plus une liste de segments.
Chaque bloc `bcStartData` du fichier ouvre un segment, chaque bloc
`bcContinuedData` suivant prolonge le segment le plus récent :

```java
public record Segment(long startTimestampNs, double sampleRateHz,
                      int startIndex, int sampleCount) {}
```

`timestampsNs()` **reconstruit** les horodatages : l'échantillon `i` d'un
segment se situe à `startTimestampNs + (long)(i * 1e9 / sampleRateHz)`
(tronqué vers zéro, addition saturante). Les lacunes **entre** segments ne
sont pas interpolées — chaque segment commence à son propre
`startTimestampNs`, une pause d'enregistrement reste une pause.

```java
DataChannel ch = mgr.channelByName("Beschleunigung.X").orElseThrow();
for (DataChannel.Segment seg : ch.segments()) {
    // seg.startTimestampNs(), seg.sampleRateHz(), seg.startIndex(), seg.sampleCount()
}
double[] werte = ch.asDoubles();      // flacher Lauf über alle Segmente
long[]   zeit  = ch.timestampsNs();   // dazu passende, rekonstruierte Zeitstempel
```

### `TIMESTAMPED` — suites parallèles

Canaux numériques ou GPS avec horodatages explicites :
`timestampsNs()` et la suite de valeurs sont parallèles. Les blocs
`bcAbsTimeStampData` aboutissent directement ici ; les deltas OSF4
`bcContinuedRelStampData` sont additionnés au chargement pour donner des
horodatages absolus (ancre = dernier horodatage absolu observé du canal).

### `VARIABLE` — String et Binary

Canaux String et Binary : toujours via `bcAbsTimeStampData` avec un
horodatage par échantillon.

```java
String[] texte = ch.asStrings();     // string-Kanal
byte[][] blobs = ch.asBinaries();    // binary-Kanal
```

La gestion du terminateur nul est déterministe selon la version : pour
**OSF4**, le reader a déjà retiré le dernier octet ; pour **OSF5**, la charge
utile arrive inchangée.

### Accesseurs typés

Chaque accesseur projette les valeurs stockées dans un nouveau tableau et
lève `OsfException.UnsupportedType` si `dataType()` ne correspond pas :

| Accesseur | Retour | valide pour |
|---|---|---|
| `asDoubles()` | `double[]` | tout type numérique (bool→0/1, toutes les largeurs d'entiers, float, double) |
| `asLongs()` | `long[]` | types entiers `int8`…`int64`, `uint8`…`uint64`, `bool` (non signé, étendu par des zéros) |
| `asBooleans()` | `boolean[]` | uniquement `bool` |
| `asStrings()` | `String[]` | uniquement `string` |
| `asBinaries()` | `byte[][]` | uniquement `binary` |
| `asGps()` | `GpsLocation[]` | uniquement `gpslocation` |

Pour `uint64`, `asLongs()` fournit les bits bruts — pour la sortie texte,
utiliser `Long.toUnsignedString(...)`. `GpsLocation` est un record composé de
`latitude`, `longitude` (degrés) et `altitude` (mètres).

### Types de données {#datentypen}

`DataType` couvre `bool`, `int8`…`int64`, `uint8`…`uint64`, `float`,
`double`, `string`, `binary` et `gpslocation` ; `bytearray` est accepté à la
lecture comme alias de `binary`. Un type de données inconnu mais non supprimé
devient `DataType.UNSUPPORTED` (le fichier se charge, le canal est omis) ; les
types `pair`, `triple`, `candata` et `gpsdata`, **supprimés** du standard OSF,
déclenchent volontairement `OsfException.UnsupportedType` lors de la
résolution.

## Le niveau du flux de blocs {#die-blockstrom-ebene}

Sous le `DataManager`, un lecteur de flux de blocs interne
(`com.optimeas.osf.internal.BlockReader`) décode les blocs binaires qui
suivent le métabloc. Ce paquet n'est **pas** exporté du module JPMS — il n'y a
donc pas d'API de streaming publique dans cette version ; côté application,
le `DataManager` est le seul point d'entrée. Il faut néanmoins connaître son
comportement, car il explique la télémétrie dans `ReaderStats` :

- **Troncature best effort :** un dernier bloc court ou corrompu met fin à la
  lecture en silence — tout ce qui a été décodé auparavant est conservé,
  `stats().truncationSeen()` est positionné. Aucune exception n'est jamais
  levée pour une troncature.
- **Les blocs ignorés restent visibles — uniquement via `ReaderStats` :**
  les octets de contrôle dépréciés (`blocksSkippedDeprecatedType()`) ou
  réservés (`blocksSkippedReservedType()`) ainsi que les blocs
  `bcStatusEvent` (`blocksSkippedStatusEvent()`) sont écartés sans analyse
  d'après leur longueur et comptabilisés. `ReaderStats` est le *seul*
  endroit où un tel événement est observable : `com.optimeas.osf.internal`
  n'est pas exporté, les valeurs `Block.Skipped` sous-jacentes
  n'atteignent donc jamais le code applicatif. Les blocs des canaux de type
  `UNSUPPORTED` sont écartés de la même manière, mais **pas encore**
  comptabilisés par un champ de `ReaderStats`.
- **Trailer OSF4 :** le bloc d'information facultatif `0xFFFF` avec son
  trailer de 40 octets est consommé en silence.
- **Intégrité :** lorsque le profil `crc` est actif, chaque bloc porte un
  CRC32C final sur l'ensemble de la trame ; il est vérifié avant l'analyse
  typée (fail-closed). Un échec ignore le bloc et incrémente
  `blocksCrcFailed()`. Les blocs de signature sur le canal réservé `0xFFFE`
  sont ignorés et comptabilisés, afin qu'un fichier signé reste lisible.

Le lecteur de flux de blocs ne décompresse **pas** lui-même — les entrées
OSFZ sont décompressées au préalable (voir ci-dessous).

## OSFZ transparent

`loadFromFile("x.osfz")` fonctionne sans autre intervention : avant l'en-tête
magique, la chaîne de lecture examine les deux premiers octets et, si
nécessaire, place un décompresseur en amont. Sont reconnus gzip (`1F 8B`) et
zlib (`78` suivi de `01`/`5E`/`9C`/`DA`) ; un vrai OSF commence par
`O` = `0x4F` et n'entre donc jamais en collision. La décompression utilise les
outils du JDK `java.util.zip` (`GZIPInputStream` ou
`InflaterInputStream`) et se fait en flux. La détection est consignée dans
`stats().compressed()` et `stats().compressionFormat()` (`"gzip"` ou
`"zlib"`) ; pour les fichiers non compressés, le libellé reste
`"none"`.

## `ReaderStats` — télémétrie

Après chaque chargement, via `mgr.stats()` :

| Champ | Signification |
|---|---|
| `blocksRead()` | Nombre de blocs entièrement décodés |
| `truncationSeen()` | Le flux s'est terminé sur un bloc partiel/corrompu |
| `compressed()` / `compressionFormat()` | Détection OSFZ (`"none"` / `"gzip"` / `"zlib"`) |
| `integrity()` | Profil d'intégrité déclaré par l'en-tête (`NONE` / `CRC32C` / `ED25519`) |
| `blocksCrcFailed()` | Blocs de données dont le CRC32C de trame n'a pas été vérifié (ignorés) |
| `blocksSignatureSkipped()` | Blocs de signature ignorés (canal réservé `0xFFFE`) |
| `blocksSkippedZeroLength()` | Blocs ignorés à cause d'un champ de longueur `0` |
| `blocksSkippedStatusEvent()` | Blocs `bcStatusEvent` ignorés (octet de contrôle 3) |
| `blocksSkippedReservedType()` | Blocs ignorés avec octet de contrôle réservé (0, 2, ≥9) ainsi que les deux variantes non spécifiées de `bcMessageEvent` |
| `blocksSkippedDeprecatedType()` | Blocs ignorés avec octet de contrôle déprécié (`bcTrustedTimestamp`, octet de contrôle 1) |
| `verificationStatus()` | État de vérification récapitulatif (voir ci-dessous) |

`verificationStatus()` résume le constat d'intégrité dans une chaîne :

- `"none"` — aucun profil d'intégrité ;
- `"crc_valid"` — niveau `crc`, chaque CRC de bloc vérifié ;
- `"invalid"` — niveau `crc`, au moins un bloc a échoué au CRC ;
- `"signature_unverifiable"` — un fichier signé dont ce lecteur `crc` ne peut
  pas vérifier les signatures.

```java
ReaderStats s = mgr.stats();
System.out.printf("%d Blöcke, Integrität=%s, komprimiert=%s (%s)%n",
        s.blocksRead(), s.verificationStatus(),
        s.compressed(), s.compressionFormat());
```

## Remarques sur les performances

- Le `DataManager` conserve tous les échantillons en mémoire, dans des
  tableaux primitifs (`double[]`, `long[]`, …) sans boxing. En règle
  générale, un fichier nécessite environ sa taille décompressée en RAM. Il
  n'existe pas d'API de streaming publique — il vaut donc mieux traiter les
  très gros volumes fichier par fichier ou filtrer avant le chargement.
- Les accesseurs typés (`asDoubles()`, `asLongs()`, …) **copient** dans un
  nouveau tableau à chaque appel. Dans les boucles, conservez le résultat une
  fois dans une variable locale au lieu d'appeler l'accesseur de façon
  répétée.
- Les fichiers de terrain réels de quelques Mo se chargent en quelques
  millisecondes. La décompression OSFZ transparente se fait en flux et
  n'a pas besoin d'un second tampon pour le fichier entier.

La suite se trouve dans [Écriture](./writing.md), [Gestion des
erreurs](./error-handling.md), [Outils](./tools.md) ou
[Architecture](./architecture.md) ; le format binaire lui-même est décrit par
la [spécification OSF](../../osf_general.md).

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
