---
title: OSF Format Description
description: Description of common properties of OSF 4 and 5
#hide_title: true/false
#hide_table_of_contents: true/false
#sidebar_label: Label shown in the sidebar
sidebar_position: 2
image: "/img/om_social_card.png"
keywords:
  - File format
  - OSF4
  - OSF5
  - OSF
last_update:
  date: 2026-05-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

🇩🇪 [German version](../de/osf_general.md)

## General description of the OSF format
- Applies to format versions 4 and 5 -

The **Open Streaming Format (OSF)** is a binary, block-oriented data format for the continuous recording of time-related measurement and process data. It is designed not only for **streaming on embedded systems with limited resources** but also for the **block-wise, efficient processing of large data sets** on powerful analysis platforms — whether on servers, PCs, or in post-processing directly on embedded devices.


### Core principles

* **Time as the central axis:** All data is stored with explicit time information — equidistant on a fixed grid or individually with timestamps.
* **Streaming-capable:** Data can be written continuously to the file during an ongoing measurement, without prior knowledge of the total amount.
* **Robustness:** Even after a power loss or unexpected shutdown, all data written up to the last completed block remains readable.
* **Open structure:** A combination of a clear metablock (XML in OSF4, JSON in OSF5) and a simple binary data stream.
* **Block-wise storage:** Instead of writing individual values sequentially, data blocks are used. This reduces write operations and enables fast loading of large data sets.
* **Flexibility:** Support for various data types — from simple scalars through vectors and matrices to binary data such as images or audio files.
* **Metadata:** Each channel can be described with a name, physical unit, dimension, and optional supplementary information.

### Basic structure of an OSF file

Regardless of version 4 or version 5, every OSF file follows the same basic schema:

![OSF file structure.](media/dfa207f25f79c84ec79e2e2f6c2c42cc.jpg)


1. **Magic header**

   * Format identifier (OSF4, OSF5, OCEAN\_STREAM\_FORMAT4, OCEAN\_STREAMING\_FORMAT4)
   * Length of the following metablock

2. **Metablock (XML or JSON)**

   * Contains information about channels, data types, physical units, and context data
   * Defines the structure of the following data blocks

3. **Binary data blocks**

   * Contain the actual measurement values in streaming format
   * Support equidistant and time-stamped data
   * May contain single values, vectors, matrices, or binary data

4. **Optional final block**

   * In OSF4, an optional XML trailer with statistics and a channel overview
   * In OSF5, this trailer is omitted by default

### Data organization

* **Channels:** Each data stream is described as a channel containing a name, data type, physical unit, and optionally further attributes.
* **Time base:** All time values are stored in nanoseconds since epoch and enable high-precision synchronization.
* **Block header:** Each data block begins with a channel index and a length value, so the stream remains interpretable even with unknown channels or interruptions.
* **Control byte:** Defines the type and structure of the following data block (e.g. start, continuation, timestamp type). In OSF5 the use of this byte is simplified, but it remains functionally compatible.



## Magic header

Every OSF file starts with the so-called **magic header**.
It serves two purposes:

1. Unique identification as an OSF file.
2. Specification of the length of the following metablock so it can be read and parsed directly.

### Layout

The magic header is an ASCII line terminated by a line feed (`\n`).

**OSF4 example:**

```
OSF4 173762\n
```

* **OSF4** is the format identifier.
* **173762** is the length of the metablock in bytes.

**OSF5 example:**

```
OSF5 84512\n
```

* **OSF5** identifies the new version.
* **84512** is the length of the metablock, which in OSF5 is JSON by default.

### Supported identifiers

For compatibility, OSF implementations recognize multiple headers:

* **OSF4** — classic OSF4 file
* **OCEAN\_STREAM\_FORMAT4** — legacy identifier for OSF4 files; still emitted by deployed devices and therefore must be accepted by readers
* **OCEAN\_STREAMING\_FORMAT4** — older historical spelling; also to be interpreted as OSF4
* **OSF5** — OSF5 file

### Detection of the metablock format

Whether the following metablock is **XML** or **JSON** is determined by the first character after the header:

* `<` → XML (OSF4 format)
* `{` → JSON (OSF5 format)
* Any other character → error

### Advantages

* **Fast start:** Readers can immediately extract the metablock and pass it to the appropriate parser.
* **Streaming-capable:** No knowledge of the total file size is required.
* **Backward-compatible:** OSF5 processes OSF4 files (including OCEAN\_STREAM\_FORMAT4 and OCEAN\_STREAMING\_FORMAT4).
* **Simple implementation:** A single line is sufficient to determine version and parser.


The following describes the **metablock** for OSF4 and OSF5 in general, without XML/JSON specifics and without vector/matrix parameters:


