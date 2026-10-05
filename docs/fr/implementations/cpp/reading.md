---
title: Lecture
description: Lire des fichiers OSF et OSFZ — DataManager, DataChannel, BlockReader, ReaderStats et décompression transparente
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - DataManager
  - BlockReader
  - OSFZ
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Lecture

L'implémentation C++ lit **OSF4**, **OSF5** et, de manière transparente,
**OSFZ** (gzip/zlib) via la même API. Il existe deux niveaux de lecture :

- **`osf::DataManager`** — la voie standard. Charge entièrement le fichier,
  assemble des canaux typés à partir du flux de blocs et résout toutes les
  limites de blocs. Pour l'analyse, l'export, l'outillage.
- **`osf::BlockReader`** — le niveau flux. Fournit bloc après bloc dans
  l'ordre du fichier avec un besoin en mémoire constant. Pour de très gros
  fichiers, des agrégations personnalisées et des outils spécialisés.

## Démarrage rapide

```cpp
#include <osf/osf.h>

auto result = osf::DataManager::loadFromFile("messung.osf");  // auch .osfz
if (!result) {
    std::cerr << result.error().message << "\n";
    return 1;
}
osf::DataManager const& mgr = *result;

// Kanal über den Namen ansprechen (primäre Zugriffsform)
if (osf::DataChannel const* ch = mgr.channel("Sensor.Temperatur")) {
    if (auto werte = osf::asDoublesFlat(std::get<osf::TimestampedChannel>(*ch))) {
        for (auto const& [ts_ns, wert] : *werte) { /* … */ }
    }
}
```

## `DataManager`

### Chargement

| Méthode | Source | Remarques |
|---|---|---|
| `DataManager::loadFromFile(path)` | Fichier | OSF, OSFZ ; détermine la taille du fichier pour `stats` |
| `DataManager::loadFromStream(istream&)` | `std::istream` quelconque | Le flux doit être positionné au début du fichier et (pour la détection OSFZ) **positionnable (seekable)** |

Les deux voies parcourent le même pipeline : détection OSFZ →
magic header → parseur de metablock (JSON ou XML) → `BlockReader` jusqu'à
EOF → assemblage des canaux. Le résultat est immuable et peut être lu par
un nombre quelconque de threads simultanément.

### Accès

```cpp
mgr.meta;                  // osf::MetaBlock  — FileInfo, Kanal-Definitionen, Infos
mgr.stats;                 // osf::ReaderStats — Telemetrie des Ladevorgangs
mgr.channels();            // std::vector<DataChannel> const& — Metablock-Reihenfolge
mgr.channel("a.b.c");      // DataChannel const* — nullptr, wenn unbekannt (Pflicht-Form)
mgr.channelByIndex(7);     // DataChannel const* — Index aus dem Metablock (optional)
```

`channel(name)` est la forme d'accès principale ;
`channelByIndex` est un confort. Les deux renvoient `nullptr` au lieu d'une
erreur, car « canal inexistant » est un cas normal lors de l'exploration de
fichiers tiers.

### Ce qui peut se produire lors du chargement

- **Fichier tronqué :** pas d'erreur. Tous les blocs entièrement lisibles
  se retrouvent dans les canaux, `mgr.stats.blocksTruncated == 1`.
- **Type de données inconnu (futur) :** le canal est **omis** de la
  liste des canaux (ses blocs ont été ignorés au niveau du lecteur) ;
  la définition reste visible dans `mgr.meta.channels`,
  y compris la graphie d'origine dans `dataTypeRaw`.
- **Erreurs de structure :** `InvalidMetablock`, `UnknownChannelIndex`,
  `ChannelMixedBlockTypes`, etc. interrompent le chargement avec une
  erreur structurée — voir
  [Gestion des erreurs](error-handling.md).

## `DataChannel` — les canaux typés

`DataChannel` est un `std::variant` sur trois dispositions :

```cpp
using DataChannel = std::variant<EquidistantChannel, TimestampedChannel, VariableChannel>;
```

### Accesseurs communs (fonctions libres)

Pour du code indépendant de la variante, il existe des fonctions libres qui utilisent en interne
`std::visit` :

```cpp
osf::channelIndex(ch);          // std::uint16_t
osf::channelName(ch);           // std::string const&
osf::channelDataType(ch);       // osf::DataType
osf::channelPhysicalUnit(ch);   // std::optional<std::string>
osf::channelDisplayName(ch);    // std::optional<std::string>
osf::channelSampleCount(ch);    // std::size_t (Summe über alle Segmente)
osf::channelIsEmpty(ch);        // bool
osf::channelMeta(ch);           // ChannelMeta const& (sekundäre Definitionfelder)
```

