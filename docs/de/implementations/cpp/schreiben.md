---
title: Schreiben
description: OSF5 schreiben — StreamingWriter (embedded, ausfallsicher), BlockWriter (analystenfreundlich), StaleValueGuard und Round-Trip
sidebar_position: 3
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - StreamingWriter
  - BlockWriter
  - StaleValueGuard
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
---

# Schreiben

Die Bibliothek schreibt **ausschließlich OSF5** (DECISIONS §6) — auch
dann, wenn die Quelle eine OSF4-Datei war. Zwei Writer-Klassen decken
zwei sehr unterschiedliche Einsatzprofile ab:

| | `StreamingWriter` | `BlockWriter` |
|---|---|---|
| Einsatz | Embedded-Aufzeichnung | Analyse, Konvertierung, Export |
| Speicher | konstant (Scratch-Puffer) | sammelt alle Samples im RAM |
| Durabilität | `fsync` pro Block — ausfallsicher bei Stromverlust | kein fsync; Datei entsteht am Ende |
| Senke | Dateipfad | Dateipfad **oder** `std::ostream` (Memory, Socket) |
| `sizeoflengthvalue` | fix ab `start()` (Metablock liegt auf Platte) | automatischer Bump 2 → 4 bei Bedarf |
| Lebenszyklus | Configure → `start()` → Schreiben → `close()` | Sammeln → `write_to_file()` / `write_to()` (beliebig oft) |
| Mehrfach-Emission | nein (eine Datei pro Instanz) | ja (`write_to*` ist `const`) |

Beide teilen sich die Kanal-Beschreibung `osf::ChannelDef` und dieselben
Schreibfamilien (äquidistant, timestamped numerisch, GPS, String/Binary).

## Kanäle deklarieren — `ChannelDef`

```cpp
osf::ChannelDef def;
def.name          = "motor.drehzahl";          // Pflicht
def.data_type     = osf::DataType::Double;     // Pflicht
def.channel_type  = osf::ChannelType::Scalar;  // Pflicht (Scalar = Konvention)
def.size_of_length_value = 2;                  // 2 (Standard) oder 4
def.physical_unit = "1/min";                   // optional
def.display_name  = "Motordrehzahl";           // optional
// ferner: physical_dimension, mime_type, reference, comment, time_increment_ns
```

`add_channel(def)` liefert den Kanalindex (sequenziell ab 0), den alle
Schreibaufrufe verwenden. Abgelehnt werden (`InvalidArgument`):
`size_of_length_value` ≠ 2/4, `Unsupported`-Typen, mehr als 65535
Kanäle — beim `StreamingWriter` zusätzlich jeder Aufruf nach `start()`.

### `size_of_length_value` richtig wählen

Das Längenfeld jedes Blocks ist 2 oder 4 Bytes breit und begrenzt die
Blockgröße (~64 KB bzw. ~2 GB). Praktische Regeln:

- **String/Binary-Kanäle mit großen Samples** (Bilder, Audio, Blobs):
  beim `StreamingWriter` zwingend `4` deklarieren — er kann den Wert
  nach `start()` nicht mehr ändern und lehnt zu große Samples mit
  `InvalidBlock` ab. Der `BlockWriter` hebt selbst an (Bump 2 → 4 beim
  Emit), dort ist `2` als Startwert immer in Ordnung.
- **Hochratige numerische Kanäle** am `StreamingWriter`: `4` erspart
  Chunking — ein 100k-Sample-`double`-Append in einen `sov=2`-Kanal
  zerfällt sonst in ~13 Blöcke = ~13 fsyncs.
- Sonst: beim Standard `2` bleiben (kompaktere Blöcke).

## `StreamingWriter` — embedded, ausfallsicher

### Lebenszyklus

```mermaid
stateDiagram-v2
    [*] --> Configure : Konstruktor(path)
    Configure --> Streaming : start() — Header+Metablock auf Platte, fsync
    Streaming --> Streaming : write_* — Block kodieren, schreiben, fsync
    Streaming --> Closed : close()
    Streaming --> Broken : I/O-Fehler (sticky)
    Broken --> Closed : close() — gibt den Originalfehler zurück
    Closed --> [*]
```

