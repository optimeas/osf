---
title: Fehlerbehandlung
description: Die Ausnahme-Hierarchie OsfException, der vollständige Fehlerkatalog, das Best-Effort-Lesemodell und der Integritätsstatus in ReaderStats
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Java
  - OsfException
  - Best-Effort
  - Integrität
last_update:
  date: 2026-07-11
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

# Fehlerbehandlung

Die Java-Implementierung meldet Fehler über **Exceptions**. Alle
Bibliotheksfehler sind Instanzen von `OsfException`, das von
`RuntimeException` erbt — es sind also **ungeprüfte** Ausnahmen: kein
`throws`-Klausel-Zwang, keine erzwungenen `try/catch`. Wer sie fangen
will, fängt gezielt `OsfException` (oder eine ihrer Unterklassen); wer
nicht, lässt sie bis zu einem zentralen Handler propagieren.

Davon getrennt steht der **Best-Effort-Leser**: Ein einzelner defekter
oder abgeschnittener Datenblock beendet das Lesen *nicht* mit einer
Ausnahme, sondern wird übersprungen und in `ReaderStats` gezählt. Nur
strukturelle Fehler *vor* dem Block-Strom (Header, Metablock) und harte
Vorbedingungsverletzungen werfen. Diese Trennung ist der Kern der
Fehlerbehandlung: hart scheitern, solange die Datei prinzipiell
uninterpretierbar ist — durchhalten, sobald nur einzelne Blöcke am Ende
verloren sind.

## Die Ausnahme-Hierarchie `OsfException`

```java
package com.optimeas.osf;

public class OsfException extends RuntimeException {
    // Nachricht (und optional Ursache) — die Nachricht ist nur zur Anzeige
    public static final class UnsupportedType       extends OsfException { }
    public static final class MalformedFile         extends OsfException { }
    public static final class UnknownHeaderToken    extends OsfException { }
    public static final class MetablockCrcMismatch  extends OsfException { }
}
```

Regeln:

- **Auf den Typ verzweigen, die Nachricht nur anzeigen.** `getMessage()`
  ist menschenlesbares Detail und nicht Teil der API — der Wortlaut darf
  sich ändern. Wer programmatisch reagieren will, prüft die Klasse
  (`instanceof` / `catch`-Reihenfolge).
- Ein `OsfException` **ohne** spezifischere Unterklasse ist der Fall
  „gültige API, aber falsch benutzt oder I/O fehlgeschlagen" — vor allem
  aus den Writern.
- Wo eine zugrunde liegende `IOException` die Ursache ist, wird sie als
  `cause` mitgeführt (`getCause()`), damit der Stacktrace vollständig
  bleibt.

## Fehlerkatalog

### Beim Öffnen und Parsen (harte Fehler)

Diese Ausnahmen entstehen, bevor überhaupt ein Nutzblock gelesen wird —
die Datei ist als OSF nicht interpretierbar und wird komplett abgelehnt.

| Ausnahme | Bedeutung | Typische Quelle |
|---|---|---|
| `MalformedFile` | Struktureller Defekt: kein wohlgeformter Magic-Header, unbekannter Versionsbezeichner, fehlendes Pflichtfeld im Metablock, nicht parsebare Zahl, ungültiges `sizeoflengthvalue` (≠ 2/4), unerwartetes Stromende, kein Zeilenumbruch im Header-Fenster, ungültiges JSON (OSF5) bzw. XML (OSF4). Trägt bei I/O-Ursache die `IOException` als `cause`. | Header-, Metablock-, Block-Parser |
| `UnknownHeaderToken` | Ein Magic-Header-Token, dessen Schlüssel die Bibliothek nicht kennt (Must-Understand-Regel). Bewusst von `MalformedFile` getrennt, damit ein unbekanntes Integritäts-/Erweiterungs-Token nicht als irreführender Zahlenformatfehler erscheint. | Magic-Header |
| `MetablockCrcMismatch` | Die im `crc32c`-Header-Token deklarierte CRC32C stimmt nicht mit den rohen Metablock-Bytes überein. Unter aktivem Integritätsprofil wird die Datei fail-closed abgelehnt — die Metadaten gelten als kompromittiert. | Metablock-Verifikation |
| `UnsupportedType` | Die Datei nutzt einen von der Spezifikation **entfernten** Datentyp (`pair`, `triple`, `candata`, `gpsdata`). Harte Ablehnung — das alte Payload-Layout ist aus einem aktuellen Build nicht reproduzierbar. Dient auch als Fehler beim **Zugriff**: ein typfalscher Getter auf `DataChannel` (z. B. Doubles von einem String-Kanal). | Metablock-Parser, `DataChannel` |