## Metablock — channels and metadata

Directly after the magic header, every OSF file contains the **metablock**.
It contains all information needed to interpret the following data blocks correctly. This includes:

* **File parameters:** Context information about the file and its origin.
* **Channel definitions:** Describe each data stream with a name, data type, and physical properties.
* **Metadata:** Additional information not directly tied to a channel (e.g. system status, comments, calibration data).

The metablock acts as the file's "table of contents" and is designed to be **unambiguous, machine-readable, and easy to extend**.


### File parameters (in the metablock)

* **created\_utc** — File creation time in UTC, ISO 8601 format
* **creator** — `optional` Identification of the producer (e.g. device serial number, program name, UUID)
* **created\_at\_longitude** / **created\_at\_latitude** / **created\_at\_altitude** — `optional` geographic position at the time of file creation
* **reason** — `optional` reason for file creation (e.g. `BOOT`, `SEQUENCE`, `TRIGGERED`)
* **total\_seq\_no** — `deprecated` Absolute sequence number since system start (starting at 0)
* **triggered\_seq\_no** — `deprecated` Relative sequence number since the last trigger event (starting at 0)
* **namespacesep** — `optional` Separator for hierarchical channel names (default `"."`)
* **tag** — `optional` Free tag for classifying the file (e.g. `preview`)
* **comment** — `optional` Optional comment text


### Channel definitions (channel)

Each channel describes one data stream within the file.
The parameters are described below.


#### **Identification and organization**

* **index**
  Unique channel index within the file (starting at 0).
* **name**
  Channel name, optionally with a hierarchical path (e.g. `Motor.Temperature`).
* **reference**
  Optional unique reference or UUID identifying the data origin.


#### **Time base**

* **timeincrement**
  Fixed time increment in nanoseconds for equidistant channels.
  *Value = 0 or unset → channel uses per-value timestamps.*

  **Note:** The `timeincrement` in the metablock is an **optional hint**. For high-resolution or trigger-based recordings, the exact sample rate is often unknown when the header is created. The **effective sample rate is delivered in every `bcStartData` block as a `double`** and applies from that point on to all subsequent `bcContinuedData` blocks of the same channel, until a new `bcStartData` block is written. This applies to **OSF4** as well as **OSF5**.


#### **Data types and structure**

