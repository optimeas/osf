---
title: OSF4 – Documentation spécifique
description: Détails et spécifications du format Open Streaming Format version 4 (OSF4)
#sidebar_label: OSF4
image: "/img/om_social_card.png"
keywords:
  - OSF4
  - Fileformat
  - XML
last_update:
  date: 2026-07-07
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# OSF4 – Documentation spécifique

Ce document décrit tous les aspects de l'**Open Streaming Format version 4 (OSF4)** qui vont au-delà de la description générale d'OSF.  
Il complète le fichier [`osf_general.md`](../osf_general.md), qui explique toutes les structures communes à OSF4 et OSF5.

OSF4 est la version classique du format. Elle utilise exclusivement **XML** pour le bloc de métadonnées et constitue la base de la rétrocompatibilité dans OSF5.

Le profil d'intégrité (CRC32C / signature) existe à partir d'OSF5 ; les fichiers OSF4 ne sont pas concernés. Voir [Profil d'intégrité OSF5](osf5_integrity.md).



## En-tête magique dans OSF4

- **Identifiants autorisés :**  
  - `OSF4`  
  - `OCEAN_STREAM_FORMAT4` — identifiant hérité, toujours écrit par les appareils livrés  
  - `OCEAN_STREAMING_FORMAT4` — ancienne graphie historique

- **Format :**  
  ```
  OSF4 173762\n
  ```

- **Propriétés :**  
  - Toujours un **bloc de métadonnées XML** directement après l'en-tête.  
  - Aucune prise en charge de JSON.  
  - Les analyseurs OSF5 peuvent lire intégralement les fichiers OSF4.

---

## Bloc de métadonnées dans OSF4 (XML)

OSF4 utilise exclusivement **XML** pour le bloc de métadonnées.  
Il contient toutes les informations relatives au fichier et aux canaux et commence par un prologue standard :

```xml
<?xml version="1.0" encoding="UTF-8"?>
```

### Élément racine `<osf>`

Exemple :

```xml
<osf version="4"
     created_utc="2019-08-12T12:23:01+02:00"
     creator="smartdevice:14001000078"
     created_at_longitude="50.2"
     created_at_latitude="8.65"
     created_at_altitude="193"
     reason="BOOT"
     total_seq_no="0"
     triggered_seq_no="0"
     namespacesep="."
     tag="preview"
     comment="">
```

#### Attributs :

- **version :** version du format OSF4 (par défaut : `"1"`).  
- **created_utc :** date et heure de création du fichier au format ISO 8601 (UTC).  
- **creator :** identification de l'appareil ou du programme générateur.  
- **created_at_longitude / latitude / altitude :** position géographique facultative.  
- **reason :** raison de la création du fichier (`BOOT`, `SEQUENCE`, `TRIGGERED`).  
- **total_seq_no :** numéro de séquence absolu depuis le démarrage du système.  
- **triggered_seq_no :** numéro de séquence relatif depuis le dernier déclenchement.  
- **namespacesep :** séparateur des noms de canaux hiérarchiques (par défaut : `"."`).  
- **tag :** étiquette libre pour classer le fichier (par défaut : `"preview"`).  
- **comment :** commentaire facultatif.  

---

## Liste des canaux `<channels>`

Contient tous les canaux du fichier.

```xml
<channels count="8">
    <channel index="0"
             name="Sensor.Temperature"
             channeltype="scalar"
             datatype="double"
             timeincrement="1000000"
             sizeoflengthvalue="2"
             physicalunit="°C"
             reference="uuid"
             physicaldimension="temperature"/>
</channels>
```

- **count :** nombre de canaux dans le fichier.

---

## Description du canal `<channel>`

Tous les paramètres sont décrits comme dans la documentation générale d'OSF ; pour OSF4, les points suivants s'appliquent :

- **Bloc de métadonnées :** toujours XML  
- **`channeltype` pris en charge :** `scalar`, `vector`, `matrix`, `binary`  
- **Paramètres de vecteur et de matrice :** dans la définition du canal via `rows`, `columns` et les attributs associés  
- **`sizeoflengthvalue` :** champ obligatoire (2 ou 4 octets)  

---

## Types de données pris en charge dans OSF4

| Type de données | Taille   | Description |
|-------------|----------|--------------|
| `bool`      | 1 octet  | Vrai/Faux |
| `int8`      | 1 octet  | Entier signé |
| `int16`     | 2 octets | Entier signé |
| `int32`     | 4 octets | Entier signé |
| `int64`     | 8 octets | Entier signé |
| `uint8`     | 1 octet  | Entier non signé, plage de valeurs 0 … 255 |
| `uint16`    | 2 octets | Entier non signé, plage de valeurs 0 … 65 535 |
| `uint32`    | 4 octets | Entier non signé, plage de valeurs 0 … 4 294 967 295 |
| `uint64`    | 8 octets | Entier non signé, plage de valeurs 0 … 18 446 744 073 709 551 615 |
| `float`     | 4 octets | IEEE 754 simple précision |
| `double`    | 8 octets | IEEE 754 double précision |
| `string`    | variable | Codé en UTF-8, longueur définie par la taille du bloc. Toujours écrit sur disque avec un octet nul final (`0x00`) – voir [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary) pour les règles d'écriture et de lecture selon les versions. |
| `gpslocation` | 24 octets | Structure pour les positions GPS |

## Terminaison des chaînes

Pour `bcAbsTimeStampData` avec `datatype=string` ou `datatype=binary` dans OSF4 :

- **Les écrivains DOIVENT** ajouter un octet nul final (`0x00`) à chaque charge utile.
- **Les lecteurs DOIVENT** supprimer inconditionnellement le dernier octet de la charge utile — sa présence est garantie.

Voir [`osf_general.md`](../osf_general.md#hinweis-zur-nullterminierung-von-string-und-binary) pour la justification et la règle complète applicable à plusieurs versions.

---

## Blocs de données dans OSF4

OSF4 utilise les structures de blocs de données décrites dans la spécification générale.  
Spécificités d'OSF4 :

- **Octet de contrôle :** tous les types de blocs d'origine `0–8` sont pris en charge.  
- **`bcContinuedRelStampData` :** encore utilisé dans OSF4 (supprimé à partir d'OSF5).  
- **`bcStatusEvent` et `bcMessageEvent` :** présents ; les écrivains NE DOIVENT PAS les générer. `bcMessageEvent` doit néanmoins être **lu** — les appareils en service sur le terrain écrivent encore ainsi les canaux `string` OSF4, et un lecteur qui ignore ce type de bloc perd silencieusement ces canaux. Voir [`osf_general.md`](../osf_general.md#bcmessageevent).  
- **Métadonnées :** toujours au format XML.

### `bcStartData` avec fréquence d'échantillonnage

Depuis cette révision de la spécification, `bcStartData` porte dans OSF4 — comme dans OSF5 — directement après l'horodatage de départ `int64`, un champ `double` contenant la **fréquence d'échantillonnage (Hz)**. Cette fréquence s'applique à tous les blocs `bcContinuedData` suivants du même canal, jusqu'à l'écriture d'un nouveau bloc `bcStartData`.

**Exemple de `bcStartData` dans OSF4 :**
`[int64 ZeitStart] [double SampleRate] [uint32 N] [double Wert1] [double Wert2] ... [double WertN]`

Les nouveaux écrivains OSF4 **doivent** écrire ce champ. Les lecteurs qui rencontrent un fichier OSF4 sans ce champ (anciens fichiers existants) échouent avec une erreur de format — il n'existe aucun repli implicite à partir de `timeincrement`.

### Restrictions :

- **Canaux équidistants (`bcStartData`, `bcContinuedData`) :** uniquement les types numériques (`int*`, `float`, `double`).  
- **Canaux horodatés (`bcAbsTimeStampData`, `bcContinuedRelStampData`) :** tous les types sont autorisés.

---

## Trailer XML et trailer magique

OSF4 prend en charge, en option, un bloc d'informations XML contenant des statistiques de canaux ainsi qu'un trailer magique.

### Exemple de trailer XML :

```xml
<trailer finalized_utc="2019-08-12T12:23:01+02:00" reason="fileStartGrid_min">
    <channels count="8">
        <channel index="0" samples="29452" last_ns="1384899599997800000"/>
        <channel index="1" samples="29452" last_ns="1384899599997800000"/>
    </channels>
</trailer>
```

### Trailer magique :

```
OSF_STREAM_END 321316454==============
```

- Longueur de 40 octets ; le nombre correspond à la position du bloc `0xFFFF`.

---

## Particularités et limites d'OSF4

- Uniquement un bloc de métadonnées XML, aucune prise en charge de JSON.  
- Octet de contrôle plus complexe, davantage de types de blocs qu'OSF5.  
- Canaux vectoriels et matriciels via des paramètres XML (`rows`, `columns`).  
- `bcContinuedRelStampData` est pris en charge, mais n'existe plus dans OSF5.  

---

## Exemple de fichier OSF4

```text
OSF4 30269
<?xml version="1.0" encoding="UTF-8"?>
<osf version="4" created_utc="2019-08-12T12:23:01+02:00" creator="smartdevice:14001000021">
  <channels count="2">
    <channel index="0" name="Sensor.Temperature" channeltype="scalar" datatype="double" timeincrement="1000000"/>
    <channel index="1" name="Sensor.Pressure" channeltype="scalar" datatype="double" timeincrement="1000000"/>
  </channels>
</osf>
[BEGIN OF BINARY DATA]
```

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
