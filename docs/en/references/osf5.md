---
title: OSF5 — Specific Documentation
description: Details and specifications for the Open Streaming Format Version 5 (OSF5)
#sidebar_label: OSF5
image: "/img/om_social_card.png"
keywords:
  - OSF5
  - File format
  - JSON
last_update:
  date: 2026-05-04
  author: Optimeas GmbH
---

🇩🇪 [German version](../../de/references/osf5.md)

# OSF5 — Specific Documentation

This document describes all aspects of the **Open Streaming Format Version 5 (OSF5)** that go beyond the general OSF description.  
It complements [`osf_general.md`](../osf_general.md), which explains all structures common to OSF4 and OSF5.

OSF5 is the evolution of OSF4. It uses **JSON** as the standard format for the metablock and simplifies the control byte.  
At the same time, OSF5 remains fully **backward-compatible** with OSF4.



## Magic header in OSF5

- **Allowed identifiers:**  
  - `OSF5`  
  - `OSF4` (for backward compatibility)  
  - `OCEAN_STREAM_FORMAT4` (legacy identifier, still emitted by deployed devices)  
  - `OCEAN_STREAMING_FORMAT4` (older historical spelling)

- **Format:**  
  ```
  OSF5 84512\n
  ```

- **Metablock detection:**  
  - First character `<` → XML (OSF4 metablock)  
  - First character `{` → JSON (OSF5 metablock)  

- **Properties:**  
  - **JSON metablock** by default.  
  - OSF5 parsers can read OSF4 XML.



## Metablock in OSF5 (JSON)

OSF5 uses **JSON** as the default format for the metablock.  
The structure is functionally equivalent to the XML variant in OSF4, but is easier to parse and optimized for embedded systems.

### Example:

```json
{
  "osf": {
    "version": 5,
    "created_utc": "2025-07-27T12:00:00Z",
    "creator": "smartdevice:15002000001",
    "channels": [
      {
        "index": 0,
        "name": "Sensor.Temperature",
        "channeltype": "scalar",
        "datatype": "double",
        "timeincrement": 1000000,
        "sizeoflengthvalue": 2,
        "physicalunit": "°C"
      }
    ]
  }
}
```

- **Difference from OSF4:** JSON instead of XML, but the same logical content.  
- **Compatibility:** OSF5 can still interpret OSF4 XML.


## Simplifications in the control byte (block types)

OSF5 keeps the block structure from OSF4 but reduces the number of used block types and simplifies their interpretation.

### Changes versus OSF4:

- `bcContinuedRelStampData` is no longer used.  
- `bcStatusEvent` and `bcMessageEvent` are dropped.  
- Bit 7 for single/multi-value remains unchanged.  

### Supported block types in OSF5:

| Value | Enum               | Description |
|-------|--------------------|--------------|
| 0     | `bcReserved`       | Reserved for internal use. |
| 2     | `bcTimebaseRealign`| Time-axis adjustment (rarely used). |
| 5     | `bcContinuedData`  | Continue data with a fixed sample rate. |
| 6     | `bcStartData`      | Start block with a fixed sample rate. |
| 8     | `bcAbsTimeStampData` | Data with an absolute timestamp. |


## Supported data types in OSF5

OSF5 supports the same data types as OSF4.