```cpp
#include <osf/streaming_writer.hpp>

osf::StreamingWriter w("aufzeichnung.osf");
w.set_creator("logger-fw/3.2");                 // Metadaten vor start()
w.set_tag("pruefstand-7");

auto rpm = w.add_channel(rpm_def);              // Result<uint16_t>
auto gps = w.add_channel(gps_def);
if (!rpm || !gps) { /* … */ }

if (auto r = w.start(); !r) { /* Datei offen, Metablock geschrieben */ }

// Aufzeichnungsschleife
while (running) {
    auto r = w.write_timestamped_sample<double>(*rpm, now_ns(), read_rpm());
    if (!r) { /* I/O-Fehler => Writer ist Broken; abbrechen */ break; }
}

if (auto r = w.close(); !r) { /* … */ }
```

Garantien und Verhalten:

- **Jeder erfolgreich zurückgekehrte `write_*` ist auf Platte**
  (`FlushFileBuffers` unter Windows, `fsync` unter POSIX). Nach
  Stromausfall ist die Datei bis zum letzten bestätigten Block lesbar —
  der Reader ist auf genau dieses Szenario ausgelegt (Best-Effort bei
  Trunkierung).
- **Sticky Error:** Nach einem I/O-Fehler wechselt der Writer in den
  Zustand `Broken`; jeder weitere Aufruf (auch `close()`) gibt den
  ursprünglichen Fehler zurück. So geht die Fehlerursache in
  Fire-and-forget-Schleifen nicht verloren.
- **Metadaten-Setter nur vor `start()`** wirksam — der Metablock wird
  von `start()` geschrieben und nie wieder angefasst.
- **Kein OSFZ:** Der Streaming-Writer schreibt rohe `.osf`-Dateien.
  Kompression ist ein nachgelagerter Schritt (DECISIONS §12).
- Move-konstruier-/zuweisbar, nicht kopierbar. Nicht thread-safe —
  Zugriffe extern serialisieren.
- Der Scratch-Puffer wächst auf die Größe des größten je geschriebenen
  Blocks und wird erst im Destruktor freigegeben.

### Schreibfamilien

```cpp
// Äquidistant (nur float/double per Spec) — Segment öffnen + verlängern:
w.start_equidistant_segment(ch, t0_ns, 1000.0 /*Hz*/, daten.data(), daten.size());
w.append_equidistant_samples(ch, weitere.data(), weitere.size());   // braucht offenes Segment

// Timestamped numerisch (11 Typen, Template):
w.write_timestamped_sample<std::int32_t>(ch, ts_ns, wert);
w.write_timestamped_samples<double>(ch, ts_array, werte, n);        // parallele Arrays

// GPS (eigene Symbole, kein Template):
w.write_timestamped_gps_sample(ch, ts_ns, osf::GpsLocation{lat, lon, alt});

// String/Binary (ein Sample pro Block per Spec; OSF5: kein 0x00-Terminator):
w.write_timestamped_string(ch, ts_ns, "Ereignis: Tür offen");
w.write_timestamped_binary(ch, ts_ns, osf::BinarySample::from_vector(jpeg_bytes));
```

Jeder Mehr-Sample-Aufruf wird automatisch auf die Blockkapazität des
Kanals gechunkt (ein fsync pro Block). Ein neues
`start_equidistant_segment` öffnet bewusst ein **neues** Segment —
Lücken zwischen Segmenten sind das spec-gemäße Mittel, um
Aufzeichnungspausen darzustellen.

## `BlockWriter` — sammeln und emittieren

```cpp
#include <osf/block_writer.hpp>

osf::BlockWriter w;
w.set_creator("analyse-tool/1.0");

auto ch = w.add_channel(def);
w.add_equidistant_segment(*ch, t0_ns, 100.0, samples.data(), samples.size());
w.add_timestamped_sample<double>(*ev, ts_ns, 42.0);
w.add_string_sample(*log, ts_ns, "Kalibrierung ok");

if (auto r = w.write_to_file("ergebnis.osf"); !r) { /* … */ }

std::ostringstream mem;                 // oder in einen beliebigen ostream
if (auto r = w.write_to(mem); !r) { /* … */ }
```

- Die `add_*`-Familie spiegelt die `write_*`-Familie des
  Streaming-Writers (gleiche Typen, gleiche Validierung), sammelt aber
  nur im Speicher; Chunking in spec-konforme Blöcke passiert beim Emit.
- `write_to_file` / `write_to` sind **`const`** — dieselbe Instanz darf
  mehrfach emittiert werden (z. B. Datei + Netzwerk).
- **Auto-Bump:** Variable Kanäle, deren größtes Sample nicht in das
  deklarierte u16-Längenfeld passt, bekommen für die Emission
  `sizeoflengthvalue = 4`.
