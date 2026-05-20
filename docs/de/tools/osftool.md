---
title: osftool — Kommandozeilen-Werkzeug
description: Das Kommandozeilen-Werkzeug osftool zum Zusammenführen, Exportieren, Prüfen und Konvertieren von OSF-Dateien
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - OSF
  - osftool
  - CLI
  - Kommandozeile
  - merge
  - export
last_update:
  date: 2026-05-20
  author: Optimeas GmbH
---

🇬🇧 [English version](../../en/tools/osftool.md)

# osftool — Kommandozeilen-Werkzeug

**osftool** ist ein verb-basiertes Kommandozeilen-Werkzeug für die Arbeit mit Dateien im Open Streaming Format. Es liest und schreibt OSF4 und OSF5, verarbeitet komprimierte OSFZ-Dateien transparent und bündelt die alltäglichen Aufgaben rund um OSF-Daten in einer einzigen ausführbaren Datei: Dateien über ein Zeitintervall zusammenführen, Kanäle nach CSV exportieren, Metadaten prüfen, Statistiken berechnen, zwischen Formatversionen konvertieren und die Dateiintegrität prüfen.

osftool baut auf der Delphi-OSF-Bibliothek auf (`implementations/delphi/src/`). Primäres Build-Ziel ist Windows 64-Bit; das Projekt enthält zusätzlich Build-Konfigurationen für macOS (Intel und Apple Silicon) und Linux 64-Bit. Die aktuelle Version ist **1.1.0**.

## Wozu osftool dient

osftool deckt die wiederkehrenden Aufgaben eines OSF-basierten Arbeitsablaufs aus einem Skript oder einem Terminal heraus ab — ohne IDE und ohne Programmierung:

- **Felddaten zusammenführen.** Viele über einen Verzeichnisbaum verteilte OSF/OSFZ-Dateien werden eingelesen und für ein gewähltes Zeitintervall und eine Kanalauswahl zu einer einzigen Datei zusammengeführt.
- **Daten in Auswerteprogramme bringen.** Kanäle werden nach CSV exportiert — entweder als ein XY-Block je Kanal oder als gemeinsame Zeitachse mit einer Spalte je Kanal.
- **Dateien schnell prüfen.** Metadaten, Zeitbereich, Kanallisten und Statistiken je Kanal stehen zur Verfügung, ohne die Datei in einer Anwendung zu öffnen.
- **Zwischen Formatversionen migrieren.** OSF4-Dateien werden als OSF5 neu geschrieben und umgekehrt.
- **Integrität prüfen.** Dateien werden Block für Block durchlaufen und auf strukturelle Konsistenz geprüft.
- **Automatisierung.** Jeder Befehl besitzt einen maschinenlesbaren `--json`-Modus und einen einheitlichen Satz von Exit-Codes, sodass sich osftool sauber in Skripte und CI-Pipelines einfügt.

## Installation

### Aus dem Quellcode bauen

osftool ist Teil des OSF-Repositorys und wird mit Embarcadero Delphi 12 (RAD Studio 23) oder neuer gebaut.

```
cd implementations/delphi/tools/osftool
dcc64 -B -Q OsfTool.dpr
```

Dies erzeugt `OsfTool.exe` für Windows 64-Bit. Die Projektdatei `OsfTool.dproj` enthält zusätzlich die Build-Konfigurationen `OSX64`, `OSXARM64` und `Linux64` für die Verwendung aus der RAD-Studio-IDE; der Quellcode ist für diese Ziele frei von Konfigurationskonflikten der bedingten Kompilierung.

### osftool zum PATH hinzufügen

Damit `osftool` aus jedem Verzeichnis heraus aufgerufen werden kann, wird sein Speicherort dem Suchpfad hinzugefügt:

```
osftool config install-path
```

Unter Windows trägt dieser Befehl das Verzeichnis der ausführbaren Datei in den benutzerbezogenen `PATH` ein (`HKCU\Environment`) — Administratorrechte sind nicht erforderlich. Unter macOS und Linux gibt der Befehl den Shell-Schnipsel aus, der zu `~/.zshrc` oder `~/.bashrc` hinzugefügt werden soll. Rückgängig gemacht wird die Änderung mit `osftool config uninstall-path`.

