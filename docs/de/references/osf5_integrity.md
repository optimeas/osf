---
title: OSF5-Integritätsprofil
description: Normative Spezifikation des optionalen OSF5-Integritätsprofils — CRC32C-Korruptionserkennung, Ed25519-Signaturkette und X.509-/PKI-Herkunftsnachweis
image: "/img/om_social_card.png"
keywords:
  - OSF5
  - Integrität
  - CRC32C
  - Ed25519
  - Signatur
  - PKI
last_update:
  date: 2026-07-08
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

🇬🇧 [English version](../../en/references/osf5_integrity.md)

# OSF5-Integritätsprofil

Dieses Dokument ist die **normative** Spezifikation des optionalen
**Integritätsprofils** für das Open Streaming Format Version 5 (OSF5). Es ergänzt
das Basisformat um drei gestufte Garantien: blockgenaue Korruptionserkennung
(CRC32C), streaming-taugliche Manipulationserkennung und Herkunftsnachweise
(eine SHA-256-Hash-Kette mit periodischen Ed25519-Signaturankern) sowie eine
offline durch Dritte prüfbare Verifizierbarkeit (ein X.509-/PKI-Zertifikatsmodell).

Das Profil ist eine **reine OSF5-Eigenschaft**. OSF4-Dateien sind nicht betroffen
und tragen niemals einen Teil davon. Das Design ist vollständig abwärtskompatibel:
OSF5-Dateien *ohne* Integritätsdeklaration bleiben unverändert gültig.

