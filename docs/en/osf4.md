---
title: OSF4 — Specific Documentation
description: Details and specifications for the Open Streaming Format Version 4 (OSF4)
#sidebar_label: OSF4
image: "/img/om_social_card.png"
keywords:
  - OSF4
  - File format
  - XML
last_update:
  date: 2026-05-04
  author: Optimeas GmbH
---

🇩🇪 [German version](../de/osf4.md)

# OSF4 — Specific Documentation

This document describes all aspects of the **Open Streaming Format Version 4 (OSF4)** that go beyond the general OSF description.  
It complements [`osf_general.md`](osf_general.md), which explains all structures common to OSF4 and OSF5.

OSF4 is the classic version of the format. It uses **XML** exclusively for the metablock and forms the basis for backward compatibility in OSF5.



## Magic header in OSF4

- **Allowed identifiers:**  
  - `OSF4`  
  - `OCEAN_STREAM_FORMAT4` (historical identifier)

- **Format:**  
  ```
  OSF4 173762\n
  ```

- **Properties:**  
  - Always an **XML metablock** directly after the header.  
  - No JSON support.  
  - OSF5 parsers can read OSF4 files in full.

---

## Metablock in OSF4 (XML)

OSF4 uses **XML** exclusively for the metablock.  
It contains all file and channel information and starts with a standard prolog:

```xml
<?xml version="1.0" encoding="UTF-8"?>
```

### Root element `<osf>`

Example:

```xml
<osf version="4"
     created_utc="2019-08-12T12:23:01+02:00"
     creator="smartdevice:14001000078"
     created_at_longitude="50.2"
     created_at_latitude="8.65"
     created_at_altitude="193"
     reason="BOOT"
     total_seq_no="0"
     triggered_seq_no="0"
     namespacesep="."
     tag="preview"
     comment="">
```

#### Attributes:

- **version:** Version of the OSF4 format (default: `"1"`).  
- **created_utc:** File creation time in ISO 8601 format (UTC).  
- **creator:** Identification of the producing device or program.  
- **created_at_longitude / latitude / altitude:** Optional geographic position.  
- **reason:** Reason for file creation (`BOOT`, `SEQUENCE`, `TRIGGERED`).  
- **total_seq_no:** Absolute sequence number since system start.  
- **triggered_seq_no:** Relative sequence number since the last trigger.  
- **namespacesep:** Separator for hierarchical channel names (default: `"."`).  
- **tag:** Free tag for classifying the file (default: `"preview"`).  
- **comment:** Optional comment.  

---

## Channel list `<channels>`

Contains all channels of the file.

```xml
<channels count="8">
    <channel index="0"
             name="Sensor/Temperature"
             channeltype="scalar"
             datatype="double"
             timeincrement="1000000"
             sizeoflengthvalue="2"
             physicalunit="°C"
             reference="uuid"
             physicaldimension="temperature"/>
</channels>
```

- **count:** Number of channels in the file.

---

## Channel description `<channel>`

All parameters as described in the general OSF documentation. For OSF4:

- **Metablock:** Always XML  
- **Supported `channeltype`:** `scalar`, `vector`, `matrix`, `binary`  
- **Vector and matrix parameters:** Defined in the channel definition via `rows`, `columns`, and related attributes  
- **`sizeoflengthvalue`:** Required field (2 or 4 bytes)  

---

## Supported data types in OSF4

| Data type   | Size     | Description |
|-------------|----------|--------------|
| `bool`      | 1 byte   | True/false |
| `int8`      | 1 byte   | Signed integer |
| `int16`     | 2 bytes  | Signed integer |
| `int32`     | 4 bytes  | Signed integer |
| `int64`     | 8 bytes  | Signed integer |
| `uint8`     | 1 byte   | Unsigned integer, range 0 … 255 |
| `uint16`    | 2 bytes  | Unsigned integer, range 0 … 65 535 |
| `uint32`    | 4 bytes  | Unsigned integer, range 0 … 4 294 967 295 |
| `uint64`    | 8 bytes  | Unsigned integer, range 0 … 18 446 744 073 709 551 615 |
| `float`     | 4 bytes  | IEEE 754 single precision |
| `double`    | 8 bytes  | IEEE 754 double precision |
| `string`    | variable | UTF-8 encoded, length defined by block size. Ends with a trailing null byte (`0x00`) — see [`osf_general.md`](osf_general.md#note-on-null-termination-of-string-and-binary). |
| `gpslocation` | 24 bytes | Structure for GPS positions |

## Null termination

For `bcAbsTimeStampData` with `datatype=string` or `datatype=binary`, the rule described in [`osf_general.md`](osf_general.md#note-on-null-termination-of-string-and-binary) applies: a trailing null byte (`0x00`) at the end of the data field. This is existing behavior in OSF4 and remains unchanged.

---

## Data blocks in OSF4

OSF4 uses the data block structures described in the general specification.  
Specific to OSF4:

- **Control byte:** All original block types `0–8` are supported.  
- **`bcContinuedRelStampData`:** Still used in OSF4 (removed from OSF5).  
- **`bcStatusEvent` and `bcMessageEvent`:** Present, but no longer recommended in new implementations.  
- **Metadata:** Always in XML format.

### `bcStartData` with sample rate

As of this spec revision, `bcStartData` in OSF4 — like in OSF5 — carries a `double` field with the **sample rate (Hz)** directly after the `int64` start timestamp. The rate applies to all subsequent `bcContinuedData` blocks of the same channel until a new `bcStartData` block is written.

**`bcStartData` example in OSF4:**
`[int64 StartTime] [double SampleRate] [uint32 N] [double Value1] [double Value2] ... [double ValueN]`

New OSF4 writers **must** write this field. Readers that encounter an OSF4 file without this field (older existing files) fail with a format error — there is no implicit fallback derived from `timeincrement`.

### Restrictions:

- **Equidistant channels (`bcStartData`, `bcContinuedData`):** Numeric types only (`int*`, `float`, `double`).  
- **Time-stamped channels (`bcAbsTimeStampData`, `bcContinuedRelStampData`):** All types allowed.

---

## XML trailer and magic trailer

OSF4 optionally supports an XML info block with channel statistics and a magic trailer.

### XML trailer example:

```xml
<trailer finalized_utc="2019-08-12T12:23:01+02:00" reason="fileStartGrid_min">
    <channels count="8">
        <channel index="0" samples="29452" last_ns="1384899599997800000"/>
        <channel index="1" samples="29452" last_ns="1384899599997800000"/>
    </channels>
</trailer>
```

### Magic trailer:

```
OSF_STREAM_END 321316454==============
```

- 40 bytes long; the number = position of the `0xFFFF` block.

---

## Specifics and limitations of OSF4

- Only XML metablock; no JSON support.  
- Control byte is more complex; more block types than OSF5.  
- Vector and matrix channels via XML parameters (`rows`, `columns`).  
- `bcContinuedRelStampData` is supported but no longer present in OSF5.  

---

## Example of an OSF4 file

```text
OSF4 30269
<?xml version="1.0" encoding="UTF-8"?>
<osf version="4" created_utc="2019-08-12T12:23:01+02:00" creator="smartdevice:14001000021">
  <channels count="2">
    <channel index="0" name="Sensor/Temperature" channeltype="scalar" datatype="double" timeincrement="1000000"/>
    <channel index="1" name="Sensor/Pressure" channeltype="scalar" datatype="double" timeincrement="1000000"/>
  </channels>
</osf>
[BEGIN OF BINARY DATA]
```