| Data type  | Size     | Description |
|------------|----------|--------------|
| `bool`     | 1 byte   | True/false |
| `int8`     | 1 byte   | Signed integer |
| `int16`    | 2 bytes  | Signed integer |
| `int32`    | 4 bytes  | Signed integer |
| `int64`    | 8 bytes  | Signed integer |
| `uint8`    | 1 byte   | Unsigned integer, range 0 … 255 |
| `uint16`   | 2 bytes  | Unsigned integer, range 0 … 65 535 |
| `uint32`   | 4 bytes  | Unsigned integer, range 0 … 4 294 967 295 |
| `uint64`   | 8 bytes  | Unsigned integer, range 0 … 18 446 744 073 709 551 615 |
| `float`    | 4 bytes  | IEEE 754 single precision |
| `double`   | 8 bytes  | IEEE 754 double precision |
| `string`   | variable | UTF-8 encoded, length defined by block size. Ends with a trailing null byte (`0x00`) — see [`osf_general.md`](../osf_general.md#note-on-null-termination-of-string-and-binary). |
| `binary` *(alias: `bytearray`)* | variable | Arbitrary byte sequences for image, audio, or other binary data with a MIME type. Maximum length is determined by `sizeoflengthvalue`. Ends with a trailing null byte (`0x00`) — see [`osf_general.md`](../osf_general.md#note-on-null-termination-of-string-and-binary). |
| `gpslocation` | 24 bytes | Structure for GPS positions |

## Null termination

For `bcAbsTimeStampData` with `datatype=string` or `datatype=binary`, the rule described in [`osf_general.md`](../osf_general.md#note-on-null-termination-of-string-and-binary) applies: a trailing null byte (`0x00`) at the end of the data field. This rule is identical to OSF4.

### `bcStartData` with sample rate

As in the general specification and in OSF4, `bcStartData` in OSF5 carries a `double` field with the **sample rate (Hz)** directly after the `int64` start timestamp. It applies to all subsequent `bcContinuedData` blocks of the same channel until a new `bcStartData` block is written. Multiple `bcStartData` blocks per channel with time gaps between them are explicitly allowed.

**`bcStartData` example in OSF5:**
`[int64 StartTime] [double SampleRate] [uint32 N] [double Value1] [double Value2] ... [double ValueN]`

## Trailer and info block

OSF5 **no longer supports an info data block (0xFFFF) or a magic trailer.**  
The end of the file is defined exclusively by reaching the last fully written data block.

- **Consequence:**  
  - Parsers read the file up to the last consistent block.  
  - There is no additional statistics or metadata at the end of the file.  
  - To obtain time-interval information the file must be analyzed in full or in part.


### Why OSF5 no longer uses a trailer or info block

In OSF4, an optional **info data block (index 0xFFFF)** and a **magic trailer** were available at the end of the file to make metadata and the file's time interval quickly accessible.  
In OSF5 we have deliberately removed both elements.

### Main reason: reduced implementation effort

- Writing the info block requires final aggregation of all channels and time information.  
- On the read side it adds extra code that must be maintained and tested alongside the regular data stream.  
- Since OSF is designed to be readable up to the last fully written block anyway, trailer and info block do not provide technical value that justifies the effort.

### Additional advantages

- **Simpler specification:** fewer special cases, leaner implementation on both embedded and PC sides.  
- **No duplicated information:** all relevant metadata is present in the metablock and in the data blocks themselves.  
- **Analysis tools can do the same:** time intervals and statistics can be derived while reading from the regular blocks.

> **Conclusion:**  
> Dropping the trailer and info block in OSF5 is primarily a decision in favor of a simpler, more maintainable implementation.  
> The robustness of the format is fully preserved — it stems from the block structure, not from additional elements at the end of the file.


## Specifics and limitations of OSF5

- **JSON** as the primary format; XML only for backward compatibility.  
- **Simplified control byte** with fewer block types.  
- No trailer or info data block at the end of the file.  
- `bcContinuedRelStampData`, `bcStatusEvent`, `bcMessageEvent` are no longer produced.



## Example of an OSF5 file

```text
OSF5 2048
{
  "osf": {
    "version": 5,
    "created_utc": "2025-07-27T12:00:00Z",
    "creator": "smartdevice:15002000001",
    "channels": [
      {
        "index": 0,
        "name": "Sensor.Temperature",
        "channeltype": "scalar",
        "datatype": "double",
        "timeincrement": 1000000
      },
      {
        "index": 1,
        "name": "Sensor.Force",
        "channeltype": "scalar",
        "datatype": "double",
        "physicalunit": "N"
      },
      {
        "index": 2,
        "name": "Sensor.Path",
        "channeltype": "scalar",
        "datatype": "double",
        "physicalunit": "mm"
      }
    ]
  }
}
[BEGIN OF BINARY DATA]
```
