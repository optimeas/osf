---
title: OSF5 – Documentation spécifique
description: Détails et spécifications du format Open Streaming Format version 5 (OSF5)
#sidebar_label: OSF5
image: "/img/om_social_card.png"
keywords:
  - OSF5
  - Fileformat
  - JSON
last_update:
  date: 2026-07-07
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# OSF5 – Documentation spécifique

Ce document décrit tous les aspects de l'**Open Streaming Format version 5 (OSF5)** qui vont au-delà de la description générale d'OSF.  
Il complète le fichier [`osf_general.md`](../osf_general.md), qui explique toutes les structures communes à OSF4 et OSF5.

OSF5 est l'évolution d'OSF4. Il utilise **JSON** comme format standard pour le bloc de métadonnées et simplifie l'octet de contrôle.  
OSF5 reste en même temps entièrement **rétrocompatible** avec OSF4.



## En-tête magique dans OSF5

- **Identifiants autorisés :**  
  - `OSF5`  
  - `OSF4` (pour la rétrocompatibilité)  
  - `OCEAN_STREAM_FORMAT4` (identifiant hérité, toujours écrit par les appareils livrés)  
  - `OCEAN_STREAMING_FORMAT4` (ancienne graphie historique)

- **Format :**  
  ```
  OSF5 84512\n
  ```

- **Détection du bloc de métadonnées :**  
  - Premier caractère `<` → XML (bloc de métadonnées OSF4)  
  - Premier caractère `{` → JSON (bloc de métadonnées OSF5)  

- **Propriétés :**  
  - **Bloc de métadonnées JSON** par défaut.  
  - Les analyseurs OSF5 peuvent lire le XML OSF4.



## Bloc de métadonnées dans OSF5 (JSON)

OSF5 utilise par défaut **JSON** pour le bloc de métadonnées.  
La structure correspond fonctionnellement à la variante XML d'OSF4, mais elle est plus facile à analyser et optimisée pour les systèmes embarqués.

### Exemple :

```json
{
  "osf": {
    "version": 5,
    "created_utc": "2025-07-27T12:00:00Z",
    "creator": "smartdevice:15002000001",
    "file_uuid": "f47ac10b-58cc-4372-a567-0e02b2c3d479",
    "channels": [
      {
        "index": 0,
        "name": "Sensor.Temperature",
        "channeltype": "scalar",
        "datatype": "double",
        "timeincrement": 1000000,
        "sizeoflengthvalue": 2,
        "physicalunit": "°C"
      }
    ]
  }
}
```

- **Différence par rapport à OSF4 :** JSON au lieu de XML, mais contenu logique identique.  
- **Compatibilité :** OSF5 peut toujours interpréter le XML OSF4.

#### Paramètre à l'échelle du fichier `file_uuid`

L'objet `osf` peut porter, au niveau du fichier, le paramètre **`file_uuid`** — un UUID (version 4), stocké sous forme de chaîne JSON. Il sert à l'**identité du fichier** et constitue l'interface par laquelle le niveau supérieur du système effectue le contrôle de séquence et de rejeu entre fichiers (interface au sens de la norme EN 50159). `file_uuid` est **obligatoire** au niveau d'intégrité *signed* et **recommandé** à tous les autres niveaux.


## Simplifications de l'octet de contrôle (types de blocs)

OSF5 reprend la structure de blocs d'OSF4, mais réduit le nombre de types de blocs utilisés et simplifie leur interprétation.

### Modifications par rapport à OSF4 :

