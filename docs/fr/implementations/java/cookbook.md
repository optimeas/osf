---
title: Livre de recettes
description: Recettes pour les tâches courantes avec la bibliothèque OSF Java — de l'inspection de fichiers à l'export CSV, jusqu'à la boucle d'enregistrement embarquée tolérante aux pannes
sidebar_position: 7
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - Exemples
  - Livre de recettes
  - Recettes
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Livre de recettes

Recettes compactes, prêtes à copier, pour la bibliothèque Java. Toutes
supposent le module JPMS `com.optimeas.osf` sur le module path (Java 21) et
importent les types publics du paquet `com.optimeas.osf` :

```java
import com.optimeas.osf.*;
import java.nio.file.Path;
```

La gestion des erreurs est réduite au minimum. Les API de lecture et
d'écriture signalent les erreurs par l'exception non contrôlée `OsfException`
(voir [Gestion des erreurs](./error-handling.md)) ; pour une vue d'ensemble
des classes, voir [Architecture](./architecture.md), [Lecture](./reading.md)
et [Écriture](./writing.md).

## Inspecter un fichier (métadonnées + liste des canaux)

`loadFromFile` reconnaît de manière transparente OSF4, OSF5 et **OSFZ**
compressé (gzip/zlib) d'après les premiers octets — le même appel pour
`.osf` et `.osfz`.

```java
DataManager mgr = DataManager.loadFromFile(Path.of(pfad));   // .osf oder .osfz

System.out.println("version:  " + mgr.version());            // OSF4 / OSF5
System.out.println("creator:  " + mgr.metadata().getOrDefault("creator", "-"));
System.out.println("created:  " + mgr.metadata().getOrDefault("created_utc", "-"));
System.out.println("channels: " + mgr.channels().size());

for (DataChannel ch : mgr.channels()) {
    System.out.printf("  [%d] %s  (%d Samples, Einheit: %s)%n",
            ch.index(), ch.name(), ch.sampleCount(),
            ch.physicalUnit() == null ? "-" : ch.physicalUnit());
}
```

## Récupérer un canal sous forme de valeurs `double` avec horodatages

`asDoubles()` élargit tout type de données numérique en `double` ;
`timestampsNs()` fournit le tableau d'horodatages parallèle — reconstruit à
partir de l'heure de départ et de la fréquence d'échantillonnage pour les
canaux équidistants, explicite pour les canaux horodatés. Les deux tableaux
ont la même longueur.

```java
DataChannel ch = mgr.channelByName("Motor.Drehzahl").orElseThrow();

long[]   ts = ch.timestampsNs();
double[] v  = ch.asDoubles();          // wirft OsfException.UnsupportedType, wenn nicht numerisch

for (int i = 0; i < v.length; i++) {
    long   tNs   = ts[i];
    double wert  = v[i];
    // … tNs, wert verarbeiten
}
```

`channelByName` renvoie un `Optional<DataChannel>` ; à l'inverse,
`channelByIndex(int)` accède au canal via l'index du métabloc.

## Itérer de façon générique sur des types de données mixtes

Un `switch` sur `dataType()` rend les outils d'export indépendants du type de
données. Chaque accès `as…()` lève `OsfException.UnsupportedType` s'il ne
correspond pas au canal — le `switch` l'évite.

```java
long[] ts = ch.timestampsNs();
switch (ch.dataType()) {
    case DOUBLE, FLOAT, INT8, INT16, INT32, INT64,
         UINT8, UINT16, UINT32, UINT64, BOOL -> {
        double[] v = ch.asDoubles();
        for (int i = 0; i < v.length; i++) row(ts[i], v[i]);
    }
    case GPS_LOCATION -> {
        GpsLocation[] g = ch.asGps();
        for (int i = 0; i < g.length; i++) row(ts[i], g[i].latitude(), g[i].longitude());
    }
    case STRING -> {
        String[] s = ch.asStrings();
        for (int i = 0; i < s.length; i++) row(ts[i], s[i]);
    }
    case BINARY -> {
        byte[][] b = ch.asBinaries();
        for (int i = 0; i < b.length; i++) row(ts[i], b[i].length + " Bytes");
    }
    default -> { /* UNSUPPORTED: Kanal geladen, aber Werte nicht projizierbar */ }
}
```

