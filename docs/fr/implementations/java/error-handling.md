---
title: Gestion des erreurs
description: La hiérarchie d'exceptions OsfException, le catalogue complet des erreurs, le modèle de lecture best effort et l'état d'intégrité dans ReaderStats
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - OsfException
  - Best effort
  - Intégrité
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Gestion des erreurs

L'implémentation Java signale les erreurs par des **exceptions**. Toutes les
erreurs de la bibliothèque sont des instances de `OsfException`, qui hérite
de `RuntimeException` — ce sont donc des exceptions **non contrôlées** : pas
de clause `throws` obligatoire, pas de `try/catch` imposé. Qui veut les
intercepter intercepte précisément `OsfException` (ou l'une de ses
sous-classes) ; les autres les laissent se propager jusqu'à un gestionnaire
central.

Le **lecteur best effort** est distinct : un bloc de données isolé,
défectueux ou tronqué, ne met *pas* fin à la lecture par une exception ; il
est ignoré et comptabilisé dans `ReaderStats`. Seules les erreurs
structurelles *avant* le flux de blocs (en-tête, métabloc) et les violations
de préconditions strictes lèvent une exception. Cette séparation est le cœur
de la gestion des erreurs : échouer franchement tant que le fichier est par
principe ininterprétable — tenir bon dès que seuls quelques blocs de fin sont
perdus.

## La hiérarchie d'exceptions `OsfException`

```java
package com.optimeas.osf;

public class OsfException extends RuntimeException {
    // Nachricht (und optional Ursache) — die Nachricht ist nur zur Anzeige
    public static final class UnsupportedType       extends OsfException { }
    public static final class MalformedFile         extends OsfException { }
    public static final class UnknownHeaderToken    extends OsfException { }
    public static final class MetablockCrcMismatch  extends OsfException { }
}
```

Règles :

- **Se brancher sur le type, n'afficher que le message.** `getMessage()`
  est un détail lisible par l'humain et ne fait pas partie de l'API — son
  libellé peut changer. Pour réagir par programmation, on teste la classe
  (`instanceof` / ordre des `catch`).
- Une `OsfException` **sans** sous-classe plus spécifique correspond au cas
  « API valide, mais mal utilisée ou échec d'E/S » — surtout depuis les
  writers.
- Lorsqu'une `IOException` sous-jacente est à l'origine de l'erreur, elle est
  transmise comme `cause` (`getCause()`), afin que la trace de pile reste
  complète.

## Catalogue des erreurs

### À l'ouverture et à l'analyse (erreurs franches)

Ces exceptions surviennent avant même qu'un bloc utile soit lu — le fichier
n'est pas interprétable comme OSF et est rejeté entièrement.

| Exception | Signification | Source typique |
|---|---|---|
| `MalformedFile` | Défaut structurel : pas d'en-tête magique bien formé, identifiant de version inconnu, champ obligatoire manquant dans le métabloc, nombre non analysable, `sizeoflengthvalue` invalide (≠ 2/4), fin de flux inattendue, pas de saut de ligne dans la fenêtre d'en-tête, JSON (OSF5) ou XML (OSF4) invalide. En cas de cause E/S, porte l'`IOException` comme `cause`. | Parseurs d'en-tête, de métabloc et de bloc |
| `UnknownHeaderToken` | Un jeton d'en-tête magique dont la bibliothèque ne connaît pas la clé (règle must-understand). Volontairement distinct de `MalformedFile`, afin qu'un jeton d'intégrité ou d'extension inconnu n'apparaisse pas comme une erreur de format numérique trompeuse. | En-tête magique |
| `MetablockCrcMismatch` | Le CRC32C déclaré dans le jeton d'en-tête `crc32c` ne correspond pas aux octets bruts du métabloc. Lorsque le profil d'intégrité est actif, le fichier est rejeté en mode fail-closed — les métadonnées sont considérées comme compromises. | Vérification du métabloc |
| `UnsupportedType` | Le fichier utilise un type de données **supprimé** de la spécification (`pair`, `triple`, `candata`, `gpsdata`). Rejet strict — l'ancienne disposition de charge utile ne peut pas être reproduite à partir d'un build actuel. Sert aussi d'erreur lors de l'**accès** : un getter de mauvais type sur `DataChannel` (par ex. des doubles depuis un canal String). | Parseur de métabloc, `DataChannel` |

### À l'écriture et lors de l'utilisation de l'API

Les writers (`StreamingWriter`, `BlockWriter`) lèvent une simple
`OsfException` pour toute précondition violée — elle signale une erreur de
programmation, et non un défaut de données :

| Déclencheur | Exemple de signification |
|---|---|
| Index de canal inconnu | Échantillon écrit sur un canal non déclaré |
| Incompatibilité de type | Le type d'écriture ne correspond pas au type de données déclaré du canal |
| Types de blocs mélangés | Un canal fournit des blocs équidistants *et* horodatés — interdit par la spécification |
| Mauvaise phase du cycle de vie | Échantillon écrit alors que le writer est encore en configuration ou déjà fermé |
| Aucun canal / longueur incohérente | `begin`/`writeTo` sans canaux déclarés ; `timestamps.length ≠ values.length` |
| Profil signé demandé | Le profil `ed25519` (signé) est rejeté à l'écriture par cette bibliothèque de niveau CRC |
| Erreur d'E/S | Fichier impossible à ouvrir, erreur d'écriture/de `force` — `IOException` comme `cause` |