- `bcContinuedRelStampData` n'est plus utilisé.  
- `bcStatusEvent` et `bcMessageEvent` ne sont plus générés. Ne plus être généré ne signifie pas ne plus être lu : les lecteurs doivent continuer à prendre en charge `bcContinuedRelStampData` et `bcMessageEvent` **dans toutes les versions**, car des fichiers contenant ces blocs existent sur le terrain — voir [`osf_general.md`](../osf_general.md#bcmessageevent).  
- Le bit 7 pour valeur unique/multiple reste inchangé.  

### Types de blocs pris en charge dans OSF5 :

| Valeur | Enum               | Description |
|------|--------------------|--------------|
| 0    | `bcReserved`       | Réservé à des usages internes. |
| 2    | `bcTimebaseRealign`| Ajustement de la base de temps (rarement utilisé). |
| 5    | `bcContinuedData`  | Poursuite des données à fréquence d'échantillonnage fixe. |
| 6    | `bcStartData`      | Bloc de démarrage à fréquence d'échantillonnage fixe. |
| 8    | `bcAbsTimeStampData` | Données avec horodatage absolu. |
| 9    | `bcIntegritySignature` | Ancre de signature d'intégrité (uniquement niveau *signed*, canal `0xFFFE`). Voir [Profil d'intégrité](osf5_integrity.md). Bit 7 = 0. |


## Profil d'intégrité

