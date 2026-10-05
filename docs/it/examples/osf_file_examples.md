---
title: File di esempio
description: File di esempio OSF — file di riferimento sintetici e registrazioni reali sul campo per test e apprendimento
image: "/img/om_social_card.png"
keywords:
  - Formato di file
  - OSF4
  - OSF5
  - OSF
  - Esempi
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

## File di esempio OSF

La directory [`examples/`](https://github.com/optimeas/osf/tree/main/examples)
nel repository contiene due tipi di file con cui è possibile imparare il
formato e verificare un reader passo dopo passo: **file di riferimento
generati** (sintetici, una caratteristica per file) e **campioni dal
campo** (registrazioni reali di dispositivi di misura optiMEAS).

## File di riferimento generati

I file in `examples/generated/` vengono prodotti
dall'[implementazione di riferimento Delphi](../implementations/delphi.md)
(riproducibili in modo non interattivo con `OSFGeneratorCLI`). Ogni file
è mirato a **una** caratteristica ed esiste sia nella variante OSF4
(header XML) sia nella variante OSF5 (header JSON):

| File (OSF4 / OSF5) | Mostra |
|---|---|
| `osf4_equidistant.osf` / `osf5_equidistant.osf` | Canale equidistante (frequenza di campionamento fissa) |
| `osf4_scalar_numeric.osf` / `osf5_scalar_numeric.osf` | Canali scalari in virgola mobile con timestamp |
| `osf4_scalar_int64.osf` / `osf5_scalar_int64.osf` | Canale intero a 64 bit con segno |
| `osf4_scalar_unsigned.osf` / `osf5_scalar_unsigned.osf` | Canali interi senza segno |
| `osf4_gpslocation.osf` / `osf5_gpslocation.osf` | Canale di posizione GPS |
| `osf4_timestamped_string.osf` / `osf5_timestamped_string.osf` | Dati utili `string` con timestamp |
| `osf4_timestamped_binary.osf` / `osf5_timestamped_binary.osf` | Blob `binary` con timestamp |
| `osf4_mixed.osf` / `osf5_mixed.osf` | Più tipi di canale in un unico file |
| — / `osf5_mixed_extended.osf` | File OSF5 misto esteso |

In totale **17 file** (8 OSF4 + 9 OSF5). Un reader corretto deve
elaborarli tutti senza errori. Questi file sono le fixture di lettura
valide per tutti i linguaggi; l'abbinamento OSF4/OSF5 verifica al tempo
stesso le regole deterministiche in base alla versione — ad esempio la
terminazione null dei dati utili `string`/`binary` (OSF4 con, OSF5 senza
`0x00` finale).

## Campioni dal campo — registrazioni reali

Dati reali provenienti da dispositivi optiMEAS. Coprono ciò che i file
sintetici non possono: grandi numeri di canali, pattern reali di
timestamp e flussi che terminano bruscamente. Un reader deve leggerli
senza arresti anomali e restituire i dati presenti.

| Percorso | Formato | Descrizione |
|---|---|---|
| `motorbike.osf` | OSF4 | 81 canali di telemetria motociclistica — velocità, temperature, GPS, stato del sistema |
| `steam_loco.osf` (+ `.csv`) | OSF4 | 123 canali da una registrazione di una locomotiva a vapore (`.csv` è un'esportazione di riferimento degli stessi dati) |
| `weather_station.osfz` | OSF4, gzip | 28 canali, OSFZ compresso con gzip — verifica la decompressione trasparente in lettura |
| `Testdata Motorbike/` | OSFZ | Registrazioni motociclistiche di più giorni in sottocartelle giornaliere (`YYYYMMDD/…`) — test di robustezza/lettura di massa |

## Aprire un file

Il modo più rapido è il pacchetto Python
[`osfdata`](../integrations/python.md):

```python
import osf

mgr = osf.load("examples/steam_loco.osf")
print(f"{len(mgr)} Kanäle")
for ch in mgr.channels:
    print(ch.name, ch.data_type, ch.sample_count)
```

I file possono essere aperti anche con l'implementazione
[Rust](../implementations/rust.md)
(`cargo run --example inspect -- <pfad>`), con quella
[C++](../implementations/cpp.md) o con quella
[Delphi](../implementations/delphi.md) (`osftool info`). Un'introduzione
al formato è fornita dal capitolo [Formato OSF](../osf_general.md).

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