## Lecteur best effort : ce qui n'est volontairement **pas** une erreur

Le lecteur de flux de blocs s'arrête là où un fichier est *encore lisible
par principe*, au lieu de lever une exception. Le résultat est consigné dans
`ReaderStats`, consultable via `manager.stats()` :

| Situation | Comportement |
|---|---|
| Le fichier se termine au milieu d'un bloc (coupure de courant, troncature) | Tous les blocs complets sont fournis, `stats.truncationSeen()` devient `true`, l'itération se termine proprement |
| Corps de bloc défectueux/altéré | Arrêt best effort à cet endroit précis : `truncationSeen()` = `true`, les autres blocs lus restent valides |
| Index de canal inconnu dans le flux de blocs | Sans définition, la largeur du champ de longueur est inconnue → arrêt (`truncationSeen()`) plutôt que tentative de déduction |
| Type de données futur inconnu | Le canal est `UNSUPPORTED` ; ses blocs sont ignorés d'après leur longueur, tous les autres canaux se chargent normalement |
| Le CRC de trame d'un bloc ne correspond pas | Le bloc est rejeté, `stats.blocksCrcFailed()` s'incrémente, la lecture se poursuit |
| Bloc de signature (canal réservé `0xFFFE`) | Non vérifiable par cette bibliothèque de niveau CRC → ignoré, `stats.blocksSignatureSkipped()` s'incrémente |
| Types de blocs réservés/vides | Consommés comme ignorés ; pas d'erreur |

Seules les erreurs *avant* le flux de blocs (en-tête, CRC du métabloc,
analyse du métabloc) lèvent une exception — aucune interprétation partielle
n'y est défendable.

## État d'intégrité — `ReaderStats.verificationStatus()`

Après le chargement, `stats.verificationStatus()` résume le résultat
d'intégrité dans une chaîne stable (vocabulaire issu de la spécification) :

| Valeur | Signification | Réaction recommandée |
|---|---|---|
| `"none"` | Le fichier ne porte aucun profil d'intégrité | Aucune — traitement normal |
| `"crc_valid"` | Profil `crc`, **chaque** CRC de bloc vérifié | Traiter les données comme intègres |
| `"invalid"` | Profil `crc`, au moins un CRC de bloc a échoué (`blocksCrcFailed() > 0`) | Avertir/rejeter ; les blocs concernés sont absents des données |
| `"signature_unverifiable"` | Fichier signé (`ed25519`) que cette bibliothèque de niveau CRC **ne peut pas** vérifier | Ne pas le présenter comme « signé de confiance » ; les données utiles restent néanmoins lisibles |

`verificationStatus()` découle uniquement du profil déclaré et des
compteurs — un `"invalid"` signifie concrètement que les blocs présentant une
erreur de CRC ont été ignorés (non fournis).

## `try/catch` en pratique

```java
import com.optimeas.osf.*;

try {
    DataManager mgr = DataManager.loadFromFile(Path.of("messung.osf"));

    ReaderStats st = mgr.stats();
    if (st.truncationSeen()) {
        log.warn("Datei am Ende abgeschnitten — {} Blöcke gelesen", st.blocksRead());
    }
    switch (st.verificationStatus()) {
        case "invalid" -> log.error("CRC-Fehler: {} Blöcke verworfen", st.blocksCrcFailed());
        case "signature_unverifiable" -> log.warn("Signatur nicht prüfbar");
        default -> { /* none / crc_valid — ok */ }
    }

    double[] werte = mgr.channelByName("temperatur")
                        .orElseThrow()
                        .asDoubles();          // wirft UnsupportedType bei Typ-Mismatch
} catch (OsfException.MetablockCrcMismatch e) {
    // Metadaten kompromittiert — Datei ablehnen
} catch (OsfException e) {
    // MalformedFile, UnknownHeaderToken, UnsupportedType, I/O …
    log.error("OSF konnte nicht geladen werden: {}", e.getMessage(), e);
}
```

Règles pratiques :

- **Intercepter le spécifique avant le général.** Les sous-classes
  spécifiques (`MetablockCrcMismatch`, `UnknownHeaderToken`) d'abord, puis
  `OsfException` comme filet de sécurité.
- **Vérifier `stats()` après le chargement** — un `load` réussi ne signifie
  pas que chaque bloc est arrivé. La troncature et les échecs de CRC sont
  silencieux et n'apparaissent que dans les compteurs.
- **Les appels de getters sur `DataChannel` peuvent lever
  `UnsupportedType`** lorsque le type demandé ne correspond pas au canal —
  vérifiez `dataType()` au préalable ou interceptez de façon ciblée.

## Cycle de vie du writer

Le `StreamingWriter` impose une machine à états `CONFIGURE →
STREAMING → CLOSED`. Un appel dans la mauvaise phase — échantillon avant
`begin()`, écriture après `close()` — lève `OsfException`. `close()`
est idempotent (un second appel est sans effet) et passe de façon fiable à
`CLOSED` même en cas d'erreur d'E/S lors du flush final, de sorte que
`try-with-resources` (le writer est `AutoCloseable`) ne laisse pas le
fichier ouvert.

Pour plus de détails, voir [Lecture](./reading.md), [Écriture](./writing.md),
[Architecture](./architecture.md) et le
[manuel du format OSF](../../osf_general.md). Retour à la
[présentation Java](../java.md).

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