### `EquidistantChannel` — segments plutôt qu'horodatages

Les canaux équidistants ne stockent **aucun horodatage par échantillon**.
À la place : un vecteur d'échantillons à plat (`NumericValues`, une variante
sur tous les types numériques) plus une liste de segments. Chaque
bloc `bcStartData` du fichier ouvre un segment :

```cpp
struct Segment {
    std::int64_t startTimestampNs;  // absoluter Startzeitpunkt
    double       sampleRateHz;      // gilt bis zum nächsten Segment
    std::size_t  startIndex;        // erster Sample-Index im flachen Vektor
    std::size_t  sampleCount;       // Anzahl Samples dieses Segments
};
```

L'échantillon `i` d'un segment se situe à
`startTimestampNs + i * (1e9 / sampleRateHz)`. Les lacunes entre
segments **ne** sont **pas** interpolées — une pause d'enregistrement
reste une pause.

Qui a besoin de paires `(horodatage, valeur)` appelle
`samplesVector()` (matérialise ; reconstruit les horodatages
à partir des segments) :

```cpp
auto const& eq = std::get<osf::EquidistantChannel>(*ch);
for (auto const& s : eq.samplesVector()) {
    // s.timestampNs, s.value (NumericValueRef = Variante über die numerischen Typen)
}
```

### `TimestampedChannel` — vecteurs parallèles

```cpp
auto const& ts = std::get<osf::TimestampedChannel>(*ch);
ts.timestampsNs;  // std::vector<std::int64_t>, Stream-Reihenfolge
ts.values;        // NumericValues, parallel dazu
```

Les blocs `bcAbsTimeStampData` atterrissent directement ici ;
les deltas OSF4 `bcContinuedRelStampData` sont convertis en horodatages
absolus lors du chargement (ancrage = dernier horodatage absolu du
canal).

### `VariableChannel` — String et Binary

```cpp
auto const& var = std::get<osf::VariableChannel>(*ch);
var.timestampsNs;                       // ein Zeitstempel pro Sample
auto strs = var.asStrings();            // Result<std::vector<std::string> const*>
auto bins = var.asBinaries();           // Result<std::vector<std::vector<uint8_t>> const*>
var.mimeType;                           // z. B. "image/jpeg" bei Binary-Kanälen
```

Exactement l'un de `string_values` / `binary_values` est renseigné
(selon `dataType`) ; l'accesseur de mauvais type renvoie
`DataTypeMismatch`. Le traitement du terminateur nul est
déterministe selon la version (rév. de spécification 2026-05-24) : avec **OSF4**, le
lecteur a déjà retiré le dernier octet, avec **OSF5**, le payload
arrive inchangé.

### Accesseurs à plat — copies typées

Pour chaque type numérique (plus GPS), il existe des utilitaires `as_<typ>_flat` sous
deux formes :

```cpp
// EquidistantChannel: nur die Werte
Result<std::vector<double>> osf::asDoublesFlat(EquidistantChannel const&);

// TimestampedChannel: (Zeitstempel, Wert)-Paare
Result<std::vector<std::pair<std::int64_t, double>>> osf::asDoublesFlat(TimestampedChannel const&);
```

(de même `asFloatsFlat`, `asInt32Flat`, …, `asGpsFlat`). Ils
**copient** à chaque appel et renvoient `DataTypeMismatch` si
le type stocké ne correspond pas. Pour les chemins critiques, on accède plutôt
une seule fois au vecteur stocké via `std::get` / `std::visit`.

## `BlockReader` — le niveau flux

Lorsque le `DataManager` est trop lourd (RAM, fichiers géants, agrégation
personnalisée), on lit soi-même le flux de blocs :

```cpp
#include <osf/osf.h>
#include <fstream>

std::ifstream in("messung.osf", std::ios::binary);
auto header = osf::parseMagicHeader(in);          // Result<MagicHeader>
// … Metablock-Bytes (header->metablockLen) lesen und parsen …
auto meta = osf::parseMetablockJson(buf.data(), buf.size());

osf::BlockReader reader(in, *meta);
for (auto& blk : reader) {                        // Input-Iterator + Sentinel
    if (!blk) { /* harter Fehler, Iteration endet */ break; }
    std::visit([](auto const& kind) { /* StartData / ContinuedData / … */ },
               blk->kind);
}
auto stats = reader.stats();
```

