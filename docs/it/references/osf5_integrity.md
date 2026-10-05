---
title: Profilo di integrità OSF5
description: Specifica normativa del profilo di integrità opzionale OSF5 — rilevamento della corruzione CRC32C, catena di firme Ed25519 e prova di provenienza X.509/PKI
image: "/img/om_social_card.png"
keywords:
  - OSF5
  - Integrità
  - CRC32C
  - Ed25519
  - Firma
  - PKI
last_update:
  date: 2026-07-08
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Profilo di integrità OSF5

Questo documento è la specifica **normativa** del **profilo di integrità**
opzionale per l'Open Streaming Format versione 5 (OSF5). Integra il formato di
base con tre garanzie graduali: rilevamento della corruzione a livello di blocco
(CRC32C), rilevamento delle manomissioni e prova di provenienza compatibili con lo
streaming (una catena di hash SHA-256 con ancore di firma Ed25519 periodiche) e
una verificabilità offline da parte di terzi (un modello di certificati X.509/PKI).

Il profilo è una **proprietà esclusiva di OSF5**. I file OSF4 non ne sono
interessati e non ne contengono mai alcuna parte. Il design è pienamente
retrocompatibile: i file OSF5 *senza* dichiarazione di integrità rimangono validi
senza modifiche.

:::info Documento concettuale
La motivazione progettuale di questo profilo — la scala a tre livelli, la
semantica fail-closed dei token, la catena di hash con ancore di firma e il
modello di fiducia PKI — è riportata nel documento concettuale *Integrità e firma
nei formati di dati di misura in streaming: l'approccio OSF5*, pubblicato su
Zenodo in **tedesco e inglese** (DOI
[10.5281/zenodo.21227941](https://doi.org/10.5281/zenodo.21227941), CC BY 4.0)
e in questo repository in
[`docs/papers/`](https://github.com/optimeas/osf/tree/main/docs/papers/).
:::

Il documento fornisce il contesto; **questo documento è il riferimento normativo**
per gli implementatori.

Il formato di base (magic header, metablock JSON, blocchi dati autonomi) è
descritto in [`osf_general.md`](../osf_general.md) e
[`osf5.md`](osf5.md); questo documento integra soltanto il livello di
integrità.

---

## 1. Modello a tre livelli

Il profilo è una **scala rigorosamente ordinata**; ogni livello include quello
sottostante:

```
none  ⊂  crc  ⊂  signed
```

Il livello vale **per file** e viene dichiarato **esattamente una volta**.

| Livello | Protegge da | Impiego tipico |
|---|---|---|
| **none** | — | Laboratorio, file intermedi, writer embedded minimali |
| **crc** | Corruzione (errori di bit, troncamento, errori del supporto) | Standard per i dati di campo |
| **signed** | Corruzione **e** manipolazione; provenienza dimostrabile a terzi | Dati operativi con valore probatorio |

Due regole di progetto impediscono la frammentazione del formato:

1. **I writer scelgono liberamente il livello** — i sistemi embedded mantengono
   l'opzione di un writer minimale.
2. **Ogni reader OSF5 conforme DEVE elaborare tutti i livelli.** Ne risulta un
   unico formato con profilo, non tre dialetti.

La granularità è sempre il **file intero**: o tutti i blocchi contengono il
meccanismo, oppure nessuno.

---

## 2. Grammatica dei token di header (normativa)

La riga del magic header viene estesa, dopo la lunghezza del metablock, con zero o
più token opzionali:

```abnf
header-line     = identifier SP metablock-len *(SP token) LF
token           = key ":" value
key             = 1*(a-z / 0-9 / "-")          ; nur Kleinbuchstaben
value           = 1*(VCHAR ohne SP)
```

- Esattamente **uno** `SP` (0x20) separa i campi; prima del `LF` **non è presente
  alcuno spazio finale**.
- I token sono **«must understand»**: un reader che non conosce una `key`
  **DEVE rifiutare il file**, con una diagnostica del tipo
  `unbekanntes Header-Token '<key>'` — **non** come errore di parsing numerico. (Gli
  attuali parser rifiutano un token inatteso solo in modo incidentale, tramite un
  fuorviante messaggio «la lunghezza non è un numero valido»; vedere l'appendice
  sulla migrazione.)
- I token di header sono una caratteristica **solo OSF5**. Gli identificatori
  OSF4 (`OSF4`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) NON DEVONO
  contenere token; un token dopo un identificatore OSF4 indica un file difettoso.

### Chiavi definite in questa revisione

| Chiave | Livello | Valore |
|---|---|---|
| `crc32c` | crc | `crc32c:<8 HEXDIG maiuscole>` — la CRC32C (Castagnoli) calcolata sui **byte grezzi del metablock** esattamente come nel file |
| `ed25519` | signed | `ed25519:<keyid>` — `keyid` = 16 HEXDIG **minuscole** = i primi 8 byte dello SHA-256 calcolato sul `SubjectPublicKeyInfo` codificato in DER del certificato del dispositivo |

- **Il casing esadecimale è rigoroso**: `crc32c` utilizza l'esadecimale in
  **maiuscolo**; il `keyid` di `ed25519` utilizza l'esadecimale in **minuscolo**. I
  reader DEVONO verificare rigorosamente il casing.
- Il token `ed25519` è ammesso solo **in aggiunta a** `crc32c` e solo in questo
  ordine — prima `crc32c`, poi `ed25519`. `signed` implica quindi sempre `crc`.
- Il metablock è l'unica parte del file che non può proteggersi da sé; il token
  `crc32c` contiene la sua checksum, in modo che l'intero ordine di verifica sia
  deterministico.

### Ordine di verifica (normativo)

1. Leggere i token di header.
2. Verificare la CRC del metablock (dal token `crc32c`).
3. Analizzare il metablock.
4. Verificare i blocchi dati (CRC di frame e — al livello signed — la catena di
   firme).

---

## 3. CRC di frame (livello crc)

Al livello crc **ogni** blocco contiene una **CRC32C** (Castagnoli;
accelerata via hardware su x86/SSE4.2 e ARMv8) calcolata sull'**intero frame**:
indice del canale, campo di lunghezza, byte di controllo e dati utili.

Includere deliberatamente il **campo di lunghezza** nell'ambito è importante: un
campo di lunghezza alterato è il singolo errore più grave, perché il reader perde
poi tutti i confini dei blocchi.

La CRC costituisce gli **ultimi quattro byte dell'area dati** ed è **conteggiata
nel campo di lunghezza**, in modo che il framing rimanga intatto per qualsiasi
reader. La lunghezza utile effettiva è la lunghezza del blocco **meno quattro**.

```
ohne Profil:   [Kanalindex][Längenfeld][Steuerbyte][ Nutzdaten ................ ]
                                       |<------------- LEN ------------------>|

mit  Profil:   [Kanalindex][Längenfeld][Steuerbyte][ Nutzdaten ....... ][CRC32C]
                                       |<------------- LEN ------------------>|
               |<================ Scope der Frame-CRC ====================>|
```

### Prescrizione normativa di implementazione (framing fail-closed)

Con il profilo attivo la CRC di frame è **parte del framing** e DEVE essere
separata **prima** della valutazione tipizzata dei dati utili. Un controllo
«resto == 0» *a valle*, dopo la decodifica, **non è sufficiente**: con tipi di dati
a lunghezza variabile (`string`, `binary`) un reader non a conoscenza
dell'integrità non può distinguere i byte CRC aggiunti dal vero payload e
restituirebbe valori corrotti. Il fail-open è quindi escluso per costruzione — il
reader separa gli ultimi quattro byte sulla base della conoscenza del profilo
ricavata da header/metablock e li verifica.

### Comportamento in caso di errore

| Condizione | Reazione |
|---|---|
| Errore CRC di un blocco dati | il blocco è **non valido** → saltare, **contare nella statistica di lettura** e proseguire (dati parziali sono meglio di nessun dato) |
| Errore CRC del metablock | **rifiutare il file** (senza un metablock affidabile nulla è interpretabile) |
| Token di header sconosciuto | **rifiutare il file** (vedere §2) |

**Raccomandazione per le implementazioni:** oltre alla separazione obbligatoria,
un rigoroso controllo di consumo completo per i blocchi **numerici** (`N ×
dimensione del campione (+ campi di header)` deve corrispondere alla lunghezza
utile effettiva). Si tratta di un miglioramento della qualità diagnostica, non di
un requisito di correttezza.

---

## 4. Blocco di firma `bcIntegritySignature = 9` (livello signed)

Il livello signed aggiunge un nuovo tipo di byte di controllo e una catena di hash
continua.

### Il blocco

- Nuovo valore del byte di controllo **9** (`bcIntegritySignature`); valido solo
  se il file dichiara il livello **signed**; **bit 7 = 0** (semantica a valore
  singolo).
- **Indice del canale: il valore riservato `0xFFFE`** — un canale di integrità a
  livello di file, che **non** viene dichiarato nel metablock. I reader senza
  supporto al livello signed saltano il blocco tramite il campo di lunghezza,
  esattamente come qualsiasi altro tipo di blocco sconosciuto (vedere §5). `0xFFFE`
  è diverso dal canale info/trailer `0xFFFF` di OSF4.
- I blocchi sul canale riservato `0xFFFE` utilizzano **sempre un campo di
  lunghezza a 4 byte (`uint32`)**, indipendentemente dalle dichiarazioni dei
  canali — in analogia con lo storico blocco info `0xFFFF`.
- I blocchi di firma contengono essi stessi una **CRC di frame**, come ogni altro
  blocco.

### Payload (little-endian; ordine normativo)

| # | Campo | Tipo | Significato |
|---|---|---|---|
| 1 | `anchor_seq` | `uint32` | Numero di sequenza dell'ancora, progressivo a partire da 0 |
| 2 | `signing_time_ns` | `int64` | Istante della firma (base della semantica di validità) |
| 3 | `chain_hash` | `byte[32]` | H(i), l'hash di catena corrente |
| 4 | `signature` | `byte[64]` | Ed25519 su `SHA-256(anchor_seq ‖ signing_time_ns ‖ chain_hash)`, con i campi nell'ordine e nella codifica del wire |
| 5 | `keyid_len` + `keyid` | `uint8` + `byte[keyid_len]` | Riferimento alla chiave/al certificato; `keyid` come nel token di header (8 byte) |

### Catena di hash

```
H(0) = SHA256(Header-Zeile ‖ Metablock)
H(i) = SHA256(H(i−1) ‖ Frame_i)
```

- I frame entrano nella catena **inclusa** la loro CRC di frame.
- **Anche i blocchi di firma stessi entrano nella catena** (come un `Frame_i`).
- Il **primo ancoraggio copre quindi anche header e metablock** tramite H(0).

Oltre alla verifica dei singoli blocchi, la catena rileva anche
**cancellazione**, **inserimento** e **riordinamento** di blocchi; i numeri di
sequenza delle ancore rilevano la **ripetizione all'interno del file**.

### Cadenza

Configurabile (su base temporale o a blocchi). Il **default normativo è su base
temporale, 10 secondi**. Inoltre un'ancora è **obbligatoria alla chiusura
regolare del file**, in modo che un file chiuso correttamente sia interamente
firmato.

### Semantica in caso di interruzione dell'alimentazione (normativa)

Se la registrazione si interrompe bruscamente, il file è **firmato fino all'ultima
ancora valida**; la coda successiva è **valida per CRC, ma non firmata**. Il
rapporto di verifica DEVE indicare entrambe le condizioni (ad es. «firmato fino al
timestamp X, resto valido per CRC, non firmato»).

---

## 5. Certificati e PKI (livello signed)

Affinché **qualsiasi terzo** — non solo il costruttore — possa verificare la
provenienza, le chiavi dei dispositivi vengono attestate da un'autorità di
certificazione (X.509 con Ed25519 secondo RFC 8410), in analogia con il modello di
fiducia di HTTPS, ma come gerarchia privata e documentata pubblicamente: una **CA
radice** offline protetta via hardware, una CA emittente (collegata alla
produzione o al cloud dei dispositivi) e, al di sotto, certificati dei dispositivi
il cui subject contiene il numero di serie del dispositivo.

### Posizione di incorporamento: il metablock

La catena di certificati viene incorporata una volta per file come nuovo
**oggetto opzionale a livello `osf`** del metablock:

```json
"integrity": {
  "certificates": ["<base64 DER Gerätezertifikat>", "<base64 DER Zwischenzertifikat>"]
}
```

- **Prima la foglia (leaf)**; la **radice non viene mai incorporata** — l'ancora di
  fiducia deve per principio provenire dall'esterno (il certificato radice
  pubblicato, fornito con gli strumenti di verifica, elencato sul sito web e nel
  repository aperto, ciascuno con fingerprint).
- Poiché l'oggetto si trova nel metablock, i certificati sono coperti dalla
  **CRC del metablock** e da **H(0)**.
- **Nota:** questo oggetto è **un dato, non una dichiarazione di profilo.** La
  dichiarazione resta esclusivamente il token di header (§2). Un file può
  contenere un oggetto `integrity.certificates` ed essere comunque al livello crc,
  se non è presente il token di header `ed25519`.

### Validità: «valido al momento della firma»

I dati di misura sopravvivono alla durata dei certificati. La semantica di
verifica è quindi: **il certificato era valido al momento della firma
(`signing_time_ns`)?** Un file registrato nel 2027 resta verificabile con esito
positivo anche nel 2040. In combinazione con le lunghe durate dei certificati dei
dispositivi, ciò è adeguato alla pratica per i dati di misura. La debolezza
teorica residua (retrodatazione con una chiave di dispositivo scaduta e sottratta)
è documentata e può, se necessario, essere eliminata con **timestamp RFC 3161**
senza modificare il formato — il timestamp è associato alla firma, non ai blocchi
dati.

### Revoca

Le revoche avvengono tramite **liste di revoca firmate** con semantica
**best-effort**; il rapporto di verifica indica esplicitamente lo stato del
controllo di revoca.

### Classi di certificati e policy di trasformazione

- I **certificati dei dispositivi** e i **certificati di organizzazione/strumenti**
  costituiscono **classi separate e distinguibili.**
- **Le trasformazioni terminano il dominio di firma — di proposito.**
  Unione, conversione ed esportazione generano per principio file senza firma del
  dispositivo. Gli strumenti si comportano in modo definito: il **risultato ricade
  al livello crc** e registra la propria **provenienza** (file sorgente con stato
  di verifica) nei metadati (`infos`). Facoltativamente un'organizzazione può
  **ri-firmare** il risultato con un proprio certificato — si tratta di una
  **dichiarazione diversa, contrassegnata di conseguenza**, rispetto alla firma del
  dispositivo.

---

## 6. Stato di verifica (vocabolario normativo per gli strumenti)

Gli strumenti di verifica riportano esattamente uno dei seguenti stati:

| Stato | Significato |
|---|---|
| `valid_signed` | interamente firmato e valido |
| `partially_signed` | firmato fino all'ancora X; il resto è valido per CRC, ma non firmato |
| `crc_valid` | il livello CRC regge; nessuna firma (valida) |
| `invalid` | un controllo CRC o di firma è fallito dove deve reggere |
| `signature_unverifiable` | le firme non possono essere verificate (ad es. libreria crittografica o certificato radice mancante) — il file resta leggibile, un'affermazione a livello CRC resta possibile |

---

## 7. Inquadramento rispetto alla DIN EN 50159 (informativo)

> Questa sezione è **informativa**. La EN 50159 considera la comunicazione
> rilevante per la sicurezza nei sistemi di trasmissione della segnalazione
> ferroviaria; il confronto viene proposto perché il profilo è rilevante per gli
> ambienti ferroviari, benché derivi dal Cyber Resilience Act e riguardi **file a
> riposo** anziché canali di trasmissione.

| Minaccia (EN 50159) | Meccanismo nel profilo OSF5 |
|---|---|
| Corruzione | CRC32C di frame per blocco; a livello crittografico: catena di hash |
| Cancellazione | Catena di hash (un blocco mancante spezza la catena) |
| Inserimento | Catena di hash e firma (un blocco estraneo spezza la catena) |
| Scambio | Catena di hash (l'ordine fa parte della formazione della catena) |
| Mascheramento | Firma Ed25519 con certificato del dispositivo e catena CA |
| Ripetizione | Numeri di sequenza delle ancore (all'interno del file); identità del file tramite **UUID del file** univoco nel metablock come ancora per la verifica a livello di sistema |
| Ritardo | non applicabile ai dati a riposo |

Si devono precisare due delimitazioni:

1. Il profilo affronta **ripetizione e cancellazione a livello di file**
   (reinserimento di vecchi file, scomparsa di interi file) deliberatamente solo
   tramite l'**interfaccia UUID del file**. L'assenza di lacune oltre i confini dei
   file è compito del **livello di sistema** sovraordinato (l'ambiente di misura,
   il suo monitoraggio delle sequenze e la sua sorgente temporale sicura) e viene
   risolta lì. Il parametro del metablock `file_uuid` è specificato in
   [`osf5.md`](osf5.md) — obbligatorio al livello signed, altrimenti
   raccomandato.
2. Il profilo è un meccanismo di **security** (nel senso di un ambiente di
   categoria 3 / reti aperte) e **non avanza alcuna pretesa di safety**: non
   sostituisce una trasmissione orientata alla sicurezza secondo EN 50159 e non
   fonda alcuna idoneità SIL.

---

## 8. Note di migrazione (informativo)

Sintetizzate dall'audit dei parser `AUDIT_INTEGRITY_O1.md` (root del repository).
Onere per implementazione implicato dal profilo:

**Tutte e quattro le implementazioni (Rust, Delphi, C++, Java)**
- Separare la CRC di frame **prima** del parser tipizzato, in base alla
  dichiarazione del profilo (framing fail-closed, §3). Oggi ogni reader scarta in
  silenzio i byte numerici in eccesso oppure assorbe l'eccedenza in un valore
  string/binary, per cui una CRC semplicemente aggiunta verrebbe ignorata o
  corromperebbe il valore.

**Delphi**
- **Irrigidire il tokenizer di header** — oggi **non** rifiuta i token finali
  sconosciuti (suddivide sullo spazio e scarta in silenzio tutto ciò che segue la
  lunghezza). Deve rifiutare secondo §2.
- **Passare i byte di controllo sconosciuti a skip-and-continue** — oggi un tipo di
  blocco sconosciuto è un soft abort, etichettato erroneamente come «troncamento»;
  deve saltare un blocco `bcIntegritySignature` tramite il campo di lunghezza e
  proseguire.

**Rust / C++ / Java**
- Rendere **intenzionale** il rifiuto dei token di header sconosciuti e migliorare
  la diagnostica («unexpected trailing token / unbekanntes Header-Token» anziché
  errore di parsing numerico). Tutti e tre saltano già correttamente i byte di
  controllo sconosciuti (valore 9) tramite il campo di lunghezza — lì non è
  necessaria alcuna modifica.

**Java**
- Pianificare il lavoro sull'integrità come **implementazione completa** (il core
  Java è un reader OSF4+OSF5 completo, non uno stub): ristrutturazione del
  tokenizer di header (onere **M**) e percorso greedy string/binary (onere **L**)
  per la separazione della CRC.

---

> Questo documento è rilasciato con licenza [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Attribuzione: optiMEAS GmbH e optiMEAS Switzerland GmbH.
