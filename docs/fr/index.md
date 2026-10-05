---
title: Format OSF
description: Introduction au Open Streaming Format
#hide_title: true/false
#hide_table_of_contents: true/false
#sidebar_label: Label shown in the sidebar
#sidebar_position: Position relative to its parent if needed
image: "/img/om_social_card.png"
keywords:
  - Format de fichier
  - OSF4
  - OSF5
  - OSF
last_update:
  date: 2026-07-07
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

## Le format OSF – Introduction

OSF est conçu comme un format de streaming universel pour les données de mesure et de process. Il répond aux questions essentielles de l'acquisition de données moderne : comment stocker sans perte des données qui arrivent en flux continu ? Comment garder le format ouvert et extensible, sans complexité inutile ? Et comment concilier les exigences des systèmes embarqués avec l'efficacité des plateformes d'analyse performantes ?

OSF existe actuellement en version 4, tandis que la version 5 est en cours d'implémentation. Bien qu'OSF4 et OSF5 ne diffèrent que sur quelques points, nous avons délibérément opté pour un saut de version : le passage à un nouveau format d'en-tête (JSON par défaut) et la gestion simplifiée de l'octet de contrôle dans les données justifient avant tout cette nouvelle version majeure. En même temps, OSF5 reste entièrement rétrocompatible avec OSF4 et peut lire et traiter ses fichiers – à quelques différences de détail près – sans restriction.

### Ce qu'OSF apporte en pratique

OSF est développé pour la réalité des systèmes de mesure modernes : des flux de données qui circulent en continu, doivent être acquis rapidement et stockés de manière sûre. Un point est au centre : un enregistrement sans lacune ni perte de données, même lorsque les systèmes s'arrêtent brutalement ou que l'alimentation électrique est interrompue.

Mais OSF ne s'arrête pas à l'écriture : il est tout aussi **optimisé pour le traitement ultérieur.** Au lieu de stocker les valeurs une à une, OSF range les données par blocs. Cela permet :

-   un **stockage efficace et rapide**, même à haut débit de données,
-   un **chargement et une exploitation rapides** de grandes quantités de données,
-   des structures cohérentes, performantes aussi bien sur les systèmes embarqués que sur les plateformes d'analyse.

### Avantages en un coup d'œil :

-   **Enregistrement continu et sans perte** des données sur les appareils embarqués.
-   **Robustesse** face aux coupures de courant ou aux arrêts inattendus.
-   **Structure de blocs efficace** pour un stockage et un chargement rapides.
-   **Flexible** pour toutes les données liées au temps : valeurs isolées, vecteurs, matrices, images et données audio.
-   **Ouvert et simple à implémenter**, avec une structure claire.
-   **Uniforme** – un seul format pour l'edge, le laboratoire et le cloud.
-   **Profil d'intégrité facultatif** (à partir d'OSF5) à trois niveaux – *none*, *crc*, *signed* : détection de la corruption de données par CRC32C bloc par bloc, chaîne de signatures Ed25519 ainsi que vérifiabilité par des tiers via X.509/PKI. Voir [Profil d'intégrité OSF5](references/osf5_integrity.md) ; document de concept : [doi.org/10.5281/zenodo.21227941](https://doi.org/10.5281/zenodo.21227941).

OSF n'est ainsi pas seulement un format de stockage, mais un outil pour accompagner **les flux de données sans perte et de façon efficace tout au long de leur cycle de vie** – de la première valeur mesurée jusqu'à l'analyse.


## Une solution ouverte pour les données liées au temps

La réponse d'OSF à ces exigences est une structure claire et ouverte, valable de la même manière pour **OSF4** et **OSF5** :

-   **En-tête magique** pour l'identification et un traitement rapide.
-   **Métabloc (XML ou JSON)** pour décrire les canaux, les types de données et les informations de contexte.
-   **Blocs de données binaires** pour un streaming continu et robuste.
-   **Stockage orienté blocs**, pour écrire efficacement de grandes quantités de données et les recharger rapidement.

### Structure de fichier OSF de base

L'architecture centrale permet de ranger toutes les données liées au temps dans un format unique et cohérent. OSF est **documenté ouvertement**, facile à implémenter et évolutif, des appareils edge jusqu'au cloud.

![Structure de fichier OSF.](media/dfa207f25f79c84ec79e2e2f6c2c42cc.jpg)

### Ce qui change avec OSF5

-   JSON comme standard pour l'en-tête (avec rétrocompatibilité avec OSF4/XML).
-   Utilisation simplifiée de l'octet de contrôle dans le bloc de données, pour un moindre effort d'implémentation.
-   Suppression du trailer au profit d'une structure de fichier plus claire.
-   En-têtes magiques flexibles (`OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) avec reconnaissance automatique de XML (`<`) ou de JSON (`{`).

La suite décrit ce qui fait d'OSF un format – **valable de manière générale pour OSF4 et OSF5**. Les détails sur des fonctions spécifiques et sur les canaux vectoriels ou matriciels figurent dans des documents distincts.

> Ce document est publié sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