Propriétés importantes :

- **Primitive `next()` :** `std::optional<Result<Block>>` —
  `std::nullopt` = fin propre (EOF, trailer consommé ou
  troncature), valeur avec erreur = arrêt strict (p. ex.
  `UnknownChannelIndex`).
- **Single-pass :** l'itérateur est un input iterator ; une seconde
  itération nécessite un nouveau lecteur (et une réinitialisation du flux).
- **Les sauts restent visibles :** les control bytes obsolètes/réservés et
  les blocs de canaux déclarés `Unsupported` passent sous la forme
  `BlockKind::Skipped` avec `SkipReason`. Les octets de payload
  sont par défaut écartés sans allocation ; pour y jeter un œil
  (p. ex. dans les blocs `bcStatusEvent` ou `bcTrustedTimestamp`, qui
  restent ignorés) : `reader.withCaptureSkippedPayload(true)`.
  `bcMessageEvent` est décodé pour les canaux `string`/`binary` au lieu
  d'être ignoré et n'a donc plus besoin de cette option.
- **Trailer OSF4 :** le bloc d'info optionnel `0xFFFF` + le trailer de 40 octets
  sont consommés silencieusement ; `reader.trailerSeen()` le signale.
- Le `BlockReader` ne décompresse **pas** lui-même — pour l'OSFZ, on place
  un `DecompressingIStream` devant (c'est exactement ce que fait le
  `DataManager`).

## OSFZ transparent

```cpp
#include <osf/compression.h>

std::ifstream raw("messung.osfz", std::ios::binary);
osf::CompressionFormat fmt = osf::detectCompression(raw); // None/Zlib/Gzip, nicht-konsumierend

osf::DecompressingIStream in(raw);   // istream-Fassade; inflatet bei Bedarf
// in wie jeden std::istream benutzen: parseMagicHeader(in), BlockReader, …
```

La détection s'appuie sur les deux premiers octets (gzip `1F 8B`, zlib
`78 01/5E/9C/DA` ; un vrai OSF commence par `O` = `0x4F`, il n'y a donc jamais
de collision). La décompression est à mémoire constante (`std::streambuf`
en flux sur `z_stream`), best-effort en cas de troncature et
sans types zlib dans le header public (PIMPL). `DataManager` utilise
cette couche automatiquement — `loadFromFile("x.osfz")` fonctionne
sans autre intervention, et `stats.compressed` /
`stats.compressionFormat` documentent la détection.

## `ReaderStats` — télémétrie

Après chaque chargement (ou via `reader.stats()`) :

| Champ | Signification |
|---|---|
| `fileSizeBytes` | Taille du fichier (si connue) |
| `headerSizeBytes` / `metablockSizeBytes` / `dataSectionSizeBytes` | Tailles des trois sections du fichier |
| `elapsed` | Temps réel (horloge murale) de l'itération des blocs |
| `channelsTotal` / `channelsWithData` / `channelsUnsupported` | Compteurs de canaux |
| `blocksTotal` / `blocksRead` / `blocksSkipped*` / `blocksTruncated` | Compteurs de blocs par motif |
| `trailerSeen` | Bloc d'info/trailer OSF4 rencontré |
| `compressed` / `compressionFormat` | Détection OSFZ |
| `perChannel` | `ChannelStats` par index de canal : nom, compteurs de blocs/échantillons/octets, nombre de segments, plage temporelle |

`operator<<` formate les deux structures sur plusieurs lignes pour les sorties CLI ;
`formatBytes` / `formatDuration` sont disponibles
séparément.

```cpp
std::cout << mgr.stats;                     // mehrzeilige Zusammenfassung
for (auto const& [idx, cs] : mgr.stats.perChannel)
    std::cout << cs << "\n";                // einzeilig pro Kanal
```

## Remarques sur les performances

- Les fichiers de terrain réels de l'ordre de quelques Mo se chargent en
  quelques millisecondes dans les builds Release.
- Le `DataManager` conserve tous les échantillons en mémoire ; en règle générale,
  un fichier nécessite à peu près sa taille décompressée en RAM. Pour des
  volumes plus importants : utiliser `BlockReader` en flux.
- Les accesseurs à plat copient. Faire un seul `std::get` et travailler directement sur le
  vecteur est la forme la plus rapide pour des accès répétés.

> Ce document est placé sous licence [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribution : optiMEAS GmbH et optiMEAS Switzerland GmbH.