## Allgemeine Bedienung

```
osftool <befehl> [optionen] [argumente]
osftool <befehl> --help
```

`osftool` ohne Argumente oder `osftool --help` gibt die globale Hilfe mit der Befehlsliste aus. Wird `--help` an einen Befehl angehängt, gibt dieser seine ausführliche Hilfe aus, ohne eine Aktion auszuführen. `osftool --version` (oder `-V`) gibt Version und Build-Zeitstempel aus; mit `--short` nur die Versionsnummer.

### Befehle

| Befehl     | Zweck |
|------------|-------|
| `merge`    | OSF-Dateien aus einem Verzeichnis über ein Zeitintervall zusammenführen |
| `export`   | OSF-Kanäle nach CSV oder in andere Formate exportieren |
| `info`     | Metadaten und Zeitbereich einer Datei anzeigen |
| `channels` | Alle Kanäle einer Datei auflisten |
| `stat`     | Statistiken (Minimum, Maximum, Mittelwert, …) je Kanal berechnen |
| `cache`    | `.json`-Sidecar-Cache-Dateien verwalten |
| `config`   | Standardeinstellungen anzeigen und bearbeiten |
| `convert`  | Zwischen OSF4 und OSF5 konvertieren |
| `verify`   | Dateiintegrität und Blockkonsistenz prüfen |

### Globale Optionen

Diese Optionen versteht jeder Befehl:

| Option      | Wirkung |
|-------------|---------|
| `--json`    | Gibt ein maschinenlesbares JSON-Ergebnis auf stdout aus statt menschenlesbarem Text |
| `--quiet`   | Unterdrückt informative Ausgaben; Warnungen und Fehler werden weiterhin angezeigt |
| `--verbose` | Gibt Debug-Meldungen auf stderr aus |

Ergebnisse gehen nach **stdout**, Log-Meldungen und Fehler nach **stderr**, sodass beide getrennt umgeleitet werden können. Im `--json`-Modus enthält stdout ausschließlich das JSON-Dokument.

### Exit-Codes

| Code | Bedeutung |
|------|-----------|
| `0`  | Erfolg |
| `1`  | Ungültige Argumente |
| `2`  | Datei oder Verzeichnis nicht gefunden |
| `3`  | E/A-Fehler |
| `4`  | Formatfehler (ungültige oder beschädigte OSF-Datei) |

## Der Sidecar-Cache

Mehrere Befehle können eine `.json`-**Sidecar-Datei** nutzen, die neben der jeweiligen OSF-Datei abgelegt wird. Die Sidecar-Datei enthält Metadaten je Kanal — Anzahl der Werte, ersten und letzten Zeitstempel, den globalen Zeitbereich —, die der OSF-Metablock allein nicht trägt. Liegt eine gültige Sidecar-Datei vor, antworten Befehle wie `info` und `channels` sofort, anstatt jeden Block zu durchlaufen.

Die Sidecar-Datei wird automatisch herangezogen; mit `--no-cache` wird sie ignoriert und die Quelldatei direkt durchsucht. Der Befehl `cache` baut, erneuert, prüft und entfernt Sidecar-Dateien in größerer Menge.

## Befehlsreferenz

### merge

Durchsucht ein Verzeichnis rekursiv nach `.osf`- und `.osfz`-Dateien, wählt die Dateien aus, die ein Zeitintervall überlappen, und führt die gewählten Kanäle zu einer einzigen Ausgabedatei zusammen.

```
osftool merge <rootdir> <outputfile> [kanal ...] [optionen]
```

| Argument     | Beschreibung |
|--------------|--------------|
| `rootdir`    | Wurzelverzeichnis; wird rekursiv nach `.osf` und `.osfz` durchsucht |
| `outputfile` | Pfad der Ausgabedatei (`.osf`) |
| `kanal`      | Optionale Kanalnamen; weglassen, um alle Kanäle zusammenzuführen |

