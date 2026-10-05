---
title: Description du format OSF
description: Description for common properties of OSF 4 and 5
#hide_title: true/false
#hide_table_of_contents: true/false
#sidebar_label: Label shown in the sidebar
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - Fileformat
  - OSF4
  - OSF5
  - OSF
last_update:
  date: 2026-07-28
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

## Description générale du format OSF
- Valable pour les versions de format 4 et 5 -

Le **Open Streaming Format (OSF)** est un format de données binaire, orienté blocs, destiné à l'enregistrement continu de données de mesure et de process liées au temps. Il est conçu pour convenir de manière optimale non seulement au **streaming sur des systèmes embarqués aux ressources limitées**, mais aussi au **traitement efficace, bloc par bloc, de grands volumes de données** sur des plateformes d'analyse puissantes – que ce soit sur des serveurs, des PC ou en post-traitement directement sur des appareils embarqués.


### Principes fondamentaux

* **Le temps comme axe central :** toutes les données sont enregistrées avec des informations temporelles univoques – de façon équidistante avec une trame temporelle fixe ou individuellement avec des horodatages.
* **Compatible streaming :** les données peuvent être écrites en continu dans le fichier pendant la mesure, sans connaître au préalable le volume total.
* **Robustesse :** même en cas de coupure de courant ou d'arrêt inattendu, toutes les données enregistrées jusqu'au dernier bloc écrit restent lisibles.
* **Structure ouverte :** combinaison d'un bloc de métadonnées clair (XML dans OSF4, JSON dans OSF5) et d'un flux de données binaire simple.
* **Enregistrement par blocs :** au lieu d'écrire séquentiellement des valeurs individuelles, des blocs de données sont utilisés. Cela réduit les opérations d'écriture et permet un chargement rapide de grands volumes de données.
* **Flexibilité :** prise en charge de différents types de données – des scalaires simples aux données binaires telles que des images ou des fichiers audio, en passant par les vecteurs et les matrices.
* **Métadonnées :** chaque canal peut être décrit par un nom, des unités physiques, des dimensions et des informations complémentaires facultatives.

### Structure de base d'un fichier OSF

Indépendamment de la version 4 ou de la version 5, tout fichier OSF suit le même schéma de base :