## Export CSV minimal

```java
import java.io.BufferedWriter;
import java.nio.file.Files;

DataChannel ch = mgr.channelByName(name).orElseThrow();
long[]   ts = ch.timestampsNs();
double[] v  = ch.asDoubles();

try (BufferedWriter w = Files.newBufferedWriter(Path.of("kanal.csv"))) {
    w.write("timestamp_ns,value\n");
    for (int i = 0; i < v.length; i++) {
        w.write(ts[i] + "," + v[i] + "\n");
    }
}
```

Un tableau large (une colonne par canal, une ligne par horodatage) est
produit par l'outil `osf-cli` avec `osf convert --to csv` — voir
[Outils](./tools.md).

## Convertir OSF4 → OSF5 (y compris en entrée OSFZ)

`BlockWriter.fromManager` reconstruit un writer à partir d'un fichier chargé
— métadonnées, définitions de canaux et tous les échantillons. La sortie est
**toujours en OSF5**, quel que soit le format source ; les échantillons
restent identiques à l'octet près.

```java
DataManager mgr = DataManager.loadFromFile(Path.of("alt_osf4.osf")); // oder .osfz
BlockWriter.fromManager(mgr).writeToFile(Path.of("neu_osf5.osf"));
```

Pour une **sortie OSFZ**, écrire le `BlockWriter` dans un `GZIPOutputStream` :

```java
import java.util.zip.GZIPOutputStream;

try (var os = new GZIPOutputStream(Files.newOutputStream(Path.of("neu.osfz")))) {
    BlockWriter.fromManager(mgr).writeTo(os);
}
```

## Écrire un nouveau fichier avec des données d'analyse

Le `BlockWriter` collecte tous les échantillons en mémoire et écrit le
fichier en une seule passe. Les canaux sont d'abord déclarés (la valeur de
retour est l'index du canal), puis remplis.

```java
BlockWriter w = new BlockWriter();
w.setMetadata("creator", "mein-tool/1.0");

int fftPeak = w.addTimestampedChannel("ergebnis.fft_peak", DataType.DOUBLE);

for (var e : ergebnisse) {
    w.writeSample(fftPeak, e.timestampNs(), e.peakHz());
}

w.writeToFile(Path.of("ergebnis.osf"));   // created_utc wird automatisch gesetzt
```

Pour des séries d'analyse équidistantes (fréquence fixe, uniquement
`float`/`double`) au lieu de canaux horodatés :

```java
int spektrum = w.addEquidistantChannel("ergebnis.psd", DataType.DOUBLE, 2, 1000.0); // 1 kHz
w.startEquidistantSegment(spektrum, startNs, block1);   // double[]
w.appendEquidistantSamples(spektrum, block2);           // hängt an dasselbe Segment an
```

## Boucle d'enregistrement embarquée tolérante aux pannes

Le `StreamingWriter` écrit immédiatement le préambule et chaque bloc terminé,
et appelle `force(true)` (fsync) après chaque bloc. La durabilité est donc
assurée **par bloc** : après une coupure de courant, le `DataManager` lit le
fichier jusqu'au dernier bloc complet et met `stats().truncationSeen()` pour
les derniers octets tronqués. Il implémente `Closeable` — un
`try`-with-resources écrit les blocs restants, force et ferme.