| Option         | Beschreibung |
|----------------|--------------|
| `--start <ts>` | Intervallbeginn, ISO 8601 (Vorgabe: `1970-01-01T00:00:00`) |
| `--end <ts>`   | Intervallende, ISO 8601 (Vorgabe: aktuelles Datum und Uhrzeit) |
| `--osf4`       | OSF4-Ausgabe schreiben (Vorgabe: aus Konfiguration `output.format`) |
| `--overwrite`  | Überlappende Zeitstempel überschreiben (Vorgabe: aus Konfiguration `output.overlap`) |
| `--no-cache`   | Keine `.json`-Sidecar-Dateien lesen oder schreiben |
| `-q`, `--quiet`   | Live-Anzeige unterdrücken; nur Fehler, auf stderr |
| `-v`, `--verbose` | Jede Log-Zeile ausgeben (klassische Scroll-Ausgabe, keine Live-Leiste) |
| `--json`          | Maschinenlesbaren JSON-Lines-Ereignisstrom auf stdout ausgeben |
| `--log <pfad>`    | Vollständiges Diagnose-Log (alle Stufen) in eine Datei schreiben |

`--start` und `--end` sind optional und unabhängig: Jede Grenze besitzt einen eigenen Vorgabewert, und eine angegebene Option überschreibt nur diese eine Grenze. Ohne beide Optionen deckt `merge` den gesamten verfügbaren Bereich ab.

```
# Alles unter ./feld-daten in eine Datei zusammenführen
osftool merge ./feld-daten zusammen.osf

# Nur zwei Kanäle innerhalb eines Zeitfensters zusammenführen
osftool merge ./feld-daten fenster.osf Sensor/Temperatur Sensor/Druck \
  --start 2026-05-05T10:00:00 --end 2026-05-05T12:00:00
```

Standardmäßig zeigt `merge` eine **Live-Fortschrittsanzeige**: eine Überschrift je Phase, die gerade gelesene Datei und einen Fortschrittsbalken. Fehlerzeilen bleiben dauerhaft oberhalb des Balkens stehen, während die informativen und warnenden Meldungen je Kanal unterdrückt werden.

```
Reading files...
  V:\feld-daten\20260517\20260517_192802.osfz
  [████████████████████░░░░░░░░░░░░░░░░░░░░] 50% (173/346)
```

Die Ausgabe-Optionen ändern diese Darstellung und schließen sich gegenseitig aus: `-v` / `--verbose` gibt stattdessen das vollständige Scroll-Log aus; `-q` / `--quiet` gibt nur Fehler auf stderr aus und ist sonst still; `--json` liefert einen JSON-Lines-Ereignisstrom für Pipelines. `--log <pfad>` ist orthogonal — es schreibt in jedem Modus das vollständige Diagnose-Log in eine Datei. Wird stdout in eine Pipe oder Datei umgeleitet, ersetzt osftool die Live-Anzeige automatisch durch periodische einfache Fortschrittszeilen.

### export

Exportiert die Kanäle einer einzelnen OSF/OSFZ-Datei nach CSV.

```
osftool export <inputfile> <outputfile> [kanal ...] [optionen]
```

| Argument     | Beschreibung |
|--------------|--------------|
| `inputfile`  | Quelldatei `.osf` oder `.osfz` |
| `outputfile` | Pfad der Ausgabedatei |
| `kanal`      | Optionale Kanalnamen; weglassen, um alle Kanäle zu exportieren |

| Option                     | Beschreibung |
|----------------------------|--------------|
| `--format <fmt>`           | `csv` (Vorgabe) — ein XY-Block je Kanal; oder `unified-csv` — eine gemeinsame Zeitachse mit einer Spalte je Kanal |
| `--timestamp-format <fmt>` | Zeitstempelformat für `unified-csv`: `datetime` (Vorgabe), `seconds`, `iso8601`, `nanoseconds` |
| `--start <zeit>`           | Nur Werte ab dieser UTC-Zeit exportieren (ISO 8601) |
| `--end <zeit>`             | Nur Werte bis zu dieser UTC-Zeit exportieren (ISO 8601) |
| `--decimal-sep <c>`        | Dezimaltrennzeichen: `comma` (Vorgabe) oder `dot` |
| `--encoding <enc>`         | `iso-8859-1` (Vorgabe) oder `utf-8` |
| `--exclude-empty`          | Kanäle ohne Werte überspringen |