### Beim Schreiben und bei API-Nutzung

Die Writer (`StreamingWriter`, `BlockWriter`) werfen ein einfaches
`OsfException` bei jeder verletzten Vorbedingung — sie signalisieren
einen Programmierfehler, keinen Datendefekt:

| Auslöser | Beispiel-Bedeutung |
|---|---|
| Unbekannter Kanalindex | Sample auf einen nicht deklarierten Kanal geschrieben |
| Typ-Mismatch | Schreib-Typ passt nicht zum deklarierten Kanal-Datentyp |
| Gemischte Blocktypen | Ein Kanal liefert äquidistante *und* zeitgestempelte Blöcke — spec-verboten |
| Falsche Lebenszyklus-Phase | Sample geschrieben, obwohl der Writer noch konfiguriert oder bereits geschlossen ist |
| Kein Kanal / Länge inkonsistent | `begin`/`writeTo` ohne deklarierte Kanäle; `timestamps.length ≠ values.length` |
| Signiertes Profil angefordert | Das `ed25519`-Profil (signiert) wird von dieser CRC-Level-Bibliothek beim Schreiben abgelehnt |
| I/O-Fehler | Datei nicht öffenbar, Schreib-/`force`-Fehler — `IOException` als `cause` |

## Best-Effort-Leser: was bewusst **kein** Fehler ist

Der Block-Stromleser hält an, wo eine Datei *prinzipiell* noch lesbar
ist, statt zu werfen. Das Ergebnis wird in `ReaderStats` festgehalten,
abrufbar über `manager.stats()`:

| Situation | Verhalten |
|---|---|
| Datei endet mitten im Block (Stromausfall, Kürzung) | Alle vollständigen Blöcke werden geliefert, `stats.truncationSeen()` wird `true`, die Iteration endet sauber |
| Defekter/garbelter Blockrumpf | Best-Effort-Stopp an genau dieser Stelle: `truncationSeen()` = `true`, restliche gelesene Blöcke bleiben gültig |
| Unbekannter Kanalindex im Block-Strom | Ohne Definition ist die Breite des Längenfelds unbekannt → Stopp (`truncationSeen()`) statt Rateversuch |
| Unbekannter zukünftiger Datentyp | Kanal ist `UNSUPPORTED`; seine Blöcke werden per Länge übersprungen, alle anderen Kanäle laden normal |
| Frame-CRC eines Blocks stimmt nicht | Block wird verworfen, `stats.blocksCrcFailed()` zählt hoch, das Lesen läuft weiter |
| Signaturblock (reservierter Kanal `0xFFFE`) | Von dieser CRC-Level-Bibliothek nicht verifizierbar → übersprungen, `stats.blocksSignatureSkipped()` zählt hoch |
| Reservierte/leere Blocktypen | Als übersprungen konsumiert; kein Fehler |

Nur Fehler *vor* dem Block-Strom (Header, Metablock-CRC, Metablock-Parse)
werfen — dort ist keine partielle Interpretation vertretbar.

## Integritätsstatus — `ReaderStats.verificationStatus()`