![Structure d'un fichier OSF.](media/dfa207f25f79c84ec79e2e2f6c2c42cc.jpg)


1. **En-tête magique**

   * Identifiant du format (OSF4, OSF5, OCEAN\_STREAM\_FORMAT4, OCEAN\_STREAMING\_FORMAT4)
   * Indication de la longueur du bloc de métadonnées qui suit

2. **Bloc de métadonnées (XML ou JSON)**

   * Contient des informations sur les canaux, les types de données, les unités physiques et les données de contexte
   * Définit la structure des blocs de données suivants

3. **Blocs de données binaires**

   * Contiennent les valeurs de mesure proprement dites au format streaming
   * Prennent en charge les données équidistantes et horodatées
   * Peuvent contenir des valeurs uniques, des vecteurs, des matrices ou des données binaires

4. **Bloc de fin facultatif**

   * Dans OSF4, trailer XML facultatif avec statistiques et vue d'ensemble des canaux
   * Dans OSF5, ce trailer est supprimé par défaut

### Organisation des données

* **Canaux :** chaque flux de données est décrit comme un canal, qui contient le nom, le type de données, l'unité physique et, éventuellement, d'autres attributs.
* **Base de temps :** toutes les indications de temps sont enregistrées en nanosecondes depuis l'epoch et permettent une synchronisation de très haute précision.
* **En-tête de bloc :** chaque bloc de données commence par un index de canal et une indication de longueur, de sorte que le flux reste interprétable même en présence de canaux inconnus ou d'interruptions.
* **Octet de contrôle :** définit le type et la structure du bloc de données suivant (par ex. début, poursuite, type d'horodatage). Dans OSF5, l'utilisation de cet octet est simplifiée, mais reste fonctionnellement compatible.



## En-tête magique

Chaque fichier OSF commence par ce que l'on appelle l'**en-tête magique**.
Il sert deux objectifs :

1. Identification univoque comme fichier OSF.
2. Indication de la longueur du bloc de métadonnées qui suit, afin qu'il puisse être lu et analysé directement.

### Structure

L'en-tête magique est une ligne ASCII terminée par un saut de ligne (`\n`).

**Exemple OSF4 :**

```
OSF4 173762\n
```

* **OSF4** est l'identifiant du format.
* **173762** indique la longueur du bloc de métadonnées en octets.

**Exemple OSF5 :**

```
OSF5 84512\n
```

* **OSF5** désigne la nouvelle version.
* **84512** Ce nombre indique la longueur du bloc de métadonnées, qui est du JSON par défaut dans OSF5.

### Identifiants pris en charge

Pour des raisons de compatibilité, les implémentations OSF reconnaissent plusieurs en-têtes :

* **OSF4** – fichier OSF4 classique
* **OCEAN\_STREAM\_FORMAT4** – identifiant hérité pour les fichiers OSF4, toujours écrit dans les appareils livrés ; doit être pris en charge par les lecteurs
* **OCEAN\_STREAMING\_FORMAT4** – ancienne graphie historique ; à interpréter également comme OSF4
* **OSF5** – fichier OSF5

### Jetons d'en-tête (champs supplémentaires facultatifs)

La ligne d'en-tête OSF5 PEUT porter, après la longueur du bloc de métadonnées, des
jetons facultatifs séparés par des espaces :

```
header-line = identifier SP metablock-len *(SP token) LF
token       = key ":" value
```

Ici, `key` se compose de lettres minuscules `a-z`, de chiffres `0-9` et du
trait d'union `-` ; `value` désigne des caractères visibles sans espace. Il y a
exactement un espace entre les champs ; il n'y a pas d'espace avant le LF
final.

Les jetons sont **« must understand »** : si un lecteur rencontre une `key` qu'il
ne connaît pas, il DOIT rejeter le fichier — avec un diagnostic tel que
`unknown header token '<key>'`, **et non** comme une erreur d'analyse de nombre.

Les jetons sont une **fonctionnalité exclusivement OSF5** : les identifiants hérités d'OSF4 (`OSF4`,
`OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) NE DOIVENT PORTER AUCUN jeton ;
un jeton après un identifiant OSF4 constitue un fichier défectueux.

Les clés définies (`crc32c`, `ed25519`) et leur signification sont fixées dans le
profil d'intégrité — voir
[Profil d'intégrité OSF5](references/osf5_integrity.md).

### Détection du format du bloc de métadonnées

Le fait que le bloc de métadonnées qui suit soit en **XML** ou en **JSON** est déterminé à partir du premier caractère après l'en-tête :

* `<` → XML (format OSF4)
* `{` → JSON (format OSF5)
* Autre caractère → erreur

### Avantages

* **Démarrage rapide :** les lecteurs peuvent extraire immédiatement le bloc de métadonnées et le transmettre à l'analyseur approprié.
* **Compatible streaming :** aucune connaissance de la taille totale du fichier n'est nécessaire.
* **Rétrocompatible :** OSF5 traite les fichiers OSF4 (y compris OCEAN\_STREAM\_FORMAT4 et OCEAN\_STREAMING\_FORMAT4).
* **Implémentation simple :** une seule ligne suffit pour déterminer la version et l'analyseur.



## Bloc de métadonnées – canaux et métadonnées

Dans chaque fichier OSF, le **bloc de métadonnées** suit directement l'en-tête magique.
Il contient toutes les informations nécessaires pour interpréter correctement les blocs de données suivants. Cela comprend :

* **Paramètres du fichier :** informations de contexte sur le fichier et sa genèse.
* **Définitions de canaux :** décrivent chaque flux de données par son nom, son type de données et ses propriétés physiques.
* **Métadonnées :** informations supplémentaires qui ne sont pas directement liées à un canal (par ex. état du système, commentaires, données d'étalonnage).

Le bloc de métadonnées constitue le « sommaire » du fichier et est conçu pour être **univoque, lisible par machine et facile à étendre**.


### Paramètres du fichier (dans le bloc de métadonnées)

* **created\_utc** – date et heure de création du fichier en UTC au format ISO 8601
* **creator** – `optional` identification du générateur (par ex. numéro de série de l'appareil, nom de programme, UUID)
* **created\_at\_longitude** / **created\_at\_latitude** / **created\_at\_altitude** – `optional` position géographique de la création du fichier
* **reason** – `optional` raison de la création du fichier (par ex. `BOOT`, `SEQUENCE`, `TRIGGERED`)
* **total\_seq\_no** – `veraltet` numéro de séquence absolu depuis le démarrage du système (commençant à 0)
* **triggered\_seq\_no** – `veraltet` numéro de séquence relatif depuis le dernier événement de déclenchement (commençant à 0)
* **namespacesep** – `optional` séparateur pour les noms de canaux hiérarchiques (par défaut `"."`)
* **tag** – `optional` étiquette libre pour classer le fichier (par ex. `preview`)
* **comment** – `optional` texte de commentaire facultatif


### Définitions de canaux (channel)

Chaque canal décrit un flux de données au sein du fichier.
Les paramètres sont décrits ci-après


#### **Identification et organisation**

* **index**
  Index de canal univoque au sein du fichier (commençant à 0).
* **name**
  Nom du canal, éventuellement avec un chemin hiérarchique (par ex. `Motor.Temperatur`).
* **reference**
  Référence univoque facultative ou UUID pour identifier l'origine des données.


#### **Base de temps**

* **timeincrement**
  Incrément de temps fixe en nanosecondes pour les canaux équidistants.
  *Valeur = 0 ou non définie → le canal utilise des horodatages individuels.*

  **Remarque :** le `timeincrement` du bloc de métadonnées est une **indication facultative**. Lors d'enregistrements à haute résolution ou déclenchés, la fréquence d'échantillonnage exacte n'est souvent pas encore connue au moment de la création de l'en-tête. La **fréquence d'échantillonnage réellement valide est fournie dans chaque bloc `bcStartData` sous forme de `double`** et s'applique, à partir de ce moment, à tous les blocs `bcContinuedData` suivants du même canal, jusqu'à l'écriture d'un nouveau bloc `bcStartData`. Cela vaut aussi bien pour **OSF4** que pour **OSF5**.


#### **Types de données et structure** {#datentypen-und-struktur}

* **datatype**
  Type de données des valeurs enregistrées (par ex. `bool`, `int32`, `double`, `string`, `gpslocation`).
  → *Une description complète de tous les types de données et de leur encodage figure au chapitre [Types de données](#datentypen).*

* **channeltype**
  Type structurel du canal :

  * `scalar` – valeurs uniques dans le temps
  * `binary` – blocs binaires quelconques (par ex. images)
    *(Vecteur et matrice sont décrits dans un document distinct)*
    → *Une explication détaillée des types de canaux figure au chapitre [Types de canaux](#kanaltypen).*

* **sizeoflengthvalue**
  Taille de l'indication de longueur pour chaque bloc de données :

  * `2` → 2 octets (uint16, taille de bloc max. \~64 ko)
  * `4` → 4 octets (uint32, taille de bloc max. \~4 Go)
    Utilisé pour déterminer la taille du bloc et lire correctement les blocs de données dans le flux.
    *→ Une explication détaillée figure au chapitre [sizeoflengthvalue](#sizeoflengthvalue).*

* **mimetype**
  Facultatif, type MIME pour les canaux binaires (par ex. `image/jpeg`, `audio/wav`).

* **spectrumtype**
  Facultatif, type de données spectrales :

  * `amplitude` (par défaut)
  * `realImag`
  * `ampPhaseRad`
  * `ampPhaseDeg`


#### **Propriétés physiques**

* **physicalunit**
  Facultatif, unité physique (conforme SI, par ex. `V`, `°C`).
* **physicaldimension**
  Facultatif, description de la dimension physique (`temperature`, `pressure`, …).


#### **Affichage et informations complémentaires**

* **displayname**
  Nom d'affichage facultatif pour la visualisation ou l'interface graphique.
* **comment**
  Commentaire facultatif sur le canal.


### Métadonnées (info)

Les métadonnées complètent le fichier par des informations supplémentaires qui ne sont pas liées à un canal.
Paramètres courants :

* **name** – nom de l'information
* **value** – valeur (sous forme de chaîne ou typée)
* **datatype** – type de la valeur (`string`, `int32`, `float`, `binary`, `gpslocation`, etc.)
* **physicalunit** – facultatif, unité physique de la valeur

Les métadonnées sont librement définissables et conviennent pour :

* des données système ou d'appareil
* des commentaires et des messages d'état
* des valeurs d'étalonnage
* des informations complémentaires définies par l'utilisateur

> **Remarque :** `bytearray` est un alias de `binary`. Les deux désignations sont valides dans OSF4 et OSF5 et sont interprétées de façon identique par les lecteurs. Les écrivains **doivent** utiliser uniformément `binary` lors de l'écriture ; `bytearray` reste lisible pour la rétrocompatibilité.


### Avantages de la structure

* **Séparation claire entre données et description :** le bloc de métadonnées définit comment les données sont interprétées, sans contenir lui-même de valeurs de mesure.
* **Auto-descriptif :** les fichiers peuvent être lus et interprétés sans définitions externes.
* **Extensible :** de nouveaux canaux, types de données ou métainformations peuvent être ajoutés sans modifier le format de base.
* **Robuste :** grâce à des index et des indications de longueur fixes, le fichier reste interprétable même si tous les canaux ne sont pas connus.



## Paramètres principaux de la description des canaux

Les paramètres suivants définissent la structure fondamentale d'un canal dans OSF et déterminent comment les données sont enregistrées et interprétées au format streaming. Ils sont pertinents pour tous les canaux et constituent le fondement de la description des canaux.

<a name="datentypen"></a>
### Types de données (datatype) {#datentypen}

Le paramètre `datatype` définit le format de données des valeurs d'un canal. Chaque valeur est enregistrée dans un format binaire précisément défini.

#### Types de données pris en charge et encodage :

| Type de données | Taille (octets) | Description                                                                                                                                         |
| --------- | ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `bool`    | 1             | Vrai/Faux (0 = false, 1 = true)                                                                                                                    |
| `int8`    | 1             | Entier signé                                                                                                                              |
| `int16`   | 2             | Entier signé                                                                                                                              |
| `int32`   | 4             | Entier signé                                                                                                                              |
| `int64`   | 8             | Entier signé                                                                                                                              |
| `uint8`   | 1             | Entier non signé, plage de valeurs 0 … 255                                                                                                       |
| `uint16`  | 2             | Entier non signé, plage de valeurs 0 … 65 535                                                                                                    |
| `uint32`  | 4             | Entier non signé, plage de valeurs 0 … 4 294 967 295                                                                                             |
| `uint64`  | 8             | Entier non signé, plage de valeurs 0 … 18 446 744 073 709 551 615                                                                                |
| `float`   | 4             | IEEE 754 simple précision                                                                                                                            |
| `double`  | 8             | IEEE 754 double précision                                                                                                                            |
| `string`  | variable      | Codé en UTF-8, longueur définie par la taille du bloc. Sur disque dans `bcAbsTimeStampData` : dans OSF4 avec octet nul final (`0x00`), dans OSF5 sans octet final – voir le bloc de remarque ci-dessous pour les règles. Pour `bcMessageEvent`, la charge utile est en revanche préfixée par sa longueur et n'est jamais terminée par un octet nul dans aucune des deux versions. |
| `binary` *(alias : `bytearray`)* | variable | Séquences d'octets quelconques pour des données d'image, audio ou autres données binaires avec type MIME. La longueur maximale du bloc est déterminée par le champ `sizeoflengthvalue` du canal. Sur disque dans `bcAbsTimeStampData` : dans OSF4 avec octet nul final (`0x00`), dans OSF5 sans octet final – voir le bloc de remarque ci-dessous pour les règles. Pour `bcMessageEvent`, la charge utile est en revanche préfixée par sa longueur et n'est jamais terminée par un octet nul dans aucune des deux versions. |
| `gpslocation` | 24        | Structure pour les positions GPS (voir ci-dessous)                                                                                                            |

> **Remarque sur les types entiers :** les valeurs entières (`int8`, `int16`, `int32`, `int64`, `uint8`, `uint16`, `uint32`, `uint64`) sont généralement utilisées dans les fichiers OSF pour des **états, des informations d'état ou des valeurs de compteur**, et non comme valeurs brutes mises à l'échelle d'une grandeur physique. C'est pourquoi OSF ne connaît délibérément **aucun** paramètre `scale`/`offset` pour la conversion en valeurs physiques – les grandeurs physiques sont enregistrées directement en `float` ou `double`.

#### Remarque sur la terminaison par octet nul de `string` et `binary` {#hinweis-zur-nullterminierung-von-string-und-binary}

> **Remarque sur la terminaison par octet nul de `string` et `binary` :**
> L'octet `0x00` final des charges utiles `string` et `binary` dans `bcAbsTimeStampData` est un héritage historique de la sérialisation `QString` de Qt dans les appareils Optimeas d'origine. La longueur du bloc est déjà déterminée sans ambiguïté par `sizeoflengthvalue` ; un terminateur nul comme sentinelle est redondant. Pour les données binaires, c'est un véritable piège : un lecteur qui ne supprime pas l'octet produit des sorties invalides (un fichier JPEG avec un `0x00` ajouté n'est plus un JPEG valide). Inversement, un lecteur qui retire un octet d'une charge utile binaire OSF5 sans terminateur (un blob ASN.1 qui se termine légitimement par `0x00`, un message Protobuf, une chaîne terminée par un octet nul stockée en binaire) supprime un véritable octet de données. La règle est donc liée au numéro de version sur disque, afin que ni les écrivains ni les lecteurs n'aient à deviner.
>
> **OSF4 :**
>
> - **Les écrivains DOIVENT** ajouter un unique octet `0x00` final après chaque charge utile `string` et `binary` dans `bcAbsTimeStampData`.
> - **Les lecteurs DOIVENT** supprimer inconditionnellement le dernier octet de la charge utile — sa présence est garantie.
>
> **OSF5 :**
>
> - **Les écrivains NE DOIVENT PAS** ajouter d'octet final. La charge utile se termine au dernier octet de données ; `sizeoflengthvalue` définit la longueur exacte.
> - **Les lecteurs NE DOIVENT PAS** supprimer d'octet final. Un `0x00` final est traité comme un octet de données ordinaire.
>
> La longueur utile effective est donc : longueur du bloc dans OSF5, longueur du bloc moins un octet dans OSF4. Il n'y a ni heuristique ni étape de détection de sentinelle.

#### Structure `gpslocation`

```c
struct gps_location {
    double latitude;   // Breitengrad
    double longitude;  // Längengrad
    double altitude;   // Höhe
};
```

> **Remarque :** pour les canaux avec `datatype="binary"`, il est recommandé de définir le type MIME (`mimetype`) dans le canal (par ex. `image/jpeg`, `audio/wav`), afin de pouvoir interpréter les données sans ambiguïté. La taille maximale des blocs est déterminée par le paramètre [`sizeoflengthvalue`](#sizeoflengthvalue) du canal.


<a name="kanaltypen"></a>
### Types de canaux (channeltype) {#kanaltypen}

Le paramètre `channeltype` définit l'**organisation logique des valeurs** d'un canal (sa *forme de données*). Il détermine combien de valeurs sont enregistrées par bloc de données et quelle structure ont ces valeurs.

> **Important :** `channeltype` est la **forme de données**, et non le mode de stockage/d'échantillonnage. Le fait qu'un canal soit **équidistant** ou **horodaté** résulte de l'**octet de contrôle du bloc de données** (`bcStartData`/`bcContinuedData` ⇒ équidistant ; `bcAbsTimeStampData` ⇒ horodatage par valeur) en combinaison avec `timeincrement` — **et non** de `channeltype`. `equidistant` et `timestamped` ne sont donc **pas** des valeurs de `channeltype` et ne doivent pas être écrits comme tels.

OSF connaît les types de canaux suivants (`scalar`, `vector`, `matrix`, `binary` — voir aussi la description des champs sous [Types de données et structure](#datentypen-und-struktur) ainsi que [osf4.md](references/osf4.md)) :


#### scalar

* **Description :**
  Un canal avec exactement une valeur par instant.
  Application courante : grandeurs physiques continues (température, tension, pression) ou signaux numériques (par ex. état d'une porte).

* **Propriétés :**

  * Chaque bloc de données contient un ou plusieurs échantillons avec une valeur unique.
  * Prend en charge l'échantillonnage équidistant via `timeincrement` ou des horodatages individuels par valeur.
  * Type de canal le plus simple et le plus fréquemment utilisé.

* **Exemple XML (OSF4) :**

  ```xml
  <channel 
      index="0"
      name="Sensor.Temperature"
      channeltype="scalar"
      datatype="double"
      physicalunit="°C"/>
  ```

* **Exemple JSON (OSF5) :**

  ```json
  {
    "index": 0,
    "name": "Sensor.Temperature",
    "channeltype": "scalar",
    "datatype": "double",
    "physicalunit": "°C"
  }
  ```



#### vector

* **Description :**
  Un canal dans lequel chaque bloc de données contient une **suite de plusieurs valeurs** logiquement liées.
  Application courante : spectres de fréquence (FFT), segments de séries temporelles, enregistrements multicanaux dans un seul bloc.

* **Propriétés :**

  * La longueur du vecteur peut varier d'un bloc à l'autre.
  * Économise de la surcharge à haute fréquence d'échantillonnage, car plusieurs valeurs sont écrites dans un bloc.
  * Peut fonctionner aussi bien avec des horodatages par bloc qu'avec un incrément de temps fixe.
  * Nécessite des paramètres supplémentaires pour les informations d'axes (document distinct).

* **Exemple XML (OSF4) :**

  ```xml
  <channel 
      index="2"
      name="FFT.Magnitude"
      channeltype="vector"
      datatype="float"
      physicalunit="dB"/>
  ```

* **Exemple JSON (OSF5) :**

  ```json
  {
    "index": 2,
    "name": "FFT.Magnitude",
    "channeltype": "vector",
    "datatype": "float",
    "physicalunit": "dB"
  }
  ```



#### matrix

* **Description :**
  Un canal qui enregistre, pour chaque horodatage, une **structure de données bidimensionnelle**.
  Application courante : classifications Rainflow, cartes thermiques (heatmaps), réseaux de capteurs 2D, données d'image.

* **Propriétés :**

  * La taille de la matrice peut varier d'un bloc à l'autre.
  * Permet des structures de données complexes dans une base de temps unifiée.
  * Nécessite des paramètres supplémentaires pour la description des lignes et des colonnes (document distinct).

* **Exemple XML (OSF4) :**

  ```xml
  <channel 
      index="5"
      name="Rainflow.Matrix"
      channeltype="matrix"
      datatype="int32"
      physicalunit="counts"/>
  ```

* **Exemple JSON (OSF5) :**

  ```json
  {
    "index": 5,
    "name": "Rainflow.Matrix",
    "channeltype": "matrix",
    "datatype": "int32",
    "physicalunit": "counts"
  }
  ```



#### binary

* **Description :**
  Un canal qui enregistre, à chaque instant, un **bloc binaire quelconque** (un blob par valeur).
  Application courante : images, extraits audio, messages sérialisés.

* **Propriétés :**

  * Équivalent, pour la charge utile, à un canal `scalar` avec `datatype="binary"` ; un lecteur traite les deux notations de façon identique.
  * Il est recommandé de définir le `mimetype` du canal (par ex. `image/jpeg`).
  * La taille maximale des blocs est déterminée par `sizeoflengthvalue`.

* **Exemple JSON (OSF5) :**

  ```json
  {
    "index": 3,
    "name": "Camera.Frame",
    "channeltype": "binary",
    "datatype": "binary",
    "mimetype": "image/jpeg"
  }
  ```



#### Remarques sur Vector et Matrix

* **Paramètres supplémentaires :** les deux types nécessitent des métainformations sur les dimensions, les axes, les unités physiques et, le cas échéant, les libellés. Elles sont décrites en détail dans des documents distincts.
* **Efficacité :** les canaux vectoriels et matriciels réduisent les opérations d'écriture et conviennent particulièrement aux données à haute fréquence d'échantillonnage ou de structure complexe.
* **Flexibilité :** la taille et la structure des blocs peuvent varier, ce qui permet une adaptation à différents scénarios de mesure.
* **Synchronisation :** ils partagent la même base de temps que les canaux scalar, de sorte que différents types de données peuvent être stockés dans un même fichier avec une synchronisation exacte.

---

#### Récapitulatif

* **`scalar`** – type de canal simple, une valeur par instant. Idéal pour les grandeurs de mesure continues.
* **`vector`** – plusieurs valeurs dans un bloc, optimisé pour les spectres de fréquence et les données à haute fréquence.
* **`matrix`** – blocs multidimensionnels, adaptés aux classifications, aux données d'image et de tableaux.
* **`binary`** – un bloc binaire quelconque par instant (images, audio, messages sérialisés) ; équivalent à `scalar` + `datatype="binary"`.

> **Remarque :** grâce à la combinaison de ces types de canaux, OSF couvre aussi bien les signaux simples que les jeux de données complexes tout en restant facile à implémenter.


<a name="sizeoflengthvalue"></a>
### Champ de taille de bloc (sizeoflengthvalue) {#sizeoflengthvalue}

Le paramètre `sizeoflengthvalue` définit la taille du champ de longueur placé devant chaque bloc de données d'un canal. Il détermine donc combien d'octets sont utilisés pour indiquer la taille du bloc, et par conséquent la taille maximale d'un bloc de données individuel.

#### Objet

OSF est un format de streaming. Chaque bloc de données peut avoir une taille différente et contient un nombre variable de valeurs de mesure. Pour pouvoir lire correctement ces blocs, leur longueur doit être connue. Le champ `sizeoflengthvalue` indique si **2 octets** ou **4 octets** sont utilisés pour l'indication de longueur.

#### Valeurs

* **2** – le champ de longueur fait 2 octets (uint16).

  * Valeur maximale : 65.535 octets par bloc de données.
  * Valeur par défaut pour les canaux de mesure courants avec des tailles de bloc modérées.
  * Consommation de mémoire et surcharge réduites.
* **4** – le champ de longueur fait 4 octets (uint32).

  * Valeur maximale : \~4 Go par bloc de données.
  * Convient aux canaux avec de très gros paquets de données, par ex. données d'image, audio ou binaires.

#### Valeur par défaut

Si elle n'est pas indiquée explicitement, `sizeoflengthvalue="2"` est utilisé.

#### Effets

* **Besoin en mémoire :** 2 octets économisent de la place pour de petits blocs, 4 octets permettent de grandes quantités de données.
* **Lisibilité :** avant d'interpréter chaque bloc, le lecteur doit lire l'indication de longueur et traiter les `N` octets suivants comme un bloc.
* **Résistance aux erreurs :** même en cas d'écriture interrompue, le lecteur peut ignorer correctement des blocs et trouver le prochain bloc valide.

#### Recommandations

* Pour les signaux continus et les canaux à valeurs scalaires → utiliser `2`.
* Pour les canaux binaires contenant des images, de l'audio ou de gros paquets de données → choisir `4`.
* Choix uniforme par canal ; peut être défini canal par canal dans la définition du canal.


## Blocs de données

Les **blocs de données** constituent le cœur du format OSF. Ils contiennent les valeurs de mesure proprement dites et sont structurés de façon à pouvoir être écrits et lus efficacement, aussi bien lors du streaming continu sur des systèmes embarqués que lors du traitement par blocs sur des serveurs et des PC. Chaque bloc de données est autonome et reste interprétable même en cas d'interruption de l'enregistrement.

### Introduction

Dans OSF, toutes les valeurs de mesure sont enregistrées dans des **blocs de données**. Chaque bloc est une unité autonome qui contient une ou plusieurs valeurs d'un canal et qui est associée à des informations temporelles.  
Le concept de blocs permet deux propriétés centrales du format :

- **Streaming continu** : les valeurs peuvent être écrites au fil de l'enregistrement, sans connaître la structure complète du fichier.  
- **Traitement efficace** : grâce à l'enregistrement par blocs, de grands volumes de données peuvent être chargés et traités rapidement sur des serveurs, des PC ou en post-traitement.

Chaque bloc de données est conçu de manière à rester lisible, même en cas d'interruption soudaine de la mesure (par ex. coupure de courant), jusqu'à la dernière unité entièrement écrite.  
La structure des blocs de données est identique pour OSF4 et OSF5 et constitue la base d'un enregistrement robuste et sans perte de données de mesure liées au temps.


### Structure générale d'un bloc de données

Un bloc de données dans OSF se compose d'une structure d'en-tête fixe, suivie de métadonnées facultatives et des valeurs de mesure proprement dites.  
La structure est conçue de façon que chaque bloc puisse être interprété indépendamment et reste valide même en cas de streaming ou d'interruption du fichier.

**Structure de base :**

1. **Index de canal (`uint16`)**  
   - Identifie à quel canal appartiennent les données.  
   - Correspond à l'attribut `index` du bloc de métadonnées.  

2. **Champ de longueur (`uint16` ou `uint32`)**  
   - Taille, en octets, du contenu du bloc qui suit (octet de contrôle et zone de données ; au niveau d'intégrité OSF5 `crc`, en plus la CRC de trame finale).  
   - La longueur du champ est définie par le paramètre de canal `sizeoflengthvalue`.  
   - Permet d'ignorer des blocs ou, en cas d'erreur, de sauter correctement à l'unité suivante.  

3. **Octet de contrôle (`uint8`)**  
   - Définit le type du bloc de données et contient des informations sur la structure des données suivantes.  
   - Le bit de poids fort (bit 7) indique si le bloc contient **une valeur unique** (0) ou **plusieurs valeurs/paires de valeurs** (1).  
   - Une vue d'ensemble complète des valeurs de l'octet de contrôle figure dans la section *L'octet de contrôle*.  

4. **Zone de données**  
   - Les valeurs de mesure ou blocs de données proprement dits.  
   - Le format et la taille dépendent du type de canal (généralement scalar) et du type de données.
<br/>

<a name="zero-length-data-blocks"></a>
#### Blocs de données de longueur zéro (non conformes) {#zero-length-data-blocks}

Cette règle s'applique de la même manière à OSF4 et OSF5 — la structure des
blocs est identique dans les deux versions du format.

Chaque bloc de données porte au minimum son octet de contrôle ; un champ de
longueur lu littéralement dans le flux et valant `0` n'apparaît donc jamais dans
un fichier conforme. Au niveau d'intégrité OSF5 `crc`, il n'est jamais inférieur
à `5`, puisque les quatre octets de la CRC de trame sont comptés dans le champ de
longueur (voir
[Profil d'intégrité OSF5](references/osf5_integrity.md)).

- **Les écrivains NE DOIVENT PAS** écrire de bloc de données avec un champ de longueur `0`.
- **Les lecteurs NE DOIVENT PAS traiter un champ de longueur lu littéralement
  comme `0` comme un fichier tronqué, ni interrompre la lecture.** Le
  bloc se compose exclusivement de l'index de canal et du champ de longueur, que
  le lecteur a déjà tous deux consommés. Le lecteur ignore le bloc, le compte
  comme bloc ignoré avec le motif `ZeroLengthBlock` et poursuit la lecture à
  l'index de canal suivant.
- **Le test `0` intervient avant le test de longueur CRC à tous les niveaux
  d'intégrité.** Un champ de longueur de `0` est toujours classé comme
  `ZeroLengthBlock`, jamais comme erreur de CRC. Une longueur de `1`–`4` au
  niveau `crc` est un bloc défectueux et reste une question de CRC, et non un
  bloc nul.
- **L'anomalie DOIT être visible via un compteur dédié dans les statistiques
  du lecteur**, afin qu'un fichier non conforme puisse être diagnostiqué au lieu
  d'être toléré en silence.

Le lecteur progresse toujours : chaque bloc de ce type consomme 4 ou 6 octets
(un index de canal `uint16` plus un champ de longueur de 2 ou 4 octets) ; une
suite de blocs nuls ne peut donc pas le bloquer.

<br/>

### L'octet de contrôle

Chaque bloc de données dans OSF contient un **octet de contrôle (`blockContent`)** qui détermine le type du bloc et la structure des données qu'il contient.  
De plus, le bit de poids fort (bit 7) indique si le bloc contient **une seule valeur** ou **plusieurs valeurs ou paires de valeurs** :

- **Bit 7 = 0** → une valeur unique ou une paire de valeurs dans le bloc.  
- **Bit 7 = 1** → plusieurs valeurs ou paires de valeurs dans le bloc (N > 1).

L'octet de contrôle est interprété comme une valeur de 8 bits. Les 7 bits inférieurs définissent le **type de bloc**, le bit supérieur le nombre de valeurs.

<br/>

<a name="overview-of-block-types"></a>
#### Vue d'ensemble des types de blocs {#overview-of-block-types}

| Valeur (0–8) | Enum                | Signification                                                                                  | Contenu du bloc de données |
|------------|--------------------|-------------------------------------------------------------------------------------------|-------------------|
| **0**      | `bcReserved`       | Réservé à un usage futur. À l'origine *bcMetaData*, non utilisé jusqu'à présent.        | Variable, fonctions spéciales internes |
| **1**      | `bcTrustedTimestamp` | `Entfällt` Prévu à l'origine pour des valeurs constantes avec un horodatage « valide jusqu'à ». Recommandation : définir les points d'appui par l'application. | `int64` : horodatage absolu (ns depuis l'epoch) |
| **2**      | `bcTimebaseRealign` | `Entfällt`Ajustement de l'axe temporel. Peut, si nécessaire, être remplacé par l'écriture d'un nouveau bloc avec un instant de départ absolu. | `int64` : horodatage absolu<br/>`int64` : décalage temporel (ns) |
| **3**      | `bcStatusEvent`    | `Entfällt` Servait à transporter des informations d'état par canal. N'est plus utilisé. Contrairement à `bcMessageEvent`, sa charge utile est un mot d'état fixe et non une valeur du canal ; il n'est donc jamais décodé comme échantillon de canal. Les lecteurs DOIVENT le comptabiliser via un compteur dédié, distinct du compteur collectif général des blocs obsolètes, afin qu'une occurrence sur le terrain reste visible. | `int64` : horodatage absolu<br/>`uint32` : mot d'état |
| **4**      | `bcMessageEvent`   | `Entfällt`, `muss gelesen werden` Est toujours généré par les appareils en service sur le terrain pour les canaux `string` dans OSF4 — un encodage admissible dans ce cas, car la spécification exclut seulement que ce type soit encore généré à partir d'OSF5. Peut être entièrement remplacé par `bcAbsTimeStampData` avec le même `datatype`. Les lecteurs DOIVENT le décoder comme un échantillon horodaté du `datatype` du canal ; les écrivains NE DOIVENT PAS le générer. **Le bit 7 n'est pas spécifié pour ce type de bloc et NE DOIT PAS être interprété :** un lecteur qui le trouve positionné traite le bloc comme inconnu, l'ignore grâce au champ de longueur, le compte et poursuit la lecture (structure complète du bloc et cas limites ci-dessous). | `int64` : horodatage absolu<br/>`uint32` : longueur de la charge utile N<br/>`N` octets de charge utile, interprétés selon le `datatype` du canal — défini uniquement pour `string` et `binary` (voir ci-dessous). **Pas de `0x00` final** — la charge utile est préfixée par sa longueur ; la règle de terminaison par octet nul d'OSF4 pour `bcAbsTimeStampData` ne s'applique donc **pas**. |
| **5**      | `bcContinuedData`  | Poursuite des données à fréquence d'échantillonnage fixe. Si le bit 7 est positionné, plusieurs valeurs dans le bloc.        | `[uint32 N]` : nombre d'échantillons (uniquement si le bit 7 est positionné)<br/>`N` × valeurs de données |
| **6**      | `bcStartData`      | Premier bloc de données à fréquence d'échantillonnage fixe ; porte en plus la fréquence d'échantillonnage valide à partir de ce bloc (par ex. en cas de déclenchement). Contient toujours un horodatage de départ absolu. | `int64` : horodatage absolu<br/>`double` : fréquence d'échantillonnage (Hz)<br/>`[uint32 N]` : nombre d'échantillons (uniquement si le bit 7 est positionné)<br/>`N` × valeurs de données |
| **7**      | `bcContinuedRelStampData` | `Entfällt`, `In OSF5 beim Lesen unterstützt` Destiné à l'origine à économiser 4 octets par échantillon grâce à des horodatages relatifs. | `[uint32 N]` : nombre d'échantillons (uniquement si le bit 7 est positionné)<br/>`N` × (`uint32` temps relatif + valeur de données) |
| **8**      | `bcAbsTimeStampData` | Blocs de données avec horodatage absolu par valeur. Prend désormais aussi en charge les chaînes et les données binaires en liaison avec `datatype` et `mimetype`. | `[uint32 N]` : nombre d'échantillons (uniquement si le bit 7 est positionné)<br/>`N` × (`int64` temps absolu + valeur de données) |


Restriction des types de blocs au regard de l'information de canal :

| Type ENUM      | Données équidistantes | Données horodatées |
| ------------- |-------------| -----|
| bcStartData            | autorisé | non autorisé |
| bcContinuedData        | autorisé | non autorisé |
| bcContinuedRelStampData | non autorisé | autorisé|
| bcAbsTimeStampData | non autorisé | autorisé|
| bcMessageEvent | non autorisé | autorisé|

<br/>
### Structure des données selon le type de contrôle

La structure des données utiles dans un bloc dépend directement du type de contrôle.  
Les sections suivantes décrivent comment les valeurs sont enregistrées pour les différents types de données et quelles restrictions s'appliquent.

#### bcStartData (données équidistantes, bloc de départ)

- **Utilisation :**  
  - Début d'une série de données à fréquence d'échantillonnage fixe.  
  - Contient toujours l'instant de départ absolu de la série.  
  - Autorisé uniquement pour les `datatype` numériques (`int*`, `float`, `double`).  

- **Structure du bloc :**  
  1. `int64` – horodatage de départ absolu (ns depuis l'epoch).  
  2. `double` – fréquence d'échantillonnage en Hz (valide à partir de ce bloc, jusqu'au prochain `bcStartData`).  
  3. **[uint32 N]** – nombre d'échantillons (uniquement si le bit 7 est positionné, sinon 1).  
  4. **N × valeurs de données** – données brutes selon `datatype`.  

- **Exemple `datatype=double` :** [int64 ZeitStart] [double SampleRate] [uint32 N] [double Wert1] [double Wert2] ... [double WertN]


- **Remarques :**
  - `bcStartData` peut apparaître **plusieurs fois par fichier et par canal**. Il est écrit :
    - au début d'un enregistrement équidistant,
    - à chaque déclenchement ou événement qui initie une nouvelle séquence de données,
    - lors d'une correction nécessaire de la trace temporelle (compensation de dérive).
  - **Conséquence pour les lecteurs :** les données d'un canal équidistant sont produites **par blocs ou par événements**. Entre des séquences successives du même canal, il peut y avoir des **lacunes temporelles**. Les lecteurs doivent reprendre la fréquence d'échantillonnage effective du bloc `bcStartData` actuellement valide et **ne doivent pas** supposer que le `timeincrement` du bloc de métadonnées est toujours correct.

#### bcContinuedData (données équidistantes, poursuite)

- **Utilisation :**  
  - Poursuit, sans nouvel horodatage, une série commencée avec `bcStartData`.  
  - La première valeur se raccorde directement à la dernière valeur du bloc précédent.  
  - Autorisé uniquement pour les `datatype` numériques (`int*`, `float`, `double`).  

- **Structure du bloc :**  
    1. **[uint32 N]** – nombre d'échantillons (uniquement si le bit 7 est positionné, sinon 1).  
    2. **N × valeurs de données** – données brutes selon `datatype`.  

- **Exemple `datatype=int16` :**[uint32 N] [int16 Wert1] [int16 Wert2] ... [int16 WertN]

- **Remarque :** le temps par échantillon dans un bloc `bcContinuedData` résulte de `1 / SampleRate` du dernier bloc `bcStartData` lu pour le même canal.



#### bcAbsTimeStampData (données horodatées)

- **Utilisation :**  
  - Pour les canaux avec des horodatages individuels par valeur.  
  - Prend en charge tous les `datatype`, y compris `string` et `binary`.

- **Structure du bloc :**  
  1. **[uint32 N]** – nombre d'échantillons (uniquement si le bit 7 est positionné, sinon 1).  
  2. **N × (int64 temps + valeur de données)** – horodatage absolu + valeur.  

- **Exemple `datatype=int16` :**[uint32 N] [int64 Zeit1] [int16 Wert1] [int64 Zeit2] [int16 Wert2] ...
- **Exemple `datatype=double` :**[uint32 N] [int64 Zeit1] [double Wert1] [int64 Zeit2] [double Wert2] ...
- **Exemple `datatype=string` :**
  - Les chaînes sont enregistrées sous forme d'octets UTF-8 bruts. La longueur effective de la chaîne résulte de la longueur utile du champ de données, diminuée d'un octet dans OSF4 (le `0x00` final prescrit par la spécification) ou de la longueur utile complète dans OSF5 (voir le bloc de remarque ci-dessus pour les règles complètes).
  - Forme à échantillon unique (bit 7 = 0, N implicitement 1) : [int64 Zeit] [UTF-8 Bytes des Strings]
  - La forme à plusieurs échantillons (bit 7 = 1) pour les types de longueur variable ne fait pas partie du format de flux standard ; voir la remarque sur les blocs à plusieurs échantillons pour les longueurs variables ci-dessous.

- **Exemple `datatype=binary` :**
  - Les données binaires sont écrites sous forme d'octets bruts. Le `mimetype` dans le canal définit l'interprétation. La longueur effective de la charge utile est la longueur utile du champ de données, diminuée d'un octet dans OSF4 (le `0x00` final prescrit par la spécification) ou la longueur utile complète dans OSF5 (voir le bloc de remarque ci-dessus pour les règles complètes).
  - Forme à échantillon unique (bit 7 = 0, N implicitement 1) : [int64 Zeit] [Byte1] [Byte2] ... [Byte M]
  - La forme à plusieurs échantillons (bit 7 = 1) pour les types de longueur variable ne fait pas partie du format de flux standard ; voir la remarque sur les blocs à plusieurs échantillons pour les longueurs variables ci-dessous.


> **Blocs à plusieurs échantillons pour les longueurs variables.** Pour les données `string` et `binary` dans `bcAbsTimeStampData`, les écrivains doivent émettre un échantillon par bloc (N=1). La forme à plusieurs échantillons (bit 7 = 1 avec N > 1) pour les types de longueur variable ne fait pas partie du format de flux standard. Les lecteurs peuvent rencontrer des blocs à plusieurs échantillons de longueur variable provenant d'écrivains plus anciens ou non conformes au standard ; le comportement du lecteur est dans ce cas spécifique à l'implémentation. Les lecteurs de référence Rust et C++ n'acceptent que des dispositions avec des segments de même longueur par échantillon ; l'écrivain Delphi historique utilise un préfixe de longueur `uint32` par échantillon, que les autres lecteurs ne savent pas analyser.

- **Remarque :** avec plusieurs échantillons par bloc (`N>1`), les types numériques doivent avoir une taille fixe par échantillon dans le flux. Les blocs à plusieurs échantillons pour les longueurs variables ne sont pas standard ; voir la remarque sur les blocs à plusieurs échantillons pour les longueurs variables ci-dessus.



#### bcContinuedRelStampData (horodaté, relatif)

- **Utilisation :**  
  - Pour les canaux avec des horodatages individuels et des écarts relatifs.  
  - N'est **plus utilisé à partir d'OSF5**, reste conservé pour les lecteurs OSF4.

- **Structure du bloc :**  
  1. **[uint32 N]** – nombre d'échantillons (uniquement si le bit 7 est positionné, sinon 1).  
  2. **N × (uint32 Δt + valeur de données)** – écart de temps relatif en ns + valeur.  

- **Exemple `datatype=int16` :**[uint32 N] [uint32 Δt1] [int16 Wert1] [uint32 Δt2] [int16 Wert2] ...
- Autres exemples sous bcAbsTimeStampData


- **Remarque :**  
- Développé à l'origine pour économiser 4 octets par échantillon.  
- Dans OSF5, ce type est supprimé au profit d'une implémentation plus simple.



#### bcMessageEvent (obsolète, doit être lu) {#bcmessageevent}

- **Utilisation :**  
  - Encodage historique d'une valeur `string` ou `binary` horodatée ; peut être entièrement remplacé par `bcAbsTimeStampData` avec le même `datatype`.  
  - Est toujours généré par les appareils en service sur le terrain pour les canaux `string` dans OSF4 — un encodage admissible dans ce cas, car la spécification exclut seulement que ce type soit encore généré à partir d'OSF5. Les lecteurs DOIVENT le décoder dans **toute** version du format, car des fichiers contenant ce bloc existent sur le terrain. Les écrivains NE DOIVENT PAS le générer.

- **Structure du bloc :**  
  1. `int64` – horodatage absolu (ns depuis l'epoch).  
  2. `uint32` – longueur de la charge utile N (octets).  
  3. `N` octets de charge utile, interprétés selon le `datatype` du canal.

- **Exemple `datatype=string` :** [int64 Zeit] [uint32 N] [N Bytes UTF-8-Nutzlast]

- **Pas de `0x00` final.** La charge utile est préfixée par sa longueur via `N` ; la règle de terminaison par octet nul d'OSF4 pour `bcAbsTimeStampData` (voir le bloc de remarque sur le traitement de l'octet nul) ne s'applique donc **pas** ici — il n'y a jamais eu d'octet à supprimer. Seule est réutilisée l'*interprétation de la valeur* des charges utiles `string`/`binary` de `bcAbsTimeStampData`, et non leur découpage en trames.

- **Portée de `datatype`.** Seuls `datatype=string` et `datatype=binary` sont définis pour ce type de bloc. Pour tout autre `datatype`, les lecteurs DOIVENT ignorer le bloc grâce à son champ de longueur et le compter — ils NE DOIVENT PAS déclencher d'erreur ni interrompre le fichier. Ignorer le bloc maintient le reste d'un enregistrement réel lisible ; une interruption réintroduirait ailleurs le même type d'erreur que la [règle sur les blocs nuls](#zero-length-data-blocks) a éliminé.

- **`N = 0` est admissible** et se décode en une valeur vide (chaîne vide ou charge utile binaire de longueur 0) – pas une erreur. Ce n'est **pas** l'anomalie des [blocs de données de longueur zéro](#zero-length-data-blocks) : là, c'est le *champ de longueur* lui-même qui vaut `0`, et aucun octet de contrôle n'est jamais lu ; ici, le champ de longueur correspond à la taille réelle de la trame (`1 + 8 + 4 + N` octets), seule la charge utile se trouve être vide.

- **Le bit 7 (valeurs multiples) n'est pas spécifié pour ce type de bloc.** Il n'a jamais été observé positionné sur le terrain, et aucune disposition à plusieurs échantillons n'est définie pour ce type de bloc. Un lecteur qui le trouve positionné NE DOIT PAS l'interpréter : il traite le bloc comme un type inconnu, l'ignore grâce au champ de longueur, le compte et poursuit la lecture — le même traitement prudent que pour toute autre forme non reconnue.



#### Restrictions

- **Canaux équidistants (bcStartData, bcContinuedData) :**
  - Uniquement des types de données numériques directs (`int*`, `float`, `double`).  
  - Pas de chaînes, pas de données binaires, pas de structures complexes.  

- **Canaux horodatés (bcAbsTimeStampData, bcContinuedRelStampData) :**
  - Prennent en charge tous les types de données.
  - Les chaînes et les données binaires contiennent, dans OSF4, un octet `0x00` final (supprimé par les lecteurs) et, dans OSF5, aucun octet final ; voir le bloc de remarque sur le traitement de l'octet nul pour les règles déterministes.

#### Points importants

- **Compatibilité :**  
  - OSF5 peut lire tous les types de blocs OSF4.  
  - À partir d'OSF5, `bcContinuedRelStampData`, `bcStatusEvent` et `bcMessageEvent` ne sont plus générés. Ne plus être généré ne signifie pas ne plus être lu : les lecteurs doivent continuer à prendre en charge `bcContinuedRelStampData` et `bcMessageEvent` **dans toutes les versions**, car des fichiers contenant ces blocs existent sur le terrain.  
  - `bcTrustedTimestamp` est ignoré et est signalé comme *deprecated*.

- **Implémentation :**  
  - Les lecteurs doivent toujours vérifier le bit 7 afin d'interpréter correctement les blocs à valeur unique et à valeurs multiples.  
  - Les types de blocs non reconnus peuvent être ignorés grâce à l'indication de longueur.

- **Chaînes et données binaires :**
  - Pour `bcAbsTimeStampData` avec `datatype=string` ou `datatype=binary`, l'octet nul final (`0x00`) est **toujours présent dans OSF4** (l'écrivain doit l'ajouter, le lecteur doit le supprimer) et **jamais présent dans OSF5** (l'écrivain ne doit pas l'ajouter, le lecteur ne doit pas le supprimer). Voir le bloc de remarque sur le traitement de l'octet nul pour les règles complètes.
  - La longueur du bloc résulte de `sizeoflengthvalue`.
  - Les données binaires utilisent `datatype=binary` plus `mimetype`.
<br/>



## Fin de fichier et trailer magique

À la fin d'un fichier OSF, un **bloc de données d'informations** portant l'index de canal spécial `0xFFFF` peut être écrit en option.  
Ce bloc fournit des métainformations sur le flux de données terminé et marque la fin régulière du fichier.  

### Bloc de données d'informations (index de canal 0xFFFF)

- **Objet :**  
  Fournit un aperçu rapide de l'intervalle de temps et de la segmentation du fichier, sans avoir à lire l'ensemble des blocs de données.  
  Utile pour les outils d'analyse et d'indexation.

- **Structure :**  
    1. `uint16` – index de canal (`0xFFFF`)  
    2. `uint32` – longueur du bloc d'options qui suit  
    3. `uint8` – octet de contrôle (toujours `bcReserved` / 0)  
    4. `string` – bloc d'informations encodé en UTF-8 (format dépendant de la version d'OSF)

#### Exemple (OSF4, XML) :

```xml
<trailer finalized_utc="2019-08-12T12:23:01+02:00" reason="fileStartGrid_min">
    <channels count="8">
        <channel index="0" samples="29452" last_ns="1384899599997800000" last_utc="2019-11-19T23:19:59"/>
        <channel index="1" samples="29452" last_ns="1384899599997800000" last_utc="2019-11-19T23:19:59"/>
        <channel index="2" samples="29452" last_ns="1384899599997800000" last_utc="2019-11-19T23:19:59"/>
        <channel index="3" samples="29452" last_ns="1384899599997800000" last_utc="2019-11-19T23:19:59"/>
    </channels>
</trailer>
````

#### Exemple (OSF5, JSON) :

```json
{
  "trailer": {
    "finalized_utc": "2019-08-12T12:23:01+02:00",
    "reason": "fileStartGrid_min",
    "channels": [
      {
        "index": 0,
        "samples": 29452,
        "last_ns": 1384899599997800000,
        "last_utc": "2019-11-19T23:19:59"
      },
      {
        "index": 1,
        "samples": 29452,
        "last_ns": 1384899599997800000,
        "last_utc": "2019-11-19T23:19:59"
      }
    ]
  }
}
```

* **Remarque :**

  * OSF4 utilise XML par défaut pour le bloc d'informations.
  * OSF5 utilise JSON, mais peut, pour des raisons de compatibilité, lire XML également, sans toutefois l'écrire.

<br/>

### Trailer magique

Un **trailer magique** peut, en option, suivre le bloc de données d'informations.
Il sert de marque fixe pour la fin du fichier et indique où commence le bloc `0xFFFF`.

* **Format :**

```
OSF_STREAM_END 321316454==============
```

* Le nombre indique la position dans le fichier à laquelle commence le bloc `0xFFFF`.
* L'étiquette du trailer est complétée à **40 octets** : après le nombre suivent des caractères `=` jusqu'à ce que la longueur soit atteinte.

#### Objet du trailer magique

* Permet de trouver le bloc de données d'informations en fin de fichier sans parcourir l'ensemble du fichier.
* Facilite les implémentations à accès aléatoire et l'indexation de fichiers volumineux.
* Offre une marque claire pour la fin régulière d'un fichier OSF.

<br/>


### Caractère facultatif et effort d'implémentation

* **Écriture :**

  * Le bloc d'informations et le trailer magique ne sont pas strictement obligatoires.
  * S'ils sont écrits, ils permettent une indexation rapide et la détermination de l'intervalle de temps du fichier.
  * Dans les systèmes embarqués aux ressources limitées, ils peuvent être omis.

* **Lecture :**

  * Les analyseurs ne doivent pas supposer la présence du bloc.
  * Les fichiers sans trailer sont interprétés jusqu'au dernier bloc entièrement lisible.
  * En cas d'interruption brutale, le dernier bloc peut être plus court que son indication de longueur – dans ce cas, le lecteur doit s'arrêter à la fin du fichier.

<br/>


**Avantages**

* Détermination rapide de l'intervalle de temps et des statistiques sans lecture complète du fichier.
* Utile pour les longues mesures et l'analyse automatisée.
* Permet l'accès aléatoire pour les outils d'analyse.

**Inconvénients**

* Effort d'implémentation accru pour l'écriture et la lecture.
* En cas d'interruption du fichier, le trailer peut manquer ou être incomplet.
* Pour un streaming simple, il n'est pas strictement nécessaire.

<br/>

## OSFZ — Fichiers OSF compressés

Les fichiers OSF peuvent être compressés pour le stockage ou la
transmission. Les fichiers compressés portent généralement l'extension
`.osfz` et contiennent un fichier OSF complet (OSF4 ou OSF5)
en tant que charge utile compressée. Il n'existe pas d'en-tête magique OSFZ propre
— la détection s'effectue à l'aide des octets magiques de compression au début
du fichier.

### Formats de compression pris en charge

Les lecteurs doivent détecter et décompresser de façon transparente les deux
formats de compression courants :

| Format | Octets magiques                                        | Spécification |
|--------|----------------------------------------------------|---------------|
| gzip   | `0x1F 0x8B`                                        | RFC 1952      |
| zlib   | `0x78 0x01`, `0x78 0x5E`, `0x78 0x9C`, `0x78 0xDA` | RFC 1950      |

Les deux formats sont valides en pratique : les appareils Optimeas écrivent
actuellement des fichiers OSFZ compressés en gzip ; d'anciens outils et
pipelines de stockage utilisent zlib. Une implémentation qui ne prendrait en
charge qu'un seul des deux formats ne pourrait pas lire de véritables fichiers
de terrain.

### Détection

La détection s'effectue à partir des deux premiers octets du fichier :

* `0x1F 0x8B` → décompression gzip
* `0x78 0x01 / 0x5E / 0x9C / 0xDA` → décompression zlib
* sinon → non compressé, lire directement le fichier comme OSF

Après la décompression, le fichier commence par un en-tête magique OSF
régulier (`OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4` ou
`OCEAN_STREAMING_FORMAT4`).

### Écriture

Les écrivains peuvent produire des sorties OSFZ. Le format de compression
utilisé est alors **gzip (RFC 1952)**.

Les **écrivains en streaming** (qui écrivent les blocs de façon incrémentale
avec un `fsync` par bloc) doivent traiter la compression comme une **étape
postérieure à la finalisation**, détachée du chemin d'écriture proprement dit.
Une compression en ligne rendrait la décompression impossible après une coupure
de courant suivie d'une troncature et anéantirait la durabilité best-effort.
L'étape de compression s'exécute après la fermeture de l'écrivain —
soit comme thread d'arrière-plan à faible priorité (le processus ne peut se
terminer qu'une fois celui-ci achevé), soit comme processus CLI externe. Le
fichier OSF source doit être conservé jusqu'à ce que le fichier OSFZ ait été
écrit avec succès et que son `fsync` ait été effectué ; une vérification par
relecture n'est pas nécessaire avant la suppression.

Les **écrivains par blocs** (qui mettent en mémoire tampon l'ensemble du fichier
et l'écrivent de façon atomique) peuvent écrire directement de manière
compressée et produire des fichiers OSFZ en ligne, car il n'y a aucun risque de
sortie partielle ni d'interruption due à une coupure de courant.

<br/>

## Étapes suivantes

Le chapitre précédent décrit la structure générale de l'**Open Streaming Format (OSF)** et tous les composants qui s'appliquent **de la même manière à OSF4 et OSF5**.  
Pour une implémentation complète ou une intégration plus poussée, les sujets complémentaires suivants se prêtent à une lecture ultérieure :

- **Spécificités d'OSF4 et d'OSF5 :**  
  - Détails sur les formats d'en-tête respectifs (XML vs JSON)  
  - Différences concernant l'octet de contrôle et le trailer  
  - Rétrocompatibilité et remarques d'implémentation
  - Poursuivre avec [OSF4](references/osf4.md) ou [OSF5](references/osf5.md)

- **Vecteurs et matrices :**  
  - Types de canaux étendus pour les données multidimensionnelles  
  - Paramètres supplémentaires pour les axes, les dimensions et les unités physiques  
  - Exemples pour les FFT, les classifications et les données d'image  

- **Exemples :**  
  - Fichiers OSF complets (OSF4/XML et OSF5/JSON) avec en-tête, bloc de métadonnées et blocs de données  
  - Vidages hexadécimaux et structures commentées  

- **Accès au code source et à l'open source :**  
  - Implémentations de référence pour OSF4 et OSF5  
  - Bibliothèques d'analyse et d'écriture pour différentes plateformes  
  - Exemples de code pour les systèmes embarqués et l'analyse sur PC

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
