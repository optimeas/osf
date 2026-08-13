---
lang: de
title: "Integrität und Signierung in Streaming-Messdatenformaten"
subtitle: "Der OSF5-Ansatz: Korruptionserkennung, kryptografische Herkunftsnachweise und PKI-gestützte Drittprüfbarkeit für zeitbezogene Messdaten"
authors: "optiMEAS GmbH, Friedrichsdorf · optiMEAS Switzerland GmbH, Oberstammheim"
corresponding: "Burkhard Schranz"
version: "1.1"
date: "August 2026"
license: "CC BY 4.0 (Creative Commons Attribution 4.0 International)"
doi: "10.5281/zenodo.21227941"
---

> **Änderungen gegenüber Version 1.0 (Juni 2026):** Die Abschnitte 2 und 8 wurden auf
> den Umsetzungsstand von August 2026 gebracht — die CRC-Stufe ist inzwischen
> normativer Bestandteil der Spezifikation und in allen fünf Referenzimplementierungen
> umgesetzt. Der fachliche Inhalt der Abschnitte 3 bis 7 ist unverändert. Erstmals liegt
> zusätzlich eine englische Fassung vor.

## Zusammenfassung

Messdaten aus industriellen und bahntechnischen Anwendungen werden zunehmend zur
Grundlage von Abrechnungen, Gewährleistungsentscheidungen und behördlichen Nachweisen.
Gleichzeitig verpflichtet der EU Cyber Resilience Act (Verordnung (EU) 2024/2847)
Hersteller von Produkten mit digitalen Elementen zum Schutz der Datenintegrität.
Etablierte Messdatenformate (MDF4, TDMS, HDF5, CSV) bieten hierfür keine eingebauten
Mechanismen. Dieses Papier beschreibt ein dreistufiges Integritätsprofil für das Open
Streaming Format Version 5 (OSF5): eine blockgenaue CRC32C-Prüfsumme zur
Korruptionserkennung, eine SHA-256-Hash-Kette mit periodischen
Ed25519-Signaturankern für streaming-taugliche Manipulationserkennung und
Herkunftsnachweise, sowie ein X.509-basiertes Zertifikatsmodell, das die Prüfung durch
unbeteiligte Dritte offline und ohne Rückfrage beim Hersteller ermöglicht. Das Design
bleibt vollständig abwärtskompatibel, erhält die Stromausfall-Robustheit des Formats
und wird gegen den Bedrohungskatalog der DIN EN 50159 eingeordnet. Die Veröffentlichung
dient zugleich als defensive Publikation: Die beschriebenen Verfahren sollen frei
implementierbar bleiben.

## 1. Motivation

Streaming-Messdatenformate für Embedded-Systeme lösen seit Jahren dieselben
Kernprobleme: verlustfreie kontinuierliche Aufzeichnung, Robustheit gegen Stromausfall,
effiziente blockweise Verarbeitung. Was sie nicht beantworten, sind zwei Fragen, die
durch Regulierung und Datenökonomie drängend werden:

*Ist diese Datei unverändert?* — Bitfehler auf Speichermedien, abgebrochene
Übertragungen und fehlerhafte Werkzeugketten verfälschen Daten unbemerkt.

*Stammt diese Datei wirklich von diesem Gerät?* — Sobald Messdaten Geld oder Haftung
bewegen (Energieabrechnung, Gewährleistung im Schienenfahrzeugbereich,
Zustandsüberwachung kritischer Infrastruktur), genügt die Behauptung der Herkunft nicht
mehr; sie muss beweisbar sein — idealerweise gegenüber Dritten, die dem Betreiber nicht
vertrauen müssen.

Der EU Cyber Resilience Act verlangt in Anhang I den Schutz der Integrität von
gespeicherten, übertragenen und verarbeiteten Daten gegen unbefugte Manipulation. Für
Messgerätehersteller ist ein Datenformat, das Integritätsschutz eingebaut mitbringt,
damit ein unmittelbarer Compliance-Baustein.