Nach dem Laden fasst `stats.verificationStatus()` das Integritätsergebnis
in einem stabilen String zusammen (Vokabular aus der Spezifikation):

| Wert | Bedeutung | Empfohlene Reaktion |
|---|---|---|
| `"none"` | Datei trägt kein Integritätsprofil | Keine — normale Verarbeitung |
| `"crc_valid"` | Profil `crc`, **jeder** Block-CRC verifiziert | Daten als integer behandeln |
| `"invalid"` | Profil `crc`, mindestens ein Block-CRC schlug fehl (`blocksCrcFailed() > 0`) | Warnen/ablehnen; die betroffenen Blöcke fehlen in den Daten |
| `"signature_unverifiable"` | Signierte Datei (`ed25519`), die diese CRC-Level-Bibliothek **nicht** verifizieren kann | Nicht als „vertrauenswürdig signiert" ausweisen; Nutzdaten sind dennoch lesbar |

`verificationStatus()` leitet sich rein aus dem deklarierten Profil und
den Zählern ab — ein `"invalid"` bedeutet konkret, dass die
CRC-fehlerhaften Blöcke übersprungen (nicht geliefert) wurden.

## `try/catch` in der Praxis

```java
import com.optimeas.osf.*;

try {
    DataManager mgr = DataManager.loadFromFile(Path.of("messung.osf"));

    ReaderStats st = mgr.stats();
    if (st.truncationSeen()) {
        log.warn("Datei am Ende abgeschnitten — {} Blöcke gelesen", st.blocksRead());
    }
    switch (st.verificationStatus()) {
        case "invalid" -> log.error("CRC-Fehler: {} Blöcke verworfen", st.blocksCrcFailed());
        case "signature_unverifiable" -> log.warn("Signatur nicht prüfbar");
        default -> { /* none / crc_valid — ok */ }
    }

    double[] werte = mgr.channelByName("temperatur")
                        .orElseThrow()
                        .asDoubles();          // wirft UnsupportedType bei Typ-Mismatch
} catch (OsfException.MetablockCrcMismatch e) {
    // Metadaten kompromittiert — Datei ablehnen
} catch (OsfException e) {
    // MalformedFile, UnknownHeaderToken, UnsupportedType, I/O …
    log.error("OSF konnte nicht geladen werden: {}", e.getMessage(), e);
}
```

Praxisregeln:

- **Speziell vor allgemein fangen.** Spezifische Unterklassen
  (`MetablockCrcMismatch`, `UnknownHeaderToken`) zuerst, dann `OsfException`
  als Auffangnetz.
- **Nach dem Laden `stats()` prüfen** — ein erfolgreicher `load` heißt
  nicht, dass jeder Block ankam. Truncation und CRC-Ausfälle sind still
  und stehen nur in den Zählern.
- **Getter-Aufrufe auf `DataChannel` können `UnsupportedType` werfen**,
  wenn der angeforderte Typ nicht zum Kanal passt — prüfe `dataType()`
  vorher oder fange gezielt.

## Writer-Lebenszyklus

Der `StreamingWriter` erzwingt eine Zustandsmaschine `CONFIGURE →
STREAMING → CLOSED`. Ein Aufruf in der falschen Phase — Sample vor
`begin()`, Schreiben nach `close()` — wirft `OsfException`. `close()`
ist idempotent (ein zweiter Aufruf ist ein No-Op) und wechselt auch bei
einem I/O-Fehler beim finalen Flush verlässlich nach `CLOSED`, sodass
`try-with-resources` (der Writer ist `AutoCloseable`) die Datei nicht
offen lässt.

Für weitere Details siehe [Lesen](./reading.md), [Schreiben](./writing.md),
[Architektur](./architecture.md) und das
[OSF-Formathandbuch](../../osf_general.md). Zurück zur
[Java-Übersicht](../java.md).

> Dieses Dokument ist lizenziert unter [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de). Namensnennung: optiMEAS GmbH und optiMEAS Switzerland GmbH.
