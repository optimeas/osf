---
title: Profil d'intégrité OSF5
description: Spécification normative du profil d'intégrité optionnel d'OSF5 — détection de corruption par CRC32C, chaîne de signatures Ed25519 et preuve d'origine X.509/PKI
image: "/img/om_social_card.png"
keywords:
  - OSF5
  - Intégrité
  - CRC32C
  - Ed25519
  - Signature
  - PKI
last_update:
  date: 2026-07-08
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Profil d'intégrité OSF5

Ce document est la spécification **normative** du **profil d'intégrité**
optionnel de l'Open Streaming Format version 5 (OSF5). Il complète le format de
base par trois garanties échelonnées : détection de corruption bloc par bloc
(CRC32C), détection de manipulation et preuve d'origine compatibles avec le
streaming (une chaîne de hachage SHA-256 avec des ancres de signature Ed25519
périodiques), ainsi qu'une vérifiabilité hors ligne par des tiers (un modèle de
certificats X.509/PKI).

Le profil est une **propriété propre à OSF5**. Les fichiers OSF4 ne sont pas
concernés et n'en comportent jamais aucun élément. La conception est entièrement
rétrocompatible : les fichiers OSF5 *sans* déclaration d'intégrité restent
valides sans modification.

:::info Document conceptuel
La justification de conception de ce profil — l'échelle à trois niveaux, la
sémantique fail-closed des jetons, la chaîne de hachage avec ancres de signature
et le modèle de confiance PKI — figure dans le document conceptuel *Intégrité et
signature dans les formats de données de mesure en streaming : l'approche
OSF5*, publié sur Zenodo en **allemand et en anglais** (DOI
[10.5281/zenodo.21227941](https://doi.org/10.5281/zenodo.21227941), CC BY 4.0)
et dans ce dépôt sous
[`docs/papers/`](https://github.com/optimeas/osf/tree/main/docs/papers/).
:::

Le document fournit le contexte ; **le présent document est la référence
normative** pour les implémenteurs.

Le format de base (en-tête magique, bloc de métadonnées JSON, blocs de données
autonomes) est décrit dans [`osf_general.md`](../osf_general.md) et
[`osf5.md`](osf5.md) ; le présent document ne complète que la couche
d'intégrité.

---

## 1. Modèle à trois niveaux

Le profil est une **échelle strictement ordonnée** ; chaque niveau contient
celui qui lui est inférieur :

```
none  ⊂  crc  ⊂  signed
```

Le niveau s'applique **par fichier** et est déclaré **exactement une fois**.

| Niveau | Protège contre | Usage courant |
|---|---|---|
| **none** | — | Laboratoire, fichiers intermédiaires, écrivains embarqués minimaux |
| **crc** | Corruption (erreurs de bits, troncature, défauts de support) | Standard pour les données de terrain |
| **signed** | Corruption **et** manipulation ; origine prouvable pour des tiers | Données d'exploitation ayant valeur de preuve |

Deux règles de conception évitent une fragmentation du format :

1. **Les écrivains choisissent librement le niveau** — les systèmes embarqués
   conservent la possibilité d'un écrivain minimal.
2. **Tout lecteur OSF5 conforme DOIT traiter tous les niveaux.** Il en résulte un
   seul format avec profil, et non trois dialectes.

La granularité est toujours le **fichier entier** : soit tous les blocs portent
le mécanisme, soit aucun.

---

## 2. Grammaire du jeton d'en-tête (normative)

La ligne d'en-tête magique est étendue, après la longueur du bloc de
métadonnées, par zéro ou plusieurs jetons optionnels :

```abnf
header-line     = identifier SP metablock-len *(SP token) LF
token           = key ":" value
key             = 1*(a-z / 0-9 / "-")          ; nur Kleinbuchstaben
value           = 1*(VCHAR ohne SP)
```

- Exactement **un** `SP` (0x20) sépare les champs ; il n'y a **aucun espace
  final** avant le `LF`.
- Les jetons sont **« must understand »** : un lecteur qui ne connaît pas une
  `key` **DOIT rejeter le fichier**, avec un diagnostic de la forme
  `unbekanntes Header-Token '<key>'` — **et non** comme une erreur d'analyse
  numérique. (Les analyseurs actuels ne rejettent un jeton inattendu que
  de façon incidente, via un message trompeur « la longueur n'est pas un nombre
  valide » ; voir l'annexe de migration.)
- Les jetons d'en-tête sont une caractéristique **propre à OSF5**. Les
  identifiants OSF4 (`OSF4`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`)
  NE DOIVENT porter aucun jeton ; un jeton après un identifiant OSF4 constitue un
  fichier défectueux.

### Clés définies dans cette révision

| Clé | Niveau | Valeur |
|---|---|---|
| `crc32c` | crc | `crc32c:<8 HEXDIG majuscules>` — la CRC32C (Castagnoli) calculée sur les **octets bruts du bloc de métadonnées**, exactement tels qu'ils figurent dans le fichier |
| `ed25519` | signed | `ed25519:<keyid>` — `keyid` = 16 HEXDIG en **minuscules** = les 8 premiers octets du SHA-256 de la `SubjectPublicKeyInfo` encodée en DER du certificat de l'appareil |

- **La casse hexadécimale est stricte** : `crc32c` utilise de l'hexadécimal en
  **majuscules** ; le `keyid` de `ed25519` utilise de l'hexadécimal en
  **minuscules**. Les lecteurs DOIVENT vérifier strictement la casse.
- Le jeton `ed25519` n'est autorisé qu'**en plus de** `crc32c` et uniquement dans
  cet ordre — `crc32c` d'abord, `ed25519` ensuite. `signed` implique donc
  toujours `crc`.
- Le bloc de métadonnées est la seule partie du fichier qui ne peut pas se
  protéger elle-même ; le jeton `crc32c` porte sa somme de contrôle afin que
  l'ordre complet de vérification soit déterministe.

### Ordre de vérification (normatif)

1. Lire les jetons d'en-tête.
2. Vérifier la CRC du bloc de métadonnées (issue du jeton `crc32c`).
3. Analyser le bloc de métadonnées.
4. Vérifier les blocs de données (CRC de trame et — au niveau signed — la chaîne de signatures).

---

## 3. CRC de trame (niveau crc)

Au niveau crc, **chaque** bloc porte une **CRC32C** (Castagnoli ;
accélérée matériellement sur x86/SSE4.2 et ARMv8) calculée sur la **trame
entière** : index de canal, champ de longueur, octet de contrôle et données utiles.

Inclure délibérément le **champ de longueur** dans le périmètre est important :
un champ de longueur altéré est l'erreur unique la plus grave, car le lecteur
perd ensuite toutes les limites de blocs.

La CRC constitue les **quatre derniers octets de la zone de données** et est
**comptée dans le champ de longueur**, de sorte que le découpage en trames reste
intact pour tout lecteur. La longueur utile effective est la longueur du bloc
**moins quatre**.

```
ohne Profil:   [Kanalindex][Längenfeld][Steuerbyte][ Nutzdaten ................ ]
                                       |<------------- LEN ------------------>|

mit  Profil:   [Kanalindex][Längenfeld][Steuerbyte][ Nutzdaten ....... ][CRC32C]
                                       |<------------- LEN ------------------>|
               |<================ Scope der Frame-CRC ====================>|
```

### Exigence d'implémentation normative (découpage en trames fail-closed)

Lorsque le profil est actif, la CRC de trame **fait partie du découpage en
trames** et DOIT être détachée **avant** l'exploitation typée des données utiles.
Une vérification « reste == 0 » *en aval*, après le décodage, **ne suffit pas** :
pour les types de données de longueur variable (`string`, `binary`), un lecteur
ignorant l'intégrité ne peut pas distinguer les octets de CRC ajoutés de la
véritable charge utile et livrerait des valeurs corrompues. Le mode fail-open est
ainsi exclu par construction — le lecteur détache les quatre derniers octets sur
la base de sa connaissance du profil issue de l'en-tête/du bloc de métadonnées, et
les vérifie.

### Comportement en cas d'erreur

| Condition | Réaction |
|---|---|
| Erreur de CRC d'un bloc de données | le bloc est **invalide** → l'ignorer, **le compter dans les statistiques de lecture** et poursuivre (des données partielles valent mieux que pas de données) |
| Erreur de CRC du bloc de métadonnées | **rejeter le fichier** (sans bloc de métadonnées fiable, rien n'est interprétable) |
| Jeton d'en-tête inconnu | **rejeter le fichier** (voir §2) |

**Recommandation aux implémentations :** en plus du détachement obligatoire,
effectuer une vérification stricte de consommation complète pour les blocs
**numériques** (`N × taille d'échantillon (+ champs d'en-tête)` doit
correspondre à la longueur utile effective). Il s'agit d'une amélioration de la
qualité du diagnostic, et non d'une exigence de correction.

---

## 4. Bloc de signature `bcIntegritySignature = 9` (niveau signed)

Le niveau signed ajoute un nouveau type d'octet de contrôle et une chaîne de
hachage continue.

### Le bloc

- Nouvelle valeur d'octet de contrôle **9** (`bcIntegritySignature`) ; valide
  uniquement si le fichier déclare le niveau **signed** ; **bit 7 = 0**
  (sémantique de valeur unique).
- **Index de canal : la valeur réservée `0xFFFE`** — un canal d'intégrité à
  l'échelle du fichier, qui n'est **pas** déclaré dans le bloc de métadonnées.
  Les lecteurs sans prise en charge du niveau signed ignorent le bloc grâce au
  champ de longueur, exactement comme tout autre type de bloc inconnu (voir §5).
  `0xFFFE` est distinct du canal d'informations/trailer `0xFFFF` d'OSF4.
- Les blocs sur le canal réservé `0xFFFE` utilisent **toujours un champ de
  longueur de 4 octets (`uint32`)**, indépendamment des déclarations de canaux —
  par analogie avec le bloc d'informations historique `0xFFFF`.
- Les blocs de signature portent eux-mêmes une **CRC de trame**, comme tout autre bloc.

### Charge utile (little-endian ; ordre normatif)

| # | Champ | Type | Signification |
|---|---|---|---|
| 1 | `anchor_seq` | `uint32` | Numéro de séquence d'ancre, consécutif à partir de 0 |
| 2 | `signing_time_ns` | `int64` | Instant de signature (base de la sémantique de validité) |
| 3 | `chain_hash` | `byte[32]` | H(i), le hachage de chaîne courant |
| 4 | `signature` | `byte[64]` | Ed25519 sur `SHA-256(anchor_seq ‖ signing_time_ns ‖ chain_hash)`, les champs dans l'ordre et l'encodage du flux |
| 5 | `keyid_len` + `keyid` | `uint8` + `byte[keyid_len]` | Référence de clé/certificat ; `keyid` comme dans le jeton d'en-tête (8 octets) |

### Chaîne de hachage

```
H(0) = SHA256(Header-Zeile ‖ Metablock)
H(i) = SHA256(H(i−1) ‖ Frame_i)
```

- Les trames entrent dans la chaîne **y compris** leur CRC de trame.
- **Les blocs de signature eux-mêmes entrent également dans la chaîne** (en tant que `Frame_i`).
- **La première ancre couvre ainsi aussi l'en-tête et le bloc de métadonnées** via H(0).

Au-delà du contrôle de chaque bloc, la chaîne détecte aussi la **suppression**,
l'**insertion** et la **permutation** de blocs ; les numéros de séquence d'ancre
détectent la **répétition au sein du fichier**.

### Cadence

Configurable (basée sur le temps ou sur les blocs). La **valeur par défaut
normative est basée sur le temps, 10 secondes**. En outre, une ancre est
**obligatoire à la fermeture régulière du fichier**, de sorte qu'un fichier
correctement fermé soit entièrement signé.

### Sémantique de coupure d'alimentation (normative)

Si l'enregistrement s'interrompt brutalement, le fichier est **signé jusqu'à la
dernière ancre valide** ; la fin qui suit est **valide au sens CRC, mais non
signée**. Le rapport de vérification DOIT mentionner les deux (par ex. « signé
jusqu'à l'horodatage X, reste valide au sens CRC, non signé »).

---

## 5. Certificats et PKI (niveau signed)

Pour que **tout tiers** — et pas seulement le fabricant — puisse vérifier
l'origine, les clés d'appareil sont certifiées par une autorité de
certification (X.509 avec Ed25519 selon la RFC 8410), à l'image du modèle de
confiance de HTTPS, mais sous la forme d'une hiérarchie privée et publiquement
documentée : une **CA racine** hors ligne protégée matériellement, une CA
émettrice (rattachée à la production ou au cloud des appareils) et, en dessous,
des certificats d'appareil dont le sujet porte le numéro de série de l'appareil.

### Emplacement d'intégration : le bloc de métadonnées

La chaîne de certificats est intégrée une fois par fichier sous la forme d'un
nouvel **objet optionnel au niveau `osf`** du bloc de métadonnées :

```json
"integrity": {
  "certificates": ["<base64 DER Gerätezertifikat>", "<base64 DER Zwischenzertifikat>"]
}
```

- **Feuille en premier** ; la **racine n'est jamais intégrée** — l'ancre de
  confiance doit par principe provenir de l'extérieur (le certificat racine
  publié publiquement, livré avec les outils de vérification, répertorié sur le
  site web et dans le dépôt ouvert, chaque fois avec son empreinte).
- Comme l'objet se trouve dans le bloc de métadonnées, les certificats sont
  couverts par la **CRC du bloc de métadonnées** et par **H(0)**.
- **Remarque :** cet objet est une **donnée, et non une déclaration de profil.**
  La déclaration reste uniquement le jeton d'en-tête (§2). Un fichier peut porter
  un objet `integrity.certificates` et être malgré tout au niveau crc si aucun
  jeton d'en-tête `ed25519` n'est présent.

### Validité : « valide au moment de la signature »

Les données de mesure survivent à la durée de vie des certificats. La sémantique
de vérification est donc : **le certificat était-il valide au moment de la
signature (`signing_time_ns`) ?** Un fichier enregistré en 2027 reste vérifiable
positivement en 2040. Associé à de longues durées de vie des certificats
d'appareil, c'est adapté à la pratique pour des données de mesure. La faiblesse
résiduelle théorique (antidatage avec une clé d'appareil expirée et dérobée) est
documentée et peut, si nécessaire, être comblée par des **horodatages RFC 3161**,
sans modifier le format — l'horodatage se rattache à la signature, et non aux
blocs de données.

### Révocation

Les révocations s'effectuent au moyen de **listes de révocation signées** avec
une sémantique de **best effort** ; le rapport de vérification indique
explicitement l'état du contrôle de révocation.

### Classes de certificats et politique de transformation

- Les **certificats d'appareil** et les **certificats d'organisation/d'outil**
  forment des **classes séparées et distinguables.**
- **Les transformations mettent fin au domaine de signature — volontairement.**
  La fusion, la conversion et l'exportation produisent par principe des fichiers
  sans signature d'appareil. Les outils se comportent de façon définie : le
  **résultat retombe au niveau crc** et consigne son **origine** (fichiers
  sources avec leur état de vérification) dans les métadonnées (`infos`).
  Facultativement, une organisation peut **re-signer** le résultat avec son
  propre certificat — c'est une **déclaration différente, signalée comme telle**,
  par rapport à la signature de l'appareil.

---

## 6. États de vérification (vocabulaire normatif pour les outils)

Les outils de vérification signalent exactement l'un des états suivants :

| État | Signification |
|---|---|
| `valid_signed` | entièrement signé et valide |
| `partially_signed` | signé jusqu'à l'ancre X ; le reste est valide au sens CRC, mais non signé |
| `crc_valid` | le niveau CRC est respecté ; aucune signature (valide) |
| `invalid` | un contrôle de CRC ou de signature a échoué là où il doit réussir |
| `signature_unverifiable` | les signatures ne peuvent pas être vérifiées (par ex. bibliothèque cryptographique ou certificat racine manquant) — le fichier reste lisible, une déclaration au niveau CRC reste possible |

---

## 7. Positionnement par rapport à la DIN EN 50159 (informatif)

> Cette section est **informative**. La norme EN 50159 traite de la
> communication relative à la sécurité dans les systèmes de transmission de la
> signalisation ferroviaire ; la comparaison est proposée parce que le profil est
> pertinent pour les environnements ferroviaires, bien qu'il soit dérivé du Cyber
> Resilience Act et qu'il traite des **fichiers au repos** plutôt que des canaux
> de transmission.

| Menace (EN 50159) | Mécanisme dans le profil OSF5 |
|---|---|
| Corruption | CRC32C de trame par bloc ; sur le plan cryptographique : chaîne de hachage |
| Suppression | Chaîne de hachage (un bloc manquant rompt la chaîne) |
| Insertion | Chaîne de hachage et signature (un bloc étranger rompt la chaîne) |
| Permutation | Chaîne de hachage (l'ordre fait partie de la construction de la chaîne) |
| Mascarade | Signature Ed25519 avec certificat d'appareil et chaîne de CA |
| Répétition | Numéros de séquence d'ancre (au sein du fichier) ; identité du fichier via un **UUID de fichier** unique dans le bloc de métadonnées, servant d'ancre pour le contrôle au niveau système |
| Retard | non applicable aux données au repos |

Deux délimitations sont à retenir :

1. Le profil traite la **répétition et la suppression au niveau du fichier**
   (réinjection d'anciens fichiers, disparition de fichiers entiers)
   volontairement uniquement via l'**interface UUID de fichier**. L'absence de
   lacunes au-delà des limites de fichiers est du ressort du **niveau système**
   supérieur (l'environnement de mesure, sa surveillance de séquence et sa source
   de temps sûre) et y est résolue. Le paramètre `file_uuid` du bloc de
   métadonnées est spécifié dans [`osf5.md`](osf5.md) — obligatoire au niveau
   signed, recommandé sinon.
2. Le profil est un mécanisme de **sécurité informatique (security)** (au sens
   d'un environnement de catégorie 3 / réseaux ouverts) et ne revendique **aucune
   exigence de sûreté (safety)** : il ne remplace pas une transmission orientée
   sécurité selon la norme EN 50159 et ne justifie aucune aptitude SIL.

---

## 8. Remarques de migration (informatif)

Condensé de l'audit d'analyseurs `AUDIT_INTEGRITY_O1.md` (racine du dépôt).
Effort impliqué par le profil pour chaque implémentation :

**Les quatre implémentations (Rust, Delphi, C++, Java)**
- Détacher la CRC de trame **avant** l'analyseur typé, pilotée par la
  déclaration du profil (découpage en trames fail-closed, §3). Aujourd'hui,
  chaque lecteur écarte silencieusement les octets numériques excédentaires ou
  absorbe l'excédent dans une valeur string/binary, de sorte qu'une CRC ajoutée
  naïvement serait ignorée ou corromprait la valeur.

**Delphi**
- **Durcir le tokenizer d'en-tête** — il ne rejette aujourd'hui **pas** les
  jetons finaux inconnus (il découpe au niveau de l'espace et écarte silencieusement
  tout ce qui suit la longueur). Il doit rejeter conformément au §2.
- **Passer les octets de contrôle inconnus en skip-and-continue** — aujourd'hui,
  un type de bloc inconnu provoque un soft-abort, mal étiqueté « troncature » ;
  il doit ignorer un bloc `bcIntegritySignature` grâce au champ de longueur et
  poursuivre.

**Rust / C++ / Java**
- Rendre **intentionnel** le rejet des jetons d'en-tête inconnus et améliorer le
  diagnostic (« unexpected trailing token / unbekanntes Header-Token » au lieu
  d'une erreur d'analyse numérique). Les trois ignorent déjà proprement les
  octets de contrôle inconnus (valeur 9) grâce au champ de longueur — aucune
  modification n'y est nécessaire.

**Java**
- Planifier le travail d'intégrité comme une **implémentation complète** (le
  noyau Java est un lecteur OSF4+OSF5 complet, pas un stub) : refonte du
  tokenizer d'en-tête (effort **M**) et chemin string/binary gourmand (greedy)
  pour le détachement de la CRC (effort **L**).

---

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