Ein Format-Vergleich zeigt die Lücke: MDF4, TDMS, HDF5 und CSV besitzen keine
eingebaute Integritätsprüfung (HDF5 optional Fletcher32 als Filter, ohne
Herkunftsnachweis). Apache Parquet bietet Manipulationserkennung über
AES-GCM-verschlüsselte Spalten — gekoppelt an symmetrische Verschlüsselung, wodurch die
Aussage „unverändert" nur gegenüber Schlüsselinhabern gilt und keine für Dritte prüfbare
Herkunft entsteht. Der hier beschriebene Ansatz schließt genau diese Lücke mit
asymmetrischen Signaturen und öffentlicher Zertifikatsprüfung.

## 2. Das Open Streaming Format in Kürze

OSF ist ein binäres, blockorientiertes Format für zeitbezogene Mess- und Prozessdaten
(optiMEAS GmbH; Spezifikation und Implementierungen offen unter
github.com/optimeas/osf). Eine Datei besteht aus einer ASCII-Header-Zeile
(`OSF5 <Metablock-Länge>\n`), einem JSON-Metablock (Kanäle, Datentypen, Metadaten) und
einem Strom in sich abgeschlossener Datenblöcke. Jeder Block trägt Kanalindex,
Längenfeld und Steuerbyte; die Datei ist append-only und bleibt bei hartem Abbruch bis
zum letzten vollständigen Block lesbar. Diese Eigenschaften — insbesondere die
Selbstabgeschlossenheit der Blöcke — sind die Grundlage, auf der sich Integrität
streaming-tauglich ergänzen lässt.

Normative Quelle für das Basisformat ist die veröffentlichte OSF-Spezifikation
(github.com/optimeas/osf, Verzeichnis `docs/`; identisch unter docs.optimeas.com). Die
in Abschnitt 4 beschriebene CRC-Stufe ist dort seit der Revision vom 2026-07-07
normativer Bestandteil. Die Signatur- und die PKI-Stufe (Abschnitte 5 und 6) werden hier
erstmals vollständig beschrieben und fließen in eine kommende Revision derselben
Spezifikation ein; diese Abschnitte sind daher bewusst selbsttragend formuliert.

## 3. Das dreistufige Integritätsprofil

Das Profil ist als streng geordnete Leiter definiert; jede Stufe enthält die
darunterliegende. Die Stufe gilt je Datei und wird genau einmal deklariert.

| Stufe | Schützt gegen | Typischer Einsatz |
|---|---|---|
| ohne | — | Labor, Zwischendateien, minimale Embedded-Writer |
| CRC | Korruption (Bitfehler, Truncation, Medienfehler) | Standard für Felddaten |
| Signiert | Korruption und Manipulation; für Dritte beweisbare Herkunft | Betriebsdaten mit Beweiswert |

Zwei Designregeln verhindern eine Fragmentierung des Formats: Schreiber wählen die Stufe
frei (Embedded-Systeme behalten die Option minimaler Writer), aber jeder konforme Leser
muss alle Stufen verarbeiten — es entsteht ein Format mit Profil, nicht drei Dialekte.
Und die Granularität ist stets die ganze Datei: alle Blöcke tragen den Mechanismus oder
keiner.

## 4. Stufe CRC: blockgenaue Korruptionserkennung

**Deklaration im Header.** Die Header-Zeile wird um ein optionales Token erweitert:

```
OSF5 84512 crc32c:9A3F01BC\n
```

Das Token deklariert den Algorithmus für die gesamte Datei und trägt zugleich die
CRC32C-Prüfsumme des Metablocks — des einzigen Dateiteils, der sich nicht selbst
schützen kann. Die Verifikationsreihenfolge ist damit deterministisch: Token lesen,
Metablock-CRC prüfen, Metablock parsen, Datenblöcke prüfen.