Konzept und Begründung des Profils sind als Konzeptpapier veröffentlicht (Zenodo,
DOI [10.5281/zenodo.21227942](https://doi.org/10.5281/zenodo.21227942), auch in
diesem Repository unter [`docs/papers/`](https://github.com/optimeas/osf/tree/main/docs/papers/)). Das Papier liefert den
Hintergrund; **dieses Dokument ist die normative Referenz** für Implementierer.

Das Basisformat (Magic Header, JSON-Metablock, in sich abgeschlossene
Datenblöcke) ist in [`osf_general.md`](../osf_general.md) und
[`osf5.md`](osf5.md) beschrieben; dieses Dokument ergänzt lediglich die
Integritätsebene.

---

## 1. Drei-Stufen-Modell

Das Profil ist eine **streng geordnete Leiter**; jede Stufe enthält die
darunterliegende:

```
none  ⊂  crc  ⊂  signed
```

Die Stufe gilt **je Datei** und wird **genau einmal** deklariert.

| Stufe | Schützt gegen | Typischer Einsatz |
|---|---|---|
| **none** | — | Labor, Zwischendateien, minimale Embedded-Writer |
| **crc** | Korruption (Bitfehler, Truncation, Medienfehler) | Standard für Felddaten |
| **signed** | Korruption **und** Manipulation; für Dritte beweisbare Herkunft | Betriebsdaten mit Beweiswert |

Zwei Designregeln verhindern eine Fragmentierung des Formats:

1. **Schreiber wählen die Stufe frei** — Embedded-Systeme behalten die Option
   eines minimalen Writers.
2. **Jeder konforme OSF5-Leser MUSS alle Stufen verarbeiten.** Es entsteht ein
   Format mit Profil, nicht drei Dialekte.

Die Granularität ist stets die **ganze Datei**: entweder tragen alle Blöcke den
Mechanismus oder keiner.

---

## 2. Header-Token-Grammatik (normativ)

Die Magic-Header-Zeile wird nach der Metablock-Länge um null oder mehr optionale
Tokens erweitert:

```abnf
header-line     = identifier SP metablock-len *(SP token) LF
token           = key ":" value
key             = 1*(a-z / 0-9 / "-")          ; nur Kleinbuchstaben
value           = 1*(VCHAR ohne SP)
```

- Genau **ein** `SP` (0x20) trennt die Felder; vor dem `LF` steht **kein
  abschließendes Leerzeichen**.
- Tokens sind **„must understand"**: Ein Leser, der einen `key` nicht kennt,
  **MUSS die Datei ablehnen**, mit einer Diagnose der Form
  `unbekanntes Header-Token '<key>'` — **nicht** als Zahlenparsfehler. (Heutige
  Parser lehnen ein unerwartetes Token nur beiläufig ab, über eine irreführende
  „Länge ist keine gültige Zahl"-Meldung; siehe Migrationsanhang.)
- Header-Tokens sind ein **OSF5-only**-Merkmal. Die OSF4-Kennungen (`OSF4`,
  `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) DÜRFEN keine Tokens tragen;
  ein Token nach einer OSF4-Kennung ist eine fehlerhafte Datei.

### In dieser Revision definierte Keys

| Key | Stufe | Wert |
|---|---|---|
| `crc32c` | crc | `crc32c:<8 HEXDIG Großbuchstaben>` — die CRC32C (Castagnoli) über die **rohen Metablock-Bytes** exakt wie in der Datei |
| `ed25519` | signed | `ed25519:<keyid>` — `keyid` = 16 HEXDIG **Kleinbuchstaben** = die ersten 8 Bytes von SHA-256 über den DER-kodierten `SubjectPublicKeyInfo` des Gerätezertifikats |

- **Hex-Casing ist strikt**: `crc32c` verwendet **Großbuchstaben**-Hex; die
  `ed25519`-`keyid` verwendet **Kleinbuchstaben**-Hex. Leser MÜSSEN das Casing
  strikt prüfen.
- Das `ed25519`-Token ist nur **zusätzlich zu** `crc32c` zulässig und nur in
  dieser Reihenfolge — `crc32c` zuerst, `ed25519` danach. `signed` impliziert
  damit stets `crc`.
- Der Metablock ist der einzige Dateiteil, der sich nicht selbst schützen kann;
  das `crc32c`-Token trägt seine Prüfsumme, damit die gesamte
  Verifikationsreihenfolge deterministisch ist.

### Verifikationsreihenfolge (normativ)

1. Header-Tokens lesen.
2. Metablock-CRC prüfen (aus dem `crc32c`-Token).
3. Metablock parsen.
4. Datenblöcke prüfen (Frame-CRCs und — auf Stufe signed — die Signaturkette).

---

## 3. Frame-CRC (Stufe crc)

Auf Stufe crc trägt **jeder** Block eine **CRC32C** (Castagnoli;
hardwarebeschleunigt auf x86/SSE4.2 und ARMv8) über den **gesamten Frame**:
Kanalindex, Längenfeld, Steuerbyte und Nutzdaten.

Das **Längenfeld** bewusst in den Scope aufzunehmen ist wichtig: Ein gekipptes
Längenfeld ist der schwerste Einzelfehler, weil der Leser danach alle
Blockgrenzen verliert.

Die CRC bildet die **letzten vier Bytes des Datenbereichs** und wird **im
Längenfeld mitgezählt**, sodass das Framing für jeden Leser intakt bleibt. Die
effektive Nutzlänge ist die Blocklänge **minus vier**.

```
ohne Profil:   [Kanalindex][Längenfeld][Steuerbyte][ Nutzdaten ................ ]
                                       |<------------- LEN ------------------>|

mit  Profil:   [Kanalindex][Längenfeld][Steuerbyte][ Nutzdaten ....... ][CRC32C]
                                       |<------------- LEN ------------------>|
               |<================ Scope der Frame-CRC ====================>|
```

### Normative Umsetzungsvorgabe (Fail-closed-Framing)

Bei aktivem Profil ist die Frame-CRC **Teil des Framings** und MUSS **vor** der
typisierten Auswertung der Nutzdaten abgetrennt werden. Eine *nachgelagerte*
„Rest == 0"-Prüfung nach dem Dekodieren **genügt nicht**: Bei Datentypen
variabler Länge (`string`, `binary`) kann ein integritäts-unbewusster Leser
angehängte CRC-Bytes nicht von echter Nutzlast unterscheiden und würde
korrumpierte Werte ausliefern. Fail-open scheidet damit konstruktiv aus — der
Leser trennt die letzten vier Bytes anhand der Profil-Kenntnis aus
Header/Metablock ab und verifiziert sie.

### Fehlerverhalten

| Bedingung | Reaktion |
|---|---|
| Datenblock-CRC-Fehler | der Block ist **ungültig** → überspringen, **in der Lesestatistik zählen** und fortfahren (partielle Daten sind besser als keine) |
| Metablock-CRC-Fehler | **Datei ablehnen** (ohne vertrauenswürdigen Metablock ist nichts interpretierbar) |
| Unbekanntes Header-Token | **Datei ablehnen** (siehe §2) |

**Empfehlung an Implementierungen:** Zusätzlich zum verpflichtenden Abtrennen
eine strikte Vollkonsum-Prüfung für **numerische** Blöcke (`N × Samplegröße
(+ Headerfelder)` muss der effektiven Nutzlänge entsprechen). Das ist eine
Verbesserung der Diagnosequalität, keine Korrektheitsvorgabe.

---

## 4. Signaturblock `bcIntegritySignature = 9` (Stufe signed)

Stufe signed ergänzt einen neuen Steuerbyte-Typ und eine laufende Hash-Kette.

### Der Block

- Neuer Steuerbyte-Wert **9** (`bcIntegritySignature`); nur gültig, wenn die
  Datei die Stufe **signed** deklariert; **Bit 7 = 0** (Einzelwert-Semantik).
- **Kanalindex: der reservierte Wert `0xFFFE`** — ein dateiweiter
  Integritätskanal, der **nicht** im Metablock deklariert wird. Leser ohne
  Stufe-signed-Unterstützung überspringen den Block per Längenfeld, genau wie
  jeden anderen unbekannten Blocktyp (siehe §5). `0xFFFE` ist verschieden vom
  `0xFFFF`-Info-/Trailer-Kanal aus OSF4.
- Blöcke auf dem reservierten Kanal `0xFFFE` verwenden **stets ein 4-Byte-
  Längenfeld (`uint32`)**, unabhängig von Kanaldeklarationen — analog zum
  historischen `0xFFFF`-Infoblock.
- Signaturblöcke tragen selbst eine **Frame-CRC** wie jeder andere Block.

### Payload (little-endian; Reihenfolge normativ)

| # | Feld | Typ | Bedeutung |
|---|---|---|---|
| 1 | `anchor_seq` | `uint32` | Anker-Sequenznummer, fortlaufend ab 0 |
| 2 | `signing_time_ns` | `int64` | Signaturzeitpunkt (Basis der Gültigkeitssemantik) |
| 3 | `chain_hash` | `byte[32]` | H(i), der aktuelle Kettenhash |
| 4 | `signature` | `byte[64]` | Ed25519 über `SHA-256(anchor_seq ‖ signing_time_ns ‖ chain_hash)`, die Felder in Wire-Reihenfolge / -Kodierung |
| 5 | `keyid_len` + `keyid` | `uint8` + `byte[keyid_len]` | Schlüssel-/Zertifikatsreferenz; `keyid` wie im Header-Token (8 Bytes) |

### Hash-Kette

```
H(0) = SHA256(Header-Zeile ‖ Metablock)
H(i) = SHA256(H(i−1) ‖ Frame_i)
```

- Frames gehen **inklusive** ihrer Frame-CRC in die Kette ein.
- **Signaturblöcke selbst gehen ebenfalls in die Kette ein** (als ein `Frame_i`).
- Der **erste Anker deckt damit auch Header und Metablock** über H(0) mit ab.

Die Kette erkennt über die Prüfung einzelner Blöcke hinaus auch **Löschung**,
**Einfügung** und **Umordnung** von Blöcken; die Anker-Sequenznummern erkennen
**Wiederholung innerhalb der Datei**.

### Kadenz

Konfigurierbar (zeit- oder blockbasiert). Der **normative Default ist zeitbasiert,
10 Sekunden**. Zusätzlich ist ein Anker bei **regulärem Dateischluss
verpflichtend**, sodass eine sauber geschlossene Datei vollständig signiert ist.

### Stromausfall-Semantik (normativ)

Bricht die Aufzeichnung hart ab, ist die Datei **signiert bis zum letzten
gültigen Anker**; der Schwanz danach ist **CRC-valide, aber unsigniert**. Der
Verifikationsbericht MUSS beides benennen (z. B. „signiert bis Zeitstempel X,
Rest CRC-valide, unsigniert").

---

## 5. Zertifikate und PKI (Stufe signed)

Damit **jeder Dritte** — nicht nur der Hersteller — die Herkunft prüfen kann,
werden Geräteschlüssel von einer Zertifizierungsstelle beglaubigt (X.509 mit
Ed25519 nach RFC 8410), analog zum Vertrauensmodell von HTTPS, jedoch als
private, öffentlich dokumentierte Hierarchie: eine offline hardwaregeschützte
**Wurzel-CA**, eine ausstellende CA (angebunden an Produktion bzw. Geräte-Cloud)
und darunter Gerätezertifikate, deren Subject die Geräteseriennummer trägt.

### Einbettungsort: der Metablock

Die Zertifikatskette wird einmal je Datei als neues **optionales Objekt auf der
`osf`-Ebene** des Metablocks eingebettet:

```json
"integrity": {
  "certificates": ["<base64 DER Gerätezertifikat>", "<base64 DER Zwischenzertifikat>"]
}
```

- **Leaf zuerst**; die **Wurzel wird niemals eingebettet** — der Vertrauensanker
  muss prinzipbedingt von außen kommen (das öffentlich publizierte
  Wurzelzertifikat, mit den Prüfwerkzeugen ausgeliefert, auf Website und im
  offenen Repository gelistet, jeweils mit Fingerprint).
- Da das Objekt im Metablock liegt, sind die Zertifikate von der
  **Metablock-CRC** und von **H(0)** abgedeckt.
- **Hinweis:** Dieses Objekt ist **Daten, keine Profil-Deklaration.** Die
  Deklaration bleibt allein das Header-Token (§2). Eine Datei kann ein
  `integrity.certificates`-Objekt tragen und dennoch auf Stufe crc sein, wenn
  kein `ed25519`-Header-Token vorhanden ist.

### Gültigkeit: „gültig zum Signaturzeitpunkt"

Messdaten überdauern Zertifikatslaufzeiten. Die Verifikationssemantik lautet
daher: **War das Zertifikat zum Signaturzeitpunkt (`signing_time_ns`) gültig?**
Eine 2027 aufgezeichnete Datei bleibt auch 2040 positiv prüfbar. In Verbindung
mit langen Gerätezertifikats-Laufzeiten ist das für Messdaten praxisgerecht. Die
theoretische Restschwäche (Rückdatierung mit einem entwendeten, abgelaufenen
Geräteschlüssel) ist dokumentiert und kann bei Bedarf durch **RFC-3161-
Zeitstempel** geschlossen werden, ohne das Format zu ändern — der Zeitstempel
hängt an der Signatur, nicht an den Datenblöcken.

### Sperrung

Sperrungen erfolgen über **signierte Sperrlisten** mit **Best-Effort**-Semantik;
der Prüfbericht weist den Stand der Sperrprüfung explizit aus.

### Zertifikatsklassen und Transformations-Policy

- **Gerätezertifikate** und **Organisations-/Werkzeugzertifikate** bilden
  **getrennte, unterscheidbare Klassen.**
- **Transformationen beenden die Signaturdomäne — mit Absicht.**
  Zusammenführen, Konvertieren und Exportieren erzeugen prinzipbedingt Dateien
  ohne Gerätesignatur. Werkzeuge verhalten sich definiert: Das **Ergebnis fällt
  auf Stufe crc zurück** und protokolliert seine **Herkunft** (Quelldateien samt
  Prüfstatus) in den Metadaten (`infos`). Optional kann eine Organisation das
  Ergebnis mit einem eigenen Zertifikat **re-signieren** — das ist eine
  **andere, entsprechend gekennzeichnete Aussage** als die Gerätesignatur.

---

## 6. Verifikationsstatus (normatives Vokabular für Werkzeuge)

Prüfwerkzeuge melden genau einen der folgenden Status:

| Status | Bedeutung |
|---|---|
| `valid_signed` | vollständig signiert und gültig |
| `partially_signed` | signiert bis Anker X; der Rest ist CRC-valide, aber unsigniert |
| `crc_valid` | CRC-Stufe hält; keine (gültige) Signatur |
| `invalid` | eine CRC- oder Signaturprüfung ist fehlgeschlagen, wo sie halten muss |
| `signature_unverifiable` | Signaturen können nicht geprüft werden (z. B. Kryptobibliothek oder Wurzelzertifikat fehlt) — die Datei bleibt lesbar, eine CRC-Stufen-Aussage bleibt möglich |

---

## 7. Einordnung gegenüber DIN EN 50159 (informativ)

> Dieser Abschnitt ist **informativ**. Die EN 50159 betrachtet
> sicherheitsrelevante Kommunikation in Übertragungssystemen der
> Bahnsignaltechnik; die Gegenüberstellung wird angeboten, weil das Profil für
> Bahnumgebungen relevant ist, obwohl es aus dem Cyber Resilience Act hergeleitet
> ist und **ruhende Dateien** statt Übertragungskanäle adressiert.

| Bedrohung (EN 50159) | Mechanismus im OSF5-Profil |
|---|---|
| Verfälschung | Frame-CRC32C je Block; kryptografisch: Hash-Kette |
| Löschung | Hash-Kette (ein fehlender Block bricht die Kette) |
| Einfügung | Hash-Kette und Signatur (ein fremder Block bricht die Kette) |
| Vertauschung | Hash-Kette (die Reihenfolge ist Teil der Kettenbildung) |
| Maskerade | Ed25519-Signatur mit Gerätezertifikat und CA-Kette |
| Wiederholung | Anker-Sequenznummern (innerhalb der Datei); Datei-Identität via eindeutiger **Datei-UUID** im Metablock als Anker für die Systemebenen-Prüfung |
| Verzögerung | für ruhende Daten nicht anwendbar |

Zwei Abgrenzungen sind festzuhalten:

1. Das Profil adressiert **Wiederholung und Löschung auf Dateiebene** (erneutes
   Einspielen alter Dateien, Verschwinden ganzer Dateien) bewusst nur über die
   **Schnittstelle Datei-UUID**. Lückenlosigkeit über Dateigrenzen hinweg ist
   Aufgabe der übergeordneten **Systemebene** (der Messumgebung, ihrer
   Sequenzüberwachung und sicheren Zeitquelle) und wird dort gelöst. Der
   `file_uuid`-Metablock-Parameter ist in [`osf5.md`](osf5.md) spezifiziert —
   Pflicht auf Stufe signed, sonst empfohlen.
2. Das Profil ist ein **Security**-Mechanismus (im Sinne einer Kategorie-3- /
   offene-Netze-Umgebung) und erhebt **keinen Safety-Anspruch**: Es ersetzt keine
   sicherheitsgerichtete Übertragung nach EN 50159 und begründet keine
   SIL-Eignung.

---

## 8. Migrationshinweise (informativ)

Verdichtet aus dem Parser-Audit `AUDIT_INTEGRITY_O1.md` (Repo-Root). Je
Implementierung der vom Profil implizierte Aufwand:

**Alle vier Implementierungen (Rust, Delphi, C++, Java)**
- Die Frame-CRC **vor** dem typisierten Parser abtrennen, gesteuert durch die
  Profil-Deklaration (Fail-closed-Framing, §3). Jeder Leser verwirft heute
  überzählige numerische Bytes still oder absorbiert Überzähliges in einen
  string/binary-Wert, sodass eine naive angehängte CRC ignoriert würde oder den
  Wert korrumpiert.

**Delphi**
- **Header-Tokenizer verschärfen** — er lehnt heute unbekannte nachgestellte
  Tokens **nicht** ab (er splittet am Leerzeichen und verwirft alles nach der
  Länge still). Muss gemäß §2 ablehnen.
- **Unbekannte Steuerbytes auf Skip-and-continue umstellen** — heute ist ein
  unbekannter Blocktyp ein Soft-Abort, mislabeled als „Truncation"; er muss einen
  `bcIntegritySignature`-Block per Längenfeld überspringen und fortfahren.

**Rust / C++ / Java**
- Die Ablehnung unbekannter Header-Tokens **beabsichtigt** machen und die
  Diagnose verbessern („unexpected trailing token / unbekanntes Header-Token"
  statt Zahlenparsfehler). Alle drei überspringen unbekannte Steuerbytes (Wert 9)
  bereits sauber per Längenfeld — dort ist keine Änderung nötig.

**Java**
- Die Integritätsarbeit als **vollwertige Implementierung** einplanen (der
  Java-Kern ist ein vollständiger OSF4+OSF5-Leser, kein Stub): Header-Tokenizer-
  Umbau (Aufwand **M**) und der greedy string/binary-Pfad (Aufwand **L**) für das
  CRC-Abtrennen.

---

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