OSF5 définit un **profil d'intégrité optionnel à trois niveaux** (none ⊂ crc ⊂ signed), déclaré par un jeton facultatif dans l'en-tête magique. Le niveau *crc* ajoute à chaque bloc une somme de contrôle de trame (CRC32C) ; le niveau *signed* ajoute en outre une chaîne de signatures Ed25519 avec des certificats X.509 intégrés dans le bloc de métadonnées. Sans jeton déclaré, un fichier reste au niveau *none* et se comporte exactement comme auparavant. La description normative complète figure dans la spécification [Profil d'intégrité](osf5_integrity.md).


## Types de données pris en charge dans OSF5

OSF5 prend en charge les mêmes types de données qu'OSF4.

| Type de données | Taille   | Description |
|------------|----------|--------------|
| `bool`     | 1 octet  | Vrai/Faux |
| `int8`     | 1 octet  | Entier signé |
| `int16`    | 2 octets | Entier signé |
| `int32`    | 4 octets | Entier signé |
| `int64`    | 8 octets | Entier signé |
| `uint8`    | 1 octet  | Entier non signé, plage de valeurs 0 … 255 |
| `uint16`   | 2 octets | Entier non signé, plage de valeurs 0 … 65 535 |
| `uint32`   | 4 octets | Entier non signé, plage de valeurs 0 … 4 294 967 295 |
| `uint64`   | 8 octets | Entier non signé, plage de valeurs 0 … 18 446 744 073 709 551 615 |
| `float`    | 4 octets | IEEE 754 simple précision |
| `double`   | 8 octets | IEEE 754 double précision |
| `string`   | variable | Codé en UTF-8, longueur définie par la taille du bloc. Les écrivains OSF5 stockent la charge utile sans octet nul final ; les lecteurs OSF5 ne doivent supprimer aucun octet final – voir [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary). |
| `binary` *(alias : `bytearray`)* | variable | Séquences d'octets quelconques pour des données d'image, audio ou autres données binaires avec type MIME. La longueur maximale est déterminée par `sizeoflengthvalue`. Les écrivains OSF5 stockent la charge utile sans octet nul final ; les lecteurs OSF5 ne doivent supprimer aucun octet final – voir [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary). |
| `gpslocation` | 24 octets | Structure pour les positions GPS |

## Terminaison des chaînes

Pour `bcAbsTimeStampData` avec `datatype=string` ou `datatype=binary` dans OSF5 :

- **Les écrivains NE DOIVENT PAS** ajouter d'octet nul final (`0x00`). La charge utile se termine au dernier octet de données ; `sizeoflengthvalue` définit la longueur exacte.
- **Les lecteurs NE DOIVENT PAS** supprimer d'octet final. Un `0x00` final est traité comme un octet de données ordinaire.

Voir [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary) pour la justification.

### `bcStartData` avec fréquence d'échantillonnage

Comme dans la spécification générale et dans OSF4, `bcStartData` porte dans OSF5, directement après l'horodatage de départ `int64`, un champ `double` contenant la **fréquence d'échantillonnage (Hz)**. Elle s'applique à tous les blocs `bcContinuedData` suivants du même canal, jusqu'à l'écriture d'un nouveau bloc `bcStartData`. Plusieurs blocs `bcStartData` par canal, avec des lacunes temporelles entre eux, sont explicitement autorisés.

**Exemple de `bcStartData` dans OSF5 :**
`[int64 ZeitStart] [double SampleRate] [uint32 N] [double Wert1] [double Wert2] ... [double WertN]`

## Trailer et bloc d'informations

OSF5 **ne prend plus en charge de bloc de données d'informations (0xFFFF) ni de trailer magique.**  
La fin du fichier est définie exclusivement par l'atteinte du dernier bloc de données entièrement écrit.

- **Conséquence :**  
  - Les analyseurs lisent le fichier jusqu'au dernier bloc cohérent.  
  - Il n'y a aucune statistique ni information supplémentaire en fin de fichier.  
  - Pour obtenir des informations sur l'intervalle de temps, le fichier doit être analysé en totalité ou en partie.


### Pourquoi OSF5 n'utilise plus de trailer ni de bloc d'informations

Dans OSF4, il existait en fin de fichier, en option, un **bloc de données d'informations (index 0xFFFF)** et un **trailer magique**, afin de rendre rapidement disponibles les métainformations et l'intervalle de temps du fichier.  
Dans OSF5, nous avons délibérément supprimé ces deux éléments.

### Raison principale : effort d'implémentation réduit

- L'écriture du bloc d'informations nécessite une préparation finale de tous les canaux et des informations temporelles.  
- Côté lecture, cela représente du code supplémentaire, à maintenir et à tester en plus du flux de données régulier.  
- Comme OSF est de toute façon conçu pour lire les fichiers jusqu'au dernier bloc complet, le trailer et le bloc d'informations n'apportent aucune valeur technique qui justifierait l'effort.

### Avantages complémentaires

- **Spécification plus simple :** moins de cas particuliers, implémentation plus légère côté embarqué comme côté PC.  
- **Pas de double conservation de l'information :** toutes les métadonnées pertinentes sont présentes dans le bloc de métadonnées et dans les blocs de données eux-mêmes.  
- **Les outils d'analyse peuvent faire de même :** les intervalles de temps et les statistiques peuvent être déduits des blocs réguliers lors de la lecture.

> **Conclusion :**  
> Renoncer au trailer et au bloc d'informations dans OSF5 est avant tout une décision en faveur d'une implémentation plus simple et plus facile à maintenir.  
> La robustesse du format est entièrement préservée – elle découle de la structure en blocs et non d'éléments supplémentaires en fin de fichier.


## Particularités et limites d'OSF5

- **JSON** comme format principal, XML uniquement pour la rétrocompatibilité.  
- **Octet de contrôle simplifié** avec moins de types de blocs.  
- Aucun trailer ni bloc de données d'informations en fin de fichier.  
- `bcContinuedRelStampData`, `bcStatusEvent`, `bcMessageEvent` ne sont plus générés — `bcContinuedRelStampData` et `bcMessageEvent` doivent toutefois toujours être lus (voir [`osf_general.md`](../osf_general.md#bcmessageevent)).



## Exemple de fichier OSF5

```text
OSF5 2048
{
  "osf": {
    "version": 5,
    "created_utc": "2025-07-27T12:00:00Z",
    "creator": "smartdevice:15002000001",
    "channels": [
      {
        "index": 0,
        "name": "Sensor.Temperature",
        "channeltype": "scalar",
        "datatype": "double",
        "timeincrement": 1000000
      },
      {
        "index": 1,
        "name": "Sensor.Force",
        "channeltype": "scalar",
        "datatype": "double",
        "physicalunit": "N"
      },
      {
        "index": 2,
        "name": "Sensor.Path",
        "channeltype": "scalar",
        "datatype": "double",
        "physicalunit": "mm"
      }
    ]
  }
}
[BEGIN OF BINARY DATA]
```

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