**Fail-closed-Semantik.** Header-Tokens sind als „must understand" definiert: Ein Leser,
der ein Token nicht kennt, lehnt die Datei ab. Diese Strenge ist kein Stilmittel,
sondern zwingend — bei Datentypen variabler Länge (Strings, Binärdaten) ist die Nutzlast
allein durch das Längenfeld definiert; ein integritäts-unbewusster Leser würde
angehängte Prüfsummenbytes als Nutzdaten ausliefern und damit stille Datenkorruption
erzeugen. Fail-open scheidet konstruktiv aus.

**Frame-Prüfsumme je Block.** Jeder Datenblock erhält eine CRC32C (Castagnoli;
hardwarebeschleunigt auf x86/SSE4.2 und ARMv8) über den gesamten Frame — Kanalindex,
Längenfeld, Steuerbyte und Nutzdaten. Der Scope schließt bewusst das Längenfeld ein: Ein
gekipptes Längenfeld ist der schwerste Einzelfehler, weil der Leser danach die
Blockgrenzen verliert. Die Prüfsumme bildet die letzten vier Bytes des Datenbereichs und
wird im Längenfeld mitgezählt; das Framing bleibt dadurch für jeden Leser intakt, die
effektive Nutzlänge ist Blocklänge minus vier.

Aufbau eines Datenblocks ohne und mit aktivem Profil (LEN = Wert des Längenfelds):

```
ohne Profil:   [Kanalindex][Längenfeld][Steuerbyte][ Nutzdaten ................ ]
                                                   |<-------------- LEN ----------------->|
mit  Profil:   [Kanalindex][Längenfeld][Steuerbyte][ Nutzdaten ....... ][CRC32C]
                                                   |<-------------- LEN ----------------->|
               |<================ Scope der Frame-CRC ========================>|
```

**Fehlerverhalten.** Ein CRC-Fehler in einem Datenblock invalidiert genau diesen Block;
der Leser überspringt ihn, zählt ihn in der Lesestatistik und fährt fort — partielle
Daten sind besser als keine. Ein Metablock-CRC-Fehler führt dagegen zur Ablehnung der
Datei, da ohne vertrauenswürdigen Metablock nichts interpretierbar ist.

## 5. Stufe Signiert: Hash-Kette mit Signaturankern

**Asymmetrisch statt symmetrisch.** Ein HMAC-Ansatz scheidet aus: Wer prüfen kann,
könnte auch fälschen. Das passende Modell für Gerätedaten ist die digitale Signatur —
der private Schlüssel verbleibt im Gerät, geprüft wird mit dem öffentlichen. Gewählt ist
Ed25519 (32-Byte-Schlüssel, 64-Byte-Signaturen, deterministisch, RFC 8410) mit SHA-256
als Kettenhash. Die CRC-Ebene bleibt dabei vollständig erhalten: Sie erlaubt den
schnellen blockweisen Korruptions-Check ohne Kryptografie; die Signaturebene liefert die
Beweisbarkeit.

**Streaming-taugliche Konstruktion.** Eine einzelne Signatur beim Dateischluss wäre mit
dem Streaming-Prinzip unvereinbar (bei Stromausfall fehlte sie vollständig); eine
Signatur je Block wäre unverhältnismäßig teuer. Die Lösung ist eine laufende Hash-Kette
mit periodischen Ankern:

```
H(0) = SHA256(Header-Zeile ‖ Metablock)
H(i) = SHA256(H(i−1) ‖ Frame_i)
```

In konfigurierbarer Kadenz (zeit- oder blockbasiert, zusätzlich beim Schließen) schreibt
der Writer einen Signaturblock — einen neuen Steuerbyte-Typ — mit fortlaufender
Anker-Sequenznummer, aktuellem Kettenhash, Ed25519-Signatur darüber und Schlüssel- bzw.
Zertifikatsreferenz. Der erste Anker deckt damit Header und Metablock mit ab;
Signaturblöcke tragen selbst eine Frame-CRC wie jeder andere Block.

