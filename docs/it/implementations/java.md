---
title: Implementazione Java
description: La libreria osf-java — implementazione Java 21 completa del Open Streaming Format con entrambi i writer OSF5, incapsulamento JPMS, OSFZ trasparente e profilo di integrità crc; inoltre gli strumenti osf-cli e osf-viewer
sidebar_position: 6
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - Maven
  - JPMS
  - JavaFX
  - crc32c
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH e optiMEAS Switzerland GmbH
---

# Implementazione Java

:::info Stato: disponibile
L'implementazione Java è **completamente realizzata e testata**. Legge
OSF4 e OSF5, scrive OSF5 con entrambi i modelli di writer, legge OSFZ in
modo trasparente e supporta il profilo di integrità crc. Le fonti
vincolanti delle decisioni sono
[DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
(architettura) e [§24](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
(profilo di integrità); lo stato più aggiornato è in
[STATUS.md](https://github.com/optimeas/osf/blob/main/STATUS.md).
:::

:::tip Manuale per sviluppatori
Questa pagina è la panoramica. La documentazione dettagliata per
sviluppatori si trova nel sottocapitolo **Java nel dettaglio**:

| Pagina | Contenuto |
|---|---|
| [Architettura](java/architecture.md) | Modello a livelli, moduli, incapsulamento JPMS, modelli di dati, convenzioni |
| [Lettura](java/reading.md) | `DataManager`, `DataChannel`, `BlockReader`, `ReaderStats`, OSFZ trasparente |
| [Scrittura](java/writing.md) | `StreamingWriter`, `BlockWriter`, `ChannelDef`, valori predefiniti dei metadati, profilo di integrità |
| [Gestione degli errori](java/error-handling.md) | Gerarchia `OsfException`, `verificationStatus()`, reader best effort |
| [Strumenti](java/tools.md) | `osf-cli` (verbi picocli) e `osf-viewer` (JavaFX) |
| [Build e integrazione](java/building.md) | Maven, Java 21, JPMS, dipendenze, test |
| [Cookbook](java/cookbook.md) | Ricette pronte per la copia, dall'ispezione al ciclo embedded |
| [Interni](java/internals.md) | Encoder, chunking, helper di integrità, parser — per i collaboratori |
:::

## Destinatari

Due destinatari principali: backend enterprise (Spring, microservizi,
optiCloud) nonché pipeline big data/IA (Spark, Flink, analisi dei dati).
A ciò si aggiunge il Java embedded, che registra dati operativi su un
gateway industriale — la stessa situazione «entrambi i mondi» che il §7
descrive già per il C++.

## Funzionalità

- **Lettura:** OSF4 (metablock XML) e OSF5 (metablock JSON), `DataManager`
  tipizzato con modello a canali/segmenti; reader best effort robusto.
- **Scrittura (OSF5):** **entrambi i writer** — un `BlockWriter` (raccolta
  in memoria, scrittura in un unico passaggio) per flussi di lavoro batch
  e uno `StreamingWriter` (`FileChannel.force(true)` per blocco) per la
  registrazione embedded a prova di interruzione. Entrambi producono file
  OSF5 **identici su disco**.
- **OSFZ trasparente:** i file compressi con gzip vengono decompressi
  automaticamente in lettura (`java.util.zip`, incluso nel JDK).
- **Profilo di integrità `crc`:** attivabile opzionalmente su entrambi i
  writer; il reader verifica il CRC del metablock e dei frame (vedere
  sotto).

## Build e utilizzo

**Java 21** e **Maven**. Distribuita come artefatto Maven
(`groupId=com.optimeas.osf`, `artifactId=osf-java`); il POM è pronto per
la pubblicazione (il deploy in un repository pubblico è ancora
rimandato).

```bash
# Aus dem Repository-Wurzelverzeichnis: den Java-Reaktor bauen und testen
mvn -f implementations/java/pom.xml test
```

**JPMS** (Java Platform Module System): `module-info.java` esporta solo
`com.optimeas.osf`; gli helper interni sotto `com.optimeas.osf.internal`
restano incapsulati — anche rispetto alla reflection.

**Dipendenze:** Jackson (JSON OSF5), StAX (XML OSF4, nel JDK),
`java.util.zip` (OSFZ + CRC32C, nel JDK), SLF4J (facade di logging).
I/O binario tramite `ByteBuffer` su `FileChannel` con `LITTLE_ENDIAN`.

## Moduli

Il reactor Java comprende, oltre alla libreria core, due strumenti:

| Modulo | Scopo |
|---|---|
| **`osf-java`** | Libreria core — lettura, entrambi i writer, OSFZ, profilo di integrità. |
| **`osf-cli`** | Strumento a riga di comando (picocli): `info`, `channels`, `dump`, `convert`; compilato come JAR eseguibile. |
| **`osf-viewer`** | Viewer JavaFX per più canali (min/max per pixel). Avvio: `mvn -pl osf-viewer javafx:run`. |

## Profilo di integrità (`crc`)

Profilo di integrità OSF5 opzionale al livello `crc` (CRC32C,
`java.util.zip.CRC32C`, nativo del JDK; valore di controllo
`0xE3069283`, identico byte per byte a Rust/C++/Delphi).

- **Reader:** `MagicHeaderParser` riconosce il token `crc32c`;
  `DataManager` verifica il CRC del metablock prima del parsing,
  `BlockReader` verifica e rimuove il CRC di frame di 4 byte prima
  della valutazione tipizzata (fail-closed). I blocchi di firma (canale
  `0xFFFE`) vengono saltati e conteggiati, in modo che i file firmati
  restino leggibili. `ReaderStats` fornisce `integrity` +
  `verificationStatus()` (`none`/`crc_valid`/`invalid`/`signature_unverifiable`).
- **Writer:** `setIntegrity(IntegrityProfile.CRC32C)` su **entrambi** i
  writer (predefinito disattivato) scrive il token, il CRC del metablock
  e per ogni blocco un CRC di frame.

Il livello di firma (`signed`, Ed25519) non è ancora implementato.
Fondamenti: [DECISIONS §24](https://github.com/optimeas/osf/blob/main/DECISIONS.md).

## Conformità alla specifica

L'implementazione Java segue le stesse regole semantiche di Rust, Python,
C++ e Delphi: tutti i tipi di dati attuali (tipi unsigned tramite
promozione di tipo Java o `BigInteger` per l'intero intervallo), rifiuto
esplicito dei tipi rimossi, `bytearray` come alias di lettura per
`binary`, `channeltype` come **forma dei dati**
(scalar/vector/matrix/binary), la regola deterministica in base alla
versione per la terminazione null di `string`/`binary` e tutti e quattro
gli identificatori del magic header. La conformità viene verificata
rispetto al contratto condiviso del manifesto di riferimento
(`examples/reference_manifest.json`).

## Codice sorgente e ulteriori informazioni

- Decisione architetturale: [DECISIONS §21](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
- Profilo di integrità: [DECISIONS §24](https://github.com/optimeas/osf/blob/main/DECISIONS.md)
- Stato attuale: [STATUS.md](https://github.com/optimeas/osf/blob/main/STATUS.md) · [github.com/optimeas/osf](https://github.com/optimeas/osf)
- Specifica del formato: capitolo [Formato OSF](../osf_general.md)

> Questo documento è distribuito con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