- Kein fsync — Durabilität liegt beim Aufrufer.
- `channel_index("name")` und `channel_count()` helfen, wenn die
  Indizes nicht mitgeführt werden.

## Metadaten-Defaults (DECISIONS §13)

Beide Writer wenden beim Zusammenbau des Metablocks dieselben
Defaults an (Parität mit dem Rust-Writer):

| Feld | Verhalten wenn nicht gesetzt |
|---|---|
| `created_utc` | **immer** automatisch gestempelt (aktuelle UTC-Zeit, `YYYY-MM-DDTHH:MM:SSZ`) |
| `creator` | `osf-cpp/<Bibliotheksversion>` |
| `tag` | `default` |
| `reason`, `created_at_*`, `namespace_sep`, `comment` | weggelassen (nicht als `null` geschrieben) |

## `StaleValueGuard` — inaktive Kanäle frisch halten

Sporadische (Event-)Kanäle bekommen nur bei Wertänderung ein Sample.
Auf einem Zeitstrahl ist ein vor Stunden zuletzt geschriebener Kanal
mehrdeutig: Gilt der Wert noch, oder ist die Aufzeichnung tot? Die
optiMEAS-Konvention begrenzt diese „Staleness", indem der letzte Wert
spätestens alle 100 s wiederholt wird. Genau das automatisiert der
Guard als Write-Through-Wrapper über einem gestarteten
`StreamingWriter`:

```cpp
#include <osf/stale_value_guard.hpp>

osf::StreamingWriter w(path);
/* … konfigurieren, start() … */
osf::StaleValueGuard guard(w);                  // Default: 100 s; eigener Wert möglich

// Timestamped-Writes durch den Guard routen (cached den letzten Wert):
guard.write_timestamped_sample<double>(temp_ch, ts_ns, 21.5);

// Periodisch (z. B. im Aufzeichnungs-Tick):
auto reemitted = guard.poll(now_ns);            // Result<std::size_t>
```

Eigenschaften:

- **Pull-basiert:** kein interner Thread, keine eigene Uhr — der
  Aufrufer liefert `now_ns` an `poll()`. Deterministisch und
  embedded-tauglich.
- Pro `poll()` höchstens **eine** Wiederholung je Kanal (kein
  Backfill der Lücke).
- Nur numerische + GPS-Kanäle; String/Binary bewusst nicht (große
  Blobs wiederholen wäre kontraproduktiv).
- Kanäle werden beim ersten Write-Through automatisch erfasst;
  `is_tracked` / `forget` / `clear` steuern das Tracking.
- Echte Writes setzen die Inaktivitätsuhr zurück — aktiv beschriebene
  Kanäle bekommen nie eine synthetische Wiederholung.

## Round-Trip und Konvertierung

Einen geladenen `DataManager` wieder herausschreiben (auch als
OSF4 → OSF5-Konvertierung):

```cpp
#include <osf/manager.hpp>
#include <osf/block_writer.hpp>

auto mgr = osf::DataManager::load_from_file("alt.osf");      // auch OSF4 / OSFZ
if (!mgr) { /* … */ }

if (auto r = osf::write_to_file(*mgr, "neu.osf"); !r) { /* … */ }   // immer OSF5
```

Intern baut `BlockWriter::from_manager(mgr)` einen Writer aus den
typisierten Kanälen; wer vor dem Schreiben filtern oder umbenennen
will, benutzt `from_manager` direkt und arbeitet auf dem Writer.

Erhalten bleiben: Kanalnamen, Datentypen, Sample-Werte (bitgenau),
Segmentgrenzen, Datei-Metadaten (außer `created_utc`, das beim
Schreiben neu gestempelt wird). **Nicht** erhalten bleibt der
Kanalindex — der Writer vergibt sequenziell 0..N neu.

## Was die Writer bewusst nicht tun

- **Kein OSF4-Output** — OSF5 ist das einzige Schreibformat
  (DECISIONS §6).
- **Kein OSFZ-Output** — Kompression ist nachgelagert (DECISIONS §12);
  ein Post-Close-Kompressor und ein `osf-compress`-CLI sind als
  Folgearbeit entworfen.
- **Kein `bcContinuedRelStampData`** — das relative Zeitformat ist
  OSF4-Lesealtbestand; Writer emittieren absolute Zeitstempel.
- **Keine Zeitstempel-Validierung** — Monotonie ist laut Spec nicht
  gefordert und wird nicht erzwungen.