```java
try (StreamingWriter w = StreamingWriter.create(Path.of("/data/rec_0001.osf"))) {
    w.setMetadata("creator", "logger-fw/3.2");

    int temp = w.addTimestampedChannel("temp", DataType.DOUBLE, 2, "degC", null);
    int tuer = w.addTimestampedChannel("tuer", DataType.BOOL, 2);   // Event-Kanal
    w.begin();   // Präambel früh festschreiben (sonst lazy beim ersten Sample)

    while (laeuft) {
        long now = jetztNs();

        if (neuerMesswert)  w.writeSample(temp, now, wert);
        if (tuerGeaendert)  w.writeSample(tuer, now, offen);

        // Optional: w.flush() erzwingt die noch offenen Teilblöcke sofort.
        warteAufNaechstenTick();
    }
}   // close(): Restblöcke schreiben, force, schließen
```

Le `StreamingWriter` fixe `sizeoflengthvalue` par canal et ne peut pas le
relever après coup — les canaux contenant de grands échantillons variables
(voir ci-dessous) doivent donc être déclarés d'emblée avec `4`.

## Images/blobs comme canal Binary

Les grands échantillons variables (JPEG, tampons bruts) dépassent facilement
le champ de longueur de 2 octets ; il faut donc créer le canal avec
`sizeoflengthvalue = 4`. Via la map d'attributs, un `mimetype` peut être
inscrit dans le métabloc. Les échantillons variables ne sont jamais regroupés
— un bloc par échantillon.

```java
// Schreiben (StreamingWriter — sov=4 wegen Sample-Größe):
int kamera = w.addTimestampedChannel(
        "kamera.snapshots", DataType.BINARY, 4,
        null, java.util.Map.of("mimetype", "image/jpeg"));

w.writeSample(kamera, tsNs, jpegBytes);   // byte[]
```

```java
// Lesen:
DataChannel ch = mgr.channelByName("kamera.snapshots").orElseThrow();
long[]   ts    = ch.timestampsNs();
byte[][] blobs = ch.asBinaries();

for (int i = 0; i < blobs.length; i++) {
    long   t    = ts[i];
    byte[] jpeg = blobs[i];
    // … jpeg speichern/decodieren
}
```

Le `BlockWriter` connaît à l'avance la taille maximale des échantillons : la
forme abrégée `addTimestampedChannel(name, DataType.BINARY)` y suffit et
relève automatiquement `sizeoflengthvalue` de 2 à 4 si nécessaire.

## Écrire et vérifier le profil d'intégrité `crc`

Les deux writers peuvent produire le niveau `crc` : un CRC32C sur le métabloc
dans l'en-tête magique, plus un CRC32C de trame par bloc.

```java
BlockWriter w = new BlockWriter();
w.setIntegrity(IntegrityProfile.CRC32C);
// … Kanäle + Samples …
w.writeToFile(Path.of("gesichert.osf"));
```

À la lecture, le `DataManager` vérifie en mode fail-closed : un CRC de
métabloc erroné lève `OsfException.MetablockCrcMismatch`, les blocs
défectueux sont ignorés et comptés.

```java
DataManager mgr = DataManager.loadFromFile(Path.of("gesichert.osf"));
ReaderStats s = mgr.stats();

System.out.println(s.verificationStatus());   // "crc_valid" oder "invalid"
if (s.blocksCrcFailed() > 0) {
    System.out.println("ACHTUNG: " + s.blocksCrcFailed() + " Blöcke mit CRC-Fehler");
}
```

## Afficher les statistiques de lecture

```java
ReaderStats s = mgr.stats();
System.out.printf("Blöcke gelesen: %d%n", s.blocksRead());
if (s.truncationSeen()) {
    System.out.println("ACHTUNG: Datei war abgeschnitten");
}
if (s.compressed()) {
    System.out.println("Quelle war OSFZ (" + s.compressionFormat() + ")");
}
```

Les détails sur les éléments internes (flux de blocs, assembleur, chunking)
se trouvent sous [Éléments internes](./internals.md) ; le build et les
coordonnées Maven sous [Build](./building.md). La vue d'ensemble de tous les
outils est fournie par la [page d'accueil Java](../java.md).

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