**Ehrliche Stromausfall-Semantik.** Bricht die Aufzeichnung hart ab, ist alles bis zum
letzten Anker signiert; der Schwanz danach ist CRC-valide, aber unsigniert. Der
Verifikationsbericht benennt genau das („signiert bis Zeitstempel X"). Die Kette erkennt
über die Prüfung einzelner Blöcke hinaus auch Löschung, Einfügung und Umordnung von
Blöcken; die Anker-Sequenznummern erkennen Wiederholung innerhalb der Datei.

**Transformationen beenden die Signaturdomäne — mit Absicht.** Zusammenführen,
Konvertieren und Exportieren erzeugen prinzipbedingt Dateien ohne Gerätesignatur.
Werkzeuge verhalten sich dabei definiert: Das Ergebnis fällt auf die CRC-Stufe zurück
und protokolliert die Herkunft (Quelldateien samt Prüfstatus) in den Metadaten. Optional
kann eine Organisation das Ergebnis mit einem eigenen Zertifikat re-signieren — das ist
eine andere, entsprechend gekennzeichnete Aussage als die Gerätesignatur.

## 6. PKI: Herkunftsnachweis für Dritte

**Zertifikate statt Schlüsselverzeichnis.** Damit nicht nur der Hersteller, sondern jeder
Dritte die Herkunft prüfen kann, werden Geräteschlüssel von einer Zertifizierungsstelle
beglaubigt (X.509 mit Ed25519 nach RFC 8410) — analog zum Vertrauensmodell von HTTPS,
jedoch als private, öffentlich dokumentierte Hierarchie: eine Wurzel-CA (offline,
hardwaregeschützt), eine ausstellende CA (angebunden an Produktion bzw. Geräte-Cloud),
darunter Gerätezertifikate, deren Subject die Geräteseriennummer trägt.
Gerätezertifikate und Organisations-/Werkzeugzertifikate (Abschnitt 5) bilden getrennte,
unterscheidbare Klassen.

**Die Datei trägt ihre Kette.** Gerätezertifikat und Zwischenzertifikat werden einmal je
Datei eingebettet; die Signaturblöcke referenzieren sie. Das Wurzelzertifikat wird nie
eingebettet — der Vertrauensanker muss prinzipbedingt von außen kommen. Damit ist eine
Datei ein in sich geschlossenes Beweisstück: Die Prüfung funktioniert offline, ohne
Verzeichnisabfrage und ohne Mitwirkung des Herstellers; benötigt wird allein das
öffentlich publizierte Wurzelzertifikat (Website und offenes Repository, jeweils mit
Fingerprint), das in den Prüfwerkzeugen mitgeliefert wird.

**Gültigkeit zum Signaturzeitpunkt.** Messdaten überdauern Zertifikatslaufzeiten. Die
Verifikationssemantik lautet daher: *War das Zertifikat zum Signaturzeitpunkt gültig?* —
eine 2027 aufgezeichnete Datei bleibt auch 2040 positiv prüfbar. In Verbindung mit
langen Gerätezertifikats-Laufzeiten ist das für Messdaten praxisgerecht; die theoretische
Restschwäche (Rückdatierung mit einem entwendeten, abgelaufenen Geräteschlüssel) ist
dokumentiert und kann bei Bedarf durch RFC-3161-Zeitstempel geschlossen werden, ohne das
Format zu ändern — der Zeitstempel hängt an der Signatur, nicht an den Datenblöcken.
Sperrungen erfolgen über signierte Sperrlisten mit Best-Effort-Semantik; der Prüfbericht
weist den Stand der Sperrprüfung explizit aus.

## 7. Einordnung gegenüber DIN EN 50159

Die EN 50159 betrachtet sicherheitsrelevante Kommunikation in Übertragungssystemen der
Bahnsignaltechnik und definiert einen Katalog von Grundbedrohungen. Obwohl das hier
beschriebene Profil aus dem Cyber Resilience Act hergeleitet ist und ruhende Dateien
statt Übertragungskanäle adressiert, lohnt die Gegenüberstellung — gerade für den
Einsatz in Bahnumgebungen:

| Bedrohung (EN 50159) | Mechanismus im OSF5-Profil |
|---|---|
| Verfälschung | Frame-CRC32C je Block; kryptografisch: Hash-Kette |
| Löschung | Hash-Kette (fehlender Block bricht die Kette) |
| Einfügung | Hash-Kette und Signatur (fremder Block bricht die Kette) |
| Vertauschung | Hash-Kette (Reihenfolge ist Teil der Kettenbildung) |
| Maskerade | Ed25519-Signatur mit Gerätezertifikat und CA-Kette |
| Wiederholung | Anker-Sequenznummern (innerhalb der Datei); Datei-Identität via eindeutiger Datei-UUID im Metablock als Anker für Systemebenen-Prüfung |
| Verzögerung | für ruhende Daten nicht anwendbar |

Zwei Abgrenzungen sind festzuhalten. Erstens adressiert das Profil Wiederholung und
Löschung *auf Dateiebene* (erneutes Einspielen alter Dateien, Verschwinden ganzer
Dateien) bewusst nur über die Schnittstelle Datei-UUID: Lückenlosigkeit über
Dateigrenzen hinweg ist Aufgabe der übergeordneten Systemebene — der Messumgebung, ihrer
Sequenzüberwachung und sicheren Zeitquelle — und wird dort gelöst. Zweitens gilt strikt:
Das Profil ist ein Security-Mechanismus im Sinne der Kategorie-3-Umgebung (offene Netze)
und erhebt keinen Safety-Anspruch; es ersetzt keine sicherheitsgerichtete Übertragung
nach EN 50159 und begründet keine SIL-Eignung.

## 8. Kompatibilität, Aufwand, Implementierungen

Das Profil ist eine Revision der OSF5-Definition, kein neues Format: Dateien ohne Token
bleiben unverändert gültig; die Vorgängerversion OSF4 ist nicht betroffen. Der
Laufzeit-Overhead ist gering (CRC32C im GB/s-Bereich, SHA-256 im Bereich hunderter
MB/s, eine Ed25519-Signatur im Mikrosekundenbereich je Anker); der Speicher-Overhead
beträgt vier Bytes je Block, rund 100–120 Bytes je Signaturanker und einmalig etwa ein
bis zwei Kilobyte Zertifikatskette je Datei.

Offene Implementierungen des Formats existieren in Rust (Kern, mit Python-Anbindung),
C++, Delphi und Java unter MIT-Lizenz; die Formatspezifikation steht unter CC BY 4.0.
Die Integritätsstufen werden gestuft umgesetzt: Die CRC-Stufe ist in allen fünf
Implementierungen fertiggestellt und durch einen gemeinsamen Satz Referenzdateien
abgesichert, den die Konformitätstests der Implementierungen gemeinsam prüfen.
Signierung und PKI folgen.
Referenz- und Negativtestdaten (gekippte Bytes, gebrochene Ketten, abgelaufene und
gesperrte Zertifikate) sind Teil der Testsuite; eine strikt getrennte Test-PKI stellt
sicher, dass Testzertifikate nie produktive Prüfergebnisse erzeugen.

## 9. Schluss

Streaming-Robustheit und kryptografische Integrität schließen sich nicht aus — sie
verstärken einander, wenn die Mechanismen entlang der Blockstruktur des Formats
entworfen werden: Prüfsummen je Frame, Ketten über Frames, Signaturanker im Strom,
Zertifikate in der Datei. Der Beitrag dieses Papiers ist die Kombination dieser
bekannten Bausteine zu einem abgestuften, abwärtskompatiblen und für Dritte offline
prüfbaren Integritätsprofil für Streaming-Messdaten. Die Verfahren sind zur freien
Implementierung veröffentlicht; die Autoren begrüßen Rückmeldungen aus der Praxis,
insbesondere aus dem bahntechnischen Umfeld.

**Lizenz.** Dieses Werk ist lizenziert unter Creative Commons Attribution 4.0
International (CC BY 4.0). Namensnennung: „optiMEAS GmbH und optiMEAS Switzerland
GmbH".

**Kontakt.** optiMEAS GmbH, Friedrichsdorf · optiMEAS Switzerland GmbH, Oberstammheim ·
github.com/optimeas/osf · docs.optimeas.com