`--start` und `--end` müssen gemeinsam angegeben werden — eine Option ohne die andere wird abgelehnt. Die Vorgaben für `--decimal-sep` und `--encoding` stammen aus der Konfigurationsdatei.

```
osftool export motorbike.osf motorbike.csv --format unified-csv \
  --timestamp-format iso8601 --decimal-sep dot
```

### info

Zeigt Metadaten und den globalen Zeitbereich einer Datei an.

```
osftool info <file> [optionen]
```

| Option       | Beschreibung |
|--------------|--------------|
| `--no-cache` | Die `.json`-Sidecar-Datei nicht heranziehen; die OSF-Datei direkt durchsuchen |
| `--json`     | Ausgabe als JSON |

Der Bericht enthält die Formatversion, Ersteller und Erstellungszeit, Tag, Reason, Kommentar, Kanalanzahl, den ersten und letzten Datenzeitstempel sowie die daraus resultierende Dauer.

```
osftool info motorbike.osf
```

### channels

Listet jeden Kanal des Metablocks auf.

```
osftool channels <file> [optionen]
```

| Option               | Beschreibung |
|----------------------|--------------|
| `--filter <muster>`  | Wildcard-Filter auf den Kanalnamen, z. B. `GPS.*` |
| `--no-cache`         | Die `.json`-Sidecar-Datei nicht heranziehen |
| `--json`             | Ausgabe als JSON-Array |

Jede Zeile zeigt Kanalindex, Name, Datentyp, physikalische Einheit und — sofern eine gültige Sidecar-Datei vorliegt — die Anzahl der Werte sowie ersten/letzten Zeitstempel. Ohne Sidecar-Datei zeigen diese Spalten `?`.

```
osftool channels motorbike.osf --filter "Sensor/*"
```

### stat

Berechnet Minimum, Maximum, Mittelwert und Standardabweichung je numerischem Kanal mit dem einläufigen Online-Algorithmus von Welford.

```
osftool stat <file> [kanal ...] [optionen]
```

| Option           | Beschreibung |
|------------------|--------------|
| `--start <zeit>` | Nur Werte ab dieser UTC-Zeit berücksichtigen (ISO 8601) |
| `--end <zeit>`   | Nur Werte bis zu dieser UTC-Zeit berücksichtigen (ISO 8601) |
| `--json`         | Ausgabe als JSON |

`--start` und `--end` sind jeweils unabhängig optional. String-, Binär- und gpslocation-Kanäle werden aufgelistet, aber als *nicht numerisch* gekennzeichnet. Int64/UInt64-Kanäle werden auf möglichen Genauigkeitsverlust bei der Umwandlung nach `Double` für die Berechnung hingewiesen.

```
osftool stat motorbike.osf --start 2026-05-05T10:00:00
```

### cache

Verwaltet die `.json`-Sidecar-Cache-Dateien für jede `.osf`/`.osfz`-Datei unterhalb eines Verzeichnisses.

```
osftool cache <subbefehl> <rootdir> [optionen]
```

| Subbefehl | Beschreibung |
|-----------|--------------|
| `build`   | Fehlende Sidecar-Dateien erzeugen; Dateien mit bereits gültiger Sidecar-Datei überspringen |
| `rebuild` | Jede Sidecar-Datei erzwungen neu erzeugen |
| `clean`   | Alle Sidecar-Dateien unterhalb von `rootdir` löschen |
| `status`  | Anzeigen, welche Dateien keine gültige Sidecar-Datei haben |

| Option           | Beschreibung |
|------------------|--------------|
| `--no-recursive` | Nur auf das Wurzelverzeichnis beschränken (Vorgabe: Unterverzeichnisse einbeziehen) |
| `--json`         | Ausgabe als JSON |

```
osftool cache build ./feld-daten
osftool cache status ./feld-daten
```

### config

Prüft und ändert die dauerhaften Einstellungen, die andere Befehle als Vorgabewerte nutzen, und verwaltet den `PATH`-Eintrag.