* **datatype**
  Data type of the stored values (e.g. `bool`, `int32`, `double`, `string`, `gpslocation`).
  → *A complete description of all data types and their encoding is in the [Data types](#datatypes) chapter.*

* **channeltype**
  Structural type of the channel:

  * `scalar` — single values over time
  * `binary` — arbitrary binary blocks (e.g. images)
    *(Vector and matrix are described in a separate document.)*
    → *A detailed explanation of channel types is in the [Channel types](#channeltypes) chapter.*

* **sizeoflengthvalue**
  Size of the length value preceding each data block:

  * `2` → 2 bytes (uint16, max. block size ~64 kB)
  * `4` → 4 bytes (uint32, max. block size ~4 GB)
    Used to determine the block size and to read data blocks correctly from the stream.
    *→ A detailed explanation is in the [sizeoflengthvalue](#sizeoflengthvalue) chapter.*

* **mimetype**
  Optional, MIME type for binary channels (e.g. `image/jpeg`, `audio/wav`).

* **spectrumtype**
  Optional, type of spectral data:

  * `amplitude` (default)
  * `realImag`
  * `ampPhaseRad`
  * `ampPhaseDeg`


#### **Physical properties**

* **physicalunit**
  Optional, physical unit (SI-compliant, e.g. `V`, `°C`).
* **physicaldimension**
  Optional, description of the physical dimension (`temperature`, `pressure`, …).


#### **Display and supplementary info**

* **displayname**
  Optional display name for visualization or GUI use.
* **comment**
  Optional comment for the channel.


### Metadata (info)

Metadata supplements the file with additional information not bound to a channel.
Typical parameters:

* **name** — Name of the information item
* **value** — Value (as string or typed)
* **datatype** — Type of the value (`string`, `int32`, `float`, `binary`, `gpslocation`, etc.)
* **physicalunit** — Optional, physical unit of the value

Metadata is freely definable and is suited for:

* System or device data
* Comments and status messages
* Calibration values
* User-defined supplementary information

> **Note:** `bytearray` is an alias for `binary`. Both names are valid in OSF4 and OSF5 and are interpreted identically by readers. Writers **should** use `binary` consistently when writing; `bytearray` remains readable for backward compatibility.


### Advantages of the structure

* **Clear separation of data and description:** The metablock defines how data is interpreted without containing measurement values itself.
* **Self-describing:** Files can be read and interpreted without external definitions.
* **Extensible:** New channels, data types, or metadata can be added without changing the basic format.
* **Robust:** Fixed indices and length values keep the file interpretable even when not all channels are known.



## Core parameters of the channel description

The following parameters define the basic structure of a channel in OSF and determine how data is stored and interpreted in the streaming format. They are relevant for all channels and form the foundation of the channel description.

<a name="datatypes"></a>
### Data types (datatype) {#datatypes}

The `datatype` parameter defines the data format of a channel's values. Each value is stored in a precisely defined binary format.

#### Supported data types and encoding:

| Data type | Size (bytes)  | Description                                                                                                                                          |
| --------- | ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `bool`    | 1             | True/false (0 = false, 1 = true)                                                                                                                     |
| `int8`    | 1             | Signed integer                                                                                                                                       |
| `int16`   | 2             | Signed integer                                                                                                                                       |
| `int32`   | 4             | Signed integer                                                                                                                                       |
| `int64`   | 8             | Signed integer                                                                                                                                       |
| `uint8`   | 1             | Unsigned integer, range 0 … 255                                                                                                                      |
| `uint16`  | 2             | Unsigned integer, range 0 … 65 535                                                                                                                   |
| `uint32`  | 4             | Unsigned integer, range 0 … 4 294 967 295                                                                                                            |
| `uint64`  | 8             | Unsigned integer, range 0 … 18 446 744 073 709 551 615                                                                                               |
| `float`   | 4             | IEEE 754 single precision                                                                                                                            |
| `double`  | 8             | IEEE 754 double precision                                                                                                                            |
| `string`  | variable      | UTF-8 encoded, length defined by block size. On disk: followed by a trailing null byte (`0x00`) in OSF4, no trailing byte in OSF5 — see the note block below for the rules. |
| `binary` *(alias: `bytearray`)* | variable | Arbitrary byte sequences for image, audio, or other binary data with a MIME type. The maximum block size is determined by the channel's `sizeoflengthvalue` field. On disk: followed by a trailing null byte (`0x00`) in OSF4, no trailing byte in OSF5 — see the note block below for the rules. |
| `gpslocation` | 24        | Structure for GPS positions (see below)                                                                                                              |

> **Note on integer types:** Integer values (`int8`, `int16`, `int32`, `int64`, `uint8`, `uint16`, `uint32`, `uint64`) are typically used in OSF files for **states, status information, or counter values**, not as scaled raw values of a physical quantity. For this reason OSF deliberately has **no** `scale`/`offset` parameters for conversion to physical values — physical quantities are stored directly as `float` or `double`.

#### Note on null termination of `string` and `binary` {#note-on-null-termination-of-string-and-binary}

> **Note on null termination of `string` and `binary`:**
> The trailing `0x00` byte on `string` and `binary` payloads in `bcAbsTimeStampData` is a historical artefact from the Qt `QString` serialisation used in the original Optimeas devices. The block length is already determined by `sizeoflengthvalue`, so a null-terminator as a sentinel is redundant. For binary payloads it is an active stumbling block: a reader that leaves the trailing byte in place produces invalid output (a JPEG file with a trailing `0x00` is no longer a valid JPEG). Equally, a reader that strips a byte from an OSF5 binary payload that never had one (an ASN.1 blob that legitimately ends in `0x00`, a protobuf message, a null-terminated string stored as binary) cuts off a real data byte. The rule is therefore tied to the on-disk format version, so neither writers nor readers have to guess.
>
> **OSF4:**
>
> - **Writers MUST** append a single trailing `0x00` byte after every `string` and `binary` payload in `bcAbsTimeStampData`.
> - **Readers MUST** strip the last byte of the payload unconditionally — the byte is guaranteed to be present.
>
> **OSF5:**
>
> - **Writers MUST NOT** append any trailing byte. The payload ends at the last data byte; `sizeoflengthvalue` defines the exact length.
> - **Readers MUST NOT** strip any trailing byte. A trailing `0x00` is treated as a regular data byte.
>
> The effective payload length is therefore: block length in OSF5, block length minus one byte in OSF4. There is no heuristic and no sentinel-detection step.

#### `gpslocation` structure

```c
struct gps_location {
    double latitude;   // Latitude
    double longitude;  // Longitude
    double altitude;   // Altitude
};
```

> **Note:** For channels with `datatype="binary"` it is recommended to define the MIME type (`mimetype`) on the channel (e.g. `image/jpeg`, `audio/wav`) so the data can be interpreted unambiguously. The maximum block size is determined by the channel's [`sizeoflengthvalue`](#sizeoflengthvalue) parameter.


<a name="channeltypes"></a>
### Channel types (channeltype) {#channeltypes}

The `channeltype` parameter defines the **logical organization of a channel's values**. It specifies how many values are stored per data block and what structure those values have.

OSF defines three basic channel types:


#### scalar

* **Description:**
  A channel with exactly one value per point in time.
  Typical use: continuous physical quantities (temperature, voltage, pressure) or digital signals (e.g. door status).

* **Properties:**

  * Each data block contains one or more samples with a single value.
  * Supports equidistant sampling via `timeincrement` or per-value timestamps.
  * The simplest and most frequently used channel type.

* **XML example (OSF4):**

  ```xml
  <channel 
      index="0"
      name="Sensor.Temperature"
      channeltype="scalar"
      datatype="double"
      physicalunit="°C"/>
  ```

* **JSON example (OSF5):**

  ```json
  {
    "index": 0,
    "name": "Sensor.Temperature",
    "channeltype": "scalar",
    "datatype": "double",
    "physicalunit": "°C"
  }
  ```



#### vector

* **Description:**
  A channel in which each data block contains a **sequence of multiple values** that logically belong together.
  Typical use: frequency spectra (FFT), time-series segments, multi-channel recordings in one block.

* **Properties:**

  * Vector length may vary per block.
  * Reduces overhead at high sample rates because multiple values are written per block.
  * Can be used with per-block timestamps or with a fixed time increment.
  * Requires additional parameters for axis information (separate document).

* **XML example (OSF4):**

  ```xml
  <channel 
      index="2"
      name="FFT.Magnitude"
      channeltype="vector"
      datatype="float"
      physicalunit="dB"/>
  ```

* **JSON example (OSF5):**

  ```json
  {
    "index": 2,
    "name": "FFT.Magnitude",
    "channeltype": "vector",
    "datatype": "float",
    "physicalunit": "dB"
  }
  ```



#### matrix

* **Description:**
  A channel that stores a **two-dimensional data structure** per timestamp.
  Typical use: rainflow classifications, heatmaps, 2D sensor arrays, image data.

* **Properties:**

  * Matrix size may vary per block.
  * Enables complex data structures on a unified time base.
  * Requires additional parameters for row and column descriptions (separate document).

* **XML example (OSF4):**

  ```xml
  <channel 
      index="5"
      name="Rainflow.Matrix"
      channeltype="matrix"
      datatype="int32"
      physicalunit="counts"/>
  ```

* **JSON example (OSF5):**

  ```json
  {
    "index": 5,
    "name": "Rainflow.Matrix",
    "channeltype": "matrix",
    "datatype": "int32",
    "physicalunit": "counts"
  }
  ```



#### Notes on vector and matrix

* **Additional parameters:** Both types require metadata about dimensions, axes, physical units, and optionally labels. These are described in detail in dedicated documents.
* **Efficiency:** Vector and matrix channels reduce write operations and are particularly suited for data with high sample rates or complex structure.
* **Flexibility:** Block size and structure may vary, allowing adaptation to different measurement scenarios.
* **Synchronization:** They share the same time base as scalar channels, so different data types can be stored exactly synchronized in one file.

---

#### Summary

* **`scalar`** — Simple channel type, one value per point in time. Ideal for continuous measurement values.
* **`vector`** — Multiple values per block, optimized for frequency spectra and high-frequency data.
* **`matrix`** — Multi-dimensional blocks, suited for classifications, image, and array data.

> **Note:** By combining these channel types, OSF covers both simple signals and complex data sets while remaining easy to implement.


<a name="sizeoflengthvalue"></a>
### Block size field (sizeoflengthvalue) {#sizeoflengthvalue}

The `sizeoflengthvalue` parameter defines the size of the length field that precedes each data block of a channel. It specifies how many bytes are used to express the block size and therefore the maximum size of a single data block.

#### Purpose

OSF is a streaming format. Each data block can be of a different size and contains a variable number of measurement values. To read these blocks correctly, their length must be known. The `sizeoflengthvalue` field specifies whether the length value uses **2 bytes** or **4 bytes**.

#### Values

* **2** — length field is 2 bytes (uint16).

  * Maximum value: 65,535 bytes per data block.
  * Default for typical measurement channels with moderate block sizes.
  * Lower memory footprint and overhead.
* **4** — length field is 4 bytes (uint32).

  * Maximum value: ~4 GB per data block.
  * Suited for channels with very large data packets, e.g. image, audio, or binary data.

#### Default value

If not specified explicitly, `sizeoflengthvalue="2"` is used.

#### Effects

* **Memory footprint:** 2 bytes save space for small blocks; 4 bytes enable large data.
* **Readability:** Before interpreting a block, the reader must read the length value and treat the next `N` bytes as the block.
* **Robustness:** Even if writing was interrupted, the reader can correctly skip blocks and find the next valid block.

#### Recommendations

* For continuous signals and channels with scalar values → use `2`.
* For binary channels with images, audio, or large data packets → choose `4`.
* Use a consistent choice per channel; it can be set per channel in the channel definition.


## Data blocks

**Data blocks** are the heart of the OSF format. They contain the actual measurement values and are structured so that they can be written and read efficiently both during continuous streaming on embedded systems and during block-wise processing on servers and PCs. Each data block is self-contained and remains interpretable even if the recording is aborted.

### Introduction

In OSF, all measurement values are stored in **data blocks**. Each block is a self-contained unit that contains one or more values of a channel and is associated with time information.  
The block concept enables two central properties of the format:

- **Continuous streaming**: Values can be written sequentially during recording without knowing the entire file structure.  
- **Efficient processing**: Block-wise storage allows fast loading and processing of large data sets on servers, PCs, or in post-processing.

Each data block is designed so that even after a sudden interruption of the measurement (e.g. power loss) it remains readable up to the last fully written unit.  
The data block structure is identical in OSF4 and OSF5 and forms the basis for robust, lossless recording of time-related measurement data.


### General structure of a data block

A data block in OSF consists of a fixed header structure followed by optional metadata and the actual measurement values.  
The layout is designed so that each block can be interpreted independently and remains valid even during streaming or after a file abort.

**Basic structure:**

1. **Channel index (`uint16`)**  
   - Identifies which channel the data belongs to.  
   - Corresponds to the `index` attribute in the metablock.  

2. **Length field (`uint16` or `uint32`)**  
   - Size of the following data area in bytes.  
   - The width of the field is defined by the channel parameter `sizeoflengthvalue`.  
   - Allows blocks to be skipped or, on errors, to jump correctly to the next unit.  

3. **Control byte (`uint8`)**  
   - Defines the type of the data block and contains information about the structure of the following data.  
   - The most significant bit (bit 7) indicates whether the block contains **a single value** (0) or **multiple values** (1).  
   - A complete overview of control byte values is in the *Control byte* section.  

4. **Data area**  
   - The actual measurement values or data.  
   - Format and size depend on the channel type (typically scalar) and the data type.
<br/>

### The control byte

Every data block in OSF contains a **control byte (`blockContent`)** that determines the block type and the structure of the contained data.  
In addition, the most significant bit (bit 7) indicates whether the block contains **only a single value** or **multiple values**:

- **Bit 7 = 0** → A single value in the block.  
- **Bit 7 = 1** → Multiple values in the block (N > 1).

The control byte is interpreted as an 8-bit value. The lower 7 bits define the **block type**, the highest bit defines the number of values.

<br/>

#### Overview of block types

| Value (0–8) | Enum                | Meaning                                                                                  | Data block content |
|-------------|--------------------|-------------------------------------------------------------------------------------------|--------------------|
| **0**       | `bcReserved`       | Reserved for future use. Originally *bcMetaData*, never used.                              | Variable, internal special functions |
| **1**       | `bcTrustedTimestamp` | `Deprecated` Originally intended for constant values with a "valid until" timestamp. Recommendation: have the application set sample points instead. | `int64`: absolute timestamp (ns since epoch) |
| **2**       | `bcTimebaseRealign` | `Deprecated` Time-axis adjustment. Can be replaced if needed by writing a new block with an absolute start time. | `int64`: absolute timestamp<br/>`int64`: time shift (ns) |
| **3**       | `bcStatusEvent`    | `Deprecated` Used to carry per-channel status information. No longer used. | `int64`: absolute timestamp<br/>`uint32`: status word |
| **4**       | `bcMessageEvent`   | `Deprecated` Can be fully replaced by `bcAbsTimeStampData` with `datatype=string`. | `int64`: absolute timestamp<br/>`string`: text |
| **5**       | `bcContinuedData`  | Continue equidistant data with a fixed sample rate. With bit 7 set, multiple values per block. | `[uint32 N]`: number of samples (only if bit 7 set)<br/>`N` × data values |
| **6**       | `bcStartData`      | First data block with a fixed sample rate; additionally carries the sample rate effective from this block onward (e.g. on a trigger). Always contains an absolute start timestamp. | `int64`: absolute timestamp<br/>`double`: sample rate (Hz)<br/>`[uint32 N]`: number of samples (only if bit 7 set)<br/>`N` × data values |
| **7**       | `bcContinuedRelStampData` | `Deprecated`, `supported on read in OSF5` Originally used to save 4 bytes per sample with relative timestamps. | `[uint32 N]`: number of samples (only if bit 7 set)<br/>`N` × (`uint32` relative time + data value) |
| **8**       | `bcAbsTimeStampData` | Data blocks with an absolute timestamp per value. Now also supports strings and binary data in combination with `datatype` and `mimetype`. | `[uint32 N]`: number of samples (only if bit 7 set)<br/>`N` × (`int64` absolute time + data value) |


Block-type restrictions with respect to channel information:

| Enum type      | Equidistant data | Time-stamped data |
| ------------- |-------------| -----|
| bcStartData            | allowed | not allowed |
| bcContinuedData        | allowed | not allowed |
| bcContinuedRelStampData | not allowed | allowed |
| bcAbsTimeStampData | not allowed | allowed |

<br/>
### Data structure per control type

The structure of the payload in a block depends directly on the control type.  
The following sections describe how values are stored for the various data types and which restrictions apply.

#### bcStartData (equidistant data, start block)

- **Use:**  
  - Beginning of a data series with a fixed sample rate.  
  - Always contains the absolute start time of the series.  
  - Allowed only for numeric `datatype` (`int*`, `float`, `double`).  

- **Block layout:**  
  1. `int64` — absolute start timestamp (ns since epoch).  
  2. `double` — sample rate in Hz (effective from this block until the next `bcStartData`).  
  3. **[uint32 N]** — number of samples (only if bit 7 is set, otherwise 1).  
  4. **N × data values** — raw data according to `datatype`.  

- **Example `datatype=double`:** [int64 StartTime] [double SampleRate] [uint32 N] [double Value1] [double Value2] ... [double ValueN]


- **Notes:**
  - `bcStartData` may appear **multiple times per file and channel**. It is written:
    - at the start of an equidistant recording,
    - on every trigger or event that starts a new data sequence,
    - on a required time-track correction (drift compensation).
  - **Consequence for readers:** Data of an equidistant channel is produced **block- or event-wise**. Time gaps may occur between consecutive sequences of the same channel. Readers must take the effective sample rate from the currently active `bcStartData` block and must **not** assume that `timeincrement` from the metablock is always correct.

#### bcContinuedData (equidistant data, continuation)

- **Use:**  
  - Continues a series started by `bcStartData` without a new timestamp.  
  - The first value follows directly after the last value of the previous block.  
  - Allowed only for numeric `datatype` (`int*`, `float`, `double`).  

- **Block layout:**  
    1. **[uint32 N]** — number of samples (only if bit 7 is set, otherwise 1).  
    2. **N × data values** — raw data according to `datatype`.  

- **Example `datatype=int16`:** [uint32 N] [int16 Value1] [int16 Value2] ... [int16 ValueN]

- **Note:** The time per sample in a `bcContinuedData` block is `1 / SampleRate` from the most recently read `bcStartData` block of the same channel.



#### bcAbsTimeStampData (time-stamped data)

- **Use:**  
  - For channels with per-value timestamps.  
  - Supports all `datatype` values, including `string` and `binary`.

- **Block layout:**  
  1. **[uint32 N]** — number of samples (only if bit 7 is set, otherwise 1).  
  2. **N × (int64 time + data value)** — absolute timestamp + value.  

- **Example `datatype=int16`:** [uint32 N] [int64 Time1] [int16 Value1] [int64 Time2] [int16 Value2] ...
- **Example `datatype=double`:** [uint32 N] [int64 Time1] [double Value1] [int64 Time2] [double Value2] ...
- **Example `datatype=string`:**
  - Strings are stored as raw UTF-8 bytes. The effective string length is the payload length of the data field, minus one byte in OSF4 (the spec-mandated trailing `0x00`) or the full payload length in OSF5 (see the note block above for the full rules).
  - Single-sample form (bit 7 = 0, N implicit 1): [int64 Time] [UTF-8 bytes of the string]
  - Multi-sample form (bit 7 = 1) for variable-length types is not part of the standard wire format; see the note on multi-sample variable-length blocks below.

- **Example `datatype=binary`:**
  - Binary data is written as raw bytes. The channel's `mimetype` defines the interpretation. The effective payload length is the data field's payload length, minus one byte in OSF4 (the spec-mandated trailing `0x00`) or the full payload length in OSF5 (see the note block above for the full rules).
  - Single-sample form (bit 7 = 0, N implicit 1): [int64 Time] [Byte1] [Byte2] ... [Byte M]
  - Multi-sample form (bit 7 = 1) for variable-length types is not part of the standard wire format; see the note on multi-sample variable-length blocks below.


> **Multi-sample variable-length blocks.** For `string` and `binary` data in `bcAbsTimeStampData`, writers should emit one sample per block (N=1). The multi-sample form (Bit 7 = 1 with N > 1) for variable-length types is not part of the standard wire format. Readers may encounter multi-sample variable-length blocks from older or non-standard writers; reader behavior in that case is implementation-defined. The Rust and C++ reference readers accept equal-length-segments-per-sample layouts only; the Delphi historical writer uses a per-sample `uint32` length prefix that other readers do not parse.

- **Note:** With multiple samples per block (`N>1`), numeric types must have a fixed wire size per sample. Multi-sample variable-length blocks are not standard; see the note on multi-sample variable-length blocks above.



#### bcContinuedRelStampData (time-stamped, relative)

- **Use:**  
  - For channels with per-value timestamps and relative intervals.  
  - **No longer used from OSF5 onwards**, but remains for OSF4 readers.

- **Block layout:**  
  1. **[uint32 N]** — number of samples (only if bit 7 is set, otherwise 1).  
  2. **N × (uint32 Δt + data value)** — relative time interval in ns + value.  

- **Example `datatype=int16`:** [uint32 N] [uint32 Δt1] [int16 Value1] [uint32 Δt2] [int16 Value2] ...
- See bcAbsTimeStampData for further examples.


- **Note:**  
- Originally introduced to save 4 bytes per sample.  
- Removed in OSF5 in favor of a simpler implementation.



#### Restrictions

- **Equidistant channels (bcStartData, bcContinuedData):**
  - Only direct numeric data types (`int*`, `float`, `double`).  
  - No strings, no binary data, no complex structures.  

- **Time-stamped channels (bcAbsTimeStampData, bcContinuedRelStampData):**
  - Support all data types.
  - Strings and binary data carry a trailing `0x00` byte in OSF4 (stripped by readers) and no trailing byte in OSF5; see the note block on null-byte handling for the deterministic rules.

#### Important points

- **Compatibility:**  
  - OSF5 can read all OSF4 block types.  
  - From OSF5 onwards, `bcContinuedRelStampData`, `bcStatusEvent`, and `bcMessageEvent` are no longer produced.  
  - `bcTrustedTimestamp` is ignored and is marked as *deprecated*.

- **Implementation:**  
  - Readers must always check bit 7 to correctly interpret single- vs. multi-value blocks.  
  - Unrecognized block types can be skipped using the length value.

- **Strings and binary data:**
  - For `bcAbsTimeStampData` with `datatype=string` or `datatype=binary`, the trailing null byte (`0x00`) is **always present in OSF4** (writer must append, reader must strip) and **never present in OSF5** (writer must not append, reader must not strip). See the note block on null-byte handling for the full rules.
  - The block length is determined by `sizeoflengthvalue`.
  - Binary data uses `datatype=binary` plus `mimetype`.
<br/>



## File completion and magic trailer

Optionally, an **info data block** with the special channel index `0xFFFF` may be written at the end of an OSF file.  
This block provides metadata about the completed data stream and marks the regular end of the file.  

### Info data block (channel index 0xFFFF)

- **Purpose:**  
  Provides a quick overview of the time interval and segmentation of the file without having to read all data blocks.  
  Useful for analysis and indexing tools.

- **Layout:**  
    1. `uint16` — channel index (`0xFFFF`)  
    2. `uint32` — length of the following options block  
    3. `uint8` — control byte (always `bcReserved` / 0)  
    4. `string` — UTF-8-encoded info block (format depends on the OSF version)

#### Example (OSF4, XML):

```xml
<trailer finalized_utc="2019-08-12T12:23:01+02:00" reason="fileStartGrid_min">
    <channels count="8">
        <channel index="0" samples="29452" last_ns="1384899599997800000" last_utc="2019-11-19T23:19:59"/>
        <channel index="1" samples="29452" last_ns="1384899599997800000" last_utc="2019-11-19T23:19:59"/>
        <channel index="2" samples="29452" last_ns="1384899599997800000" last_utc="2019-11-19T23:19:59"/>
        <channel index="3" samples="29452" last_ns="1384899599997800000" last_utc="2019-11-19T23:19:59"/>
    </channels>
</trailer>
````

#### Example (OSF5, JSON):

```json
{
  "trailer": {
    "finalized_utc": "2019-08-12T12:23:01+02:00",
    "reason": "fileStartGrid_min",
    "channels": [
      {
        "index": 0,
        "samples": 29452,
        "last_ns": 1384899599997800000,
        "last_utc": "2019-11-19T23:19:59"
      },
      {
        "index": 1,
        "samples": 29452,
        "last_ns": 1384899599997800000,
        "last_utc": "2019-11-19T23:19:59"
      }
    ]
  }
}
```

* **Note:**

  * OSF4 uses XML by default for the info block.
  * OSF5 uses JSON, but for compatibility can read XML as well, although it never writes XML.

<br/>

### Magic trailer

Optionally, a **magic trailer** may follow the info data block.
It serves as a fixed marker for the end of the file and indicates where the `0xFFFF` block begins.

* **Format:**

```
OSF_STREAM_END 321316454==============
```

* The number is the position in the file at which the `0xFFFF` block starts.
* The trailer tag is padded to **40 bytes**, with `=` characters appended after the number until the length is reached.

#### Purpose of the magic trailer

* Allows the info data block at the end of the file to be located without searching the entire file.
* Eases random-access implementations and indexing of large files.
* Provides a clear marker for the regular end of an OSF file.

<br/>


### Optionality and implementation effort

* **Writing:**

  * The info block and the magic trailer are not strictly required.
  * If written, they enable fast indexing and determination of the file's time interval.
  * On embedded systems with tight resources they can be omitted.

* **Reading:**

  * Parsers must not assume the block is present.
  * Files without a trailer are interpreted up to the last fully readable block.
  * On a hard abort, the last block may be shorter than its length value indicates — in that case the reader must stop at the end of the file.

<br/>


**Advantages**

* Quick determination of the time interval and statistics without reading the entire file.
* Useful for long measurements and automated analysis.
* Enables random access for analysis tools.

**Disadvantages**

* Higher implementation effort for writing and reading.
* On a file abort, the trailer may be missing or incomplete.
* Not strictly necessary for simple streaming.

<br/>

## OSFZ — Compressed OSF files

OSF files may be compressed for storage or transport. Compressed
files typically use the `.osfz` extension and contain a complete
OSF file (OSF4 or OSF5) as the compressed payload. There is no
dedicated OSFZ magic header — detection is based on the compression
magic bytes at the start of the file.

### Supported compression formats

Readers must transparently detect and decompress both common
compression formats:

| Format | Magic bytes                                        | Specification |
|--------|----------------------------------------------------|---------------|
| gzip   | `0x1F 0x8B`                                        | RFC 1952      |
| zlib   | `0x78 0x01`, `0x78 0x5E`, `0x78 0x9C`, `0x78 0xDA` | RFC 1950      |

Both formats occur in practice: current Optimeas devices write
gzip-compressed OSFZ files; older tooling and storage pipelines
use zlib. An implementation supporting only one of the two would
fail on real field data.

### Detection

Detection is based on the first two bytes of the file:

* `0x1F 0x8B` → gzip decompression
* `0x78 0x01 / 0x5E / 0x9C / 0xDA` → zlib decompression
* otherwise → uncompressed, read the file as OSF directly

After decompression, the file begins with a regular OSF magic
header (`OSF4`, `OSF5`, `OCEAN_STREAM_FORMAT4`, or
`OCEAN_STREAMING_FORMAT4`).

### Writing

Writers MAY produce OSFZ output. When they do, the compressed container
is **gzip (RFC 1952)**.

**Streaming writers** (those that write blocks incrementally with per-block
`fsync`) must treat compression as a **post-finalization** step, decoupled
from the write path. Compressing inline would make a power-loss truncation
undecompressable and defeat the best-effort durability guarantee. The
compression step runs after the writer is closed, either as a low-priority
background thread (the process must not exit before it completes) or as an
external CLI process. The source OSF file must be retained until the OSFZ
has been successfully written and fsync'd; no read-back verification is
required before deletion.

**Block writers** (those that buffer the entire file in memory and write it
atomically) MAY compress inline and emit OSFZ directly, because there is no
partial-stream or power-loss truncation risk.

<br/>

## Next steps

The chapter so far describes the general layout of the **Open Streaming Format (OSF)** and all components that apply to **OSF4 and OSF5 alike**.  
For a complete implementation or deeper integration, the following further topics are available:

- **OSF4 and OSF5 specifics:**  
  - Details of the respective header formats (XML vs. JSON)  
  - Differences in the control byte and the trailer  
  - Backward compatibility and implementation notes
  - Continue with [OSF4](references/osf4.md) or [OSF5](references/osf5.md)

- **Vectors and matrices:**  
  - Extended channel types for multi-dimensional data  
  - Additional parameters for axes, dimensions, and physical units  
  - Examples for FFTs, classifications, and image data  

- **Examples:**  
  - Complete OSF files (OSF4/XML and OSF5/JSON) with header, metablock, and data blocks  
  - Hex dumps and annotated structures  

- **Source-code access and open source:**  
  - Reference implementations for OSF4 and OSF5  
  - Parser and writer libraries for various platforms  
  - Sample code for embedded systems and PC analysis

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