```
osftool config                    Alle aktuellen Einstellungen anzeigen
osftool config set <key> <wert>   Einen Wert setzen
osftool config reset              Alle Einstellungen auf Vorgaben zurücksetzen
osftool config install-path       osftool zum Benutzer-PATH hinzufügen
osftool config uninstall-path     osftool aus dem Benutzer-PATH entfernen
osftool config --json             Einstellungen als JSON anzeigen
```

Die verfügbaren Schlüssel sind unter [Konfiguration](#konfiguration) beschrieben.

### convert

Konvertiert eine einzelne Datei zwischen OSF4 und OSF5, indem sie über den Merger neu geschrieben wird.

```
osftool convert <inputfile> <outputfile> [optionen]
```

| Option   | Beschreibung |
|----------|--------------|
| `--osf4` | Ausgabe als OSF4 schreiben |
| `--osf5` | Ausgabe als OSF5 schreiben |
| `--json` | Ausgabe als JSON |

`--osf4` und `--osf5` schließen sich gegenseitig aus. Wird keine der beiden Optionen angegeben, stammt die Zielversion aus dem Konfigurationsschlüssel `output.format`. Zu beachten ist, dass der Konverter unabhängig vom Aufbau der Quelldatei stets Datenblöcke mit absolutem Zeitstempel schreibt.

```
osftool convert legacy.osf modern.osf --osf5
```

### verify

Durchläuft eine Datei von Anfang bis Ende und prüft sie auf strukturelle Integrität.

```
osftool verify <file> [optionen]
```

Folgende Prüfungen werden durchgeführt:

1. Magic-Header lesbar und Version erkannt.
2. Metablock verarbeitbar (gültiges XML oder JSON).
3. Der Kanalindex jedes Blocks ist im Metablock vorhanden.
4. Kein Block gibt eine Länge an, die die Dateigröße übersteigt.
5. Zeitstempel steigen je Kanal monoton an.
6. Die Datei endet sauber — der letzte Block ist nicht abgeschnitten.

| Option     | Beschreibung |
|------------|--------------|
| `--strict` | Warnungen als Fehler behandeln (wirkt sich auf den Exit-Code aus) |
| `--json`   | Ausgabe als JSON |

Integritätsverletzende Probleme werden als Fehler gemeldet und führen zu Exit-Code `4`; behebbare Auffälligkeiten (etwa ein abgeschnittener letzter Block) sind Warnungen. Mit `--strict` führen auch Warnungen zu Exit-Code `4`.

```
osftool verify motorbike.osf --strict
```

## Konfiguration

osftool speichert dauerhafte Einstellungen in einer JSON-Datei. Andere Befehle lesen daraus ihre Vorgabewerte, sodass die Konfiguration als benutzerbezogene Richtlinie wirkt.

| Plattform | Speicherort |
|-----------|-------------|
| Windows   | `%APPDATA%\osftool\config.json` |
| macOS / Linux | `~/.config/osftool/config.json` |

Die Datei ist optional — fehlt sie, fällt jede Einstellung auf ihren eingebauten Vorgabewert zurück.

| Schlüssel            | Vorgabe      | Bedeutung |
|----------------------|--------------|-----------|
| `output.format`      | `osf5`       | Vorgabe-Ausgabeformat für `merge` und `convert` |
| `output.overlap`     | `skip`       | Überlappungsstrategie: `skip` oder `overwrite` |
| `export.decimal_sep` | `,`          | Vorgabe-Dezimaltrennzeichen für CSV |
| `export.encoding`    | `iso-8859-1` | Vorgabe-Kodierung für CSV |
| `cache.enabled`      | `true`       | `.json`-Sidecar-Dateien verwenden |
| `cache.auto_build`   | `true`       | Cache während eines Scans automatisch aufbauen |

Werte werden mit dem Befehl `config` gelesen und geschrieben:

```
osftool config                          # alle Einstellungen anzeigen
osftool config set output.format osf4   # eine Vorgabe ändern
osftool config reset                    # Vorgaben wiederherstellen
```

Eine Kommandozeilen-Option überschreibt die konfigurierte Vorgabe stets nur für diesen einen Aufruf.
