# OSF vs. HDF5 — Format Comparison

**Comparison of the OSF (Open Streaming Format) and HDF5 (Hierarchical Data Format 5) data formats with respect to flexibility, simplicity, and mutual mappability**

Status: 2026-05-19
Sources:
- OSF: `V:/github/osf/docs/de/osf_general.md` + Rust reference implementation (`V:/github/osf/implementations/rust/osf-core`)
- HDF5: `V:/github/hdf5/src/H5*public.h` + Doxygen documentation (`V:/github/hdf5/docs/doxygen/dox`)

---

## 1. Goals of the Formats

| Aspect | OSF | HDF5 |
|---|---|---|
| **Primary purpose** | Robust streaming and universal container for time-related data of any kind — from measurement data to documents to n-dimensional matrices | General-purpose container for scientific data of arbitrary structure |
| **Typical domain** | Embedded measurement devices, sensors, telemetry, process data | Scientific computing, simulation, ML, climate data, NetCDF backend |
| **Design focus** | Sequential writing with an unknown final size; readable at any time | Random access, hierarchical organization, arbitrary data types |
| **Origin** | Industrial measurement technology (optiMEAS) | NCSA / The HDF Group |
| **File extension** | `.osf`, `.osfz` (compressed) | `.h5`, `.hdf5` |
| **Spec scope** | ~875 lines of Markdown | Several hundred pages of Doxygen documentation + format spec |

OSF is primarily a **robust streaming format** — through its ability to write complete blocks of arbitrary content, but at the same time also a **universal container for time-related data of any kind**: from classic measurement series to documents and images to arbitrarily n-dimensional matrices. Its central advantage is that this can be specified and implemented very **simply and quickly**. HDF5, by contrast, is a **universal container format with almost arbitrary flexibility**, but considerably higher complexity.

---

## 2. Conceptual Comparison (Table)

| Concept | OSF | HDF5 |
|---|---|---|
| **Top-level structure** | Magic header + metablock + block stream | Superblock + root group |
| **Hierarchy** | Flat channel list (hierarchy only via name separator) | True nested groups |
| **Data unit** | Channel (sensor/measurement stream) | Dataset (N-dim array) |
| **Data model** | 1D time series per channel (scalar/vector/matrix) | Arbitrary N-dim arrays |
| **Metadata carrier** | Metablock (XML in OSF4 / JSON in OSF5) + info block | Attributes (on groups or datasets) |
| **Data types** | 14 predefined types (bool, int8..64, uint8..64, float, double, string, binary, gpslocation) | Arbitrary atomic + Compound/VLEN/Array/Enum/Reference/Opaque/Complex |
| **Time axis** | First-class concept (int64 ns since Epoch in every block) | Not in the format — must be modeled as a dataset/attribute yourself |
| **Write mode** | Append-only, streaming-native | Random access with chunked storage, streaming via SWMR |
| **Compression** | Optional at the file level (gzip/zlib → .osfz) | Filter pipeline per dataset (Deflate, Szip, Shuffle, Bitshuffle, Blosc, Custom) |
| **Endianness** | Fixed little-endian | Selectable per dataset (LE/BE) |
| **Format versions** | Exactly 2 (OSF4, OSF5) | Multiple (LIBVER_EARLIEST/V18/V110/V112/V114/V200) with fine granularity |
| **Robustness on crash** | File valid at any time, all data stored so far is readable | Requires flush/SWMR; cache loss possible |
| **Index/random access** | Optional magic trailer with position; otherwise sequential scan | Complete B-tree / fractal-heap indexes |
| **Self-describing** | Yes (metablock contains all channel types) | Yes (each dataset carries type + shape) |

---

## 3. Flexibility — Detailed Analysis

### 3.1 Structural Flexibility

**HDF5 clearly superior.** HDF5 allows:
- Arbitrarily deeply nested group hierarchies
- Various link types (hard / soft / external links to other files)
- Heterogeneous data structures in one file (tables, images, scalars, tensors side by side)
- Variable-length data types (VL strings, VL arrays of different lengths per element)
- Compound types (structs) with arbitrary nesting
- N-dimensional arrays with rank up to 32
- Region references (references to a subset of another dataset)

**OSF** offers:
- Flat channel list (hierarchy only by convention via name separator, such as `Motor.Block.Temperatur`)
- Three fixed channel types (scalar, vector, matrix)
- No cross-channel references
- No variable structuring — all samples of a channel have an identical shape

### 3.2 Data Type Flexibility

| Requirement | OSF | HDF5 |
|---|---|---|
| Standard scalars (int, float) | ✅ | ✅ |
| Strings | ✅ (UTF-8, null-terminated) | ✅ (fixed + variable length) |
| Structures (compound) | ❌ (only via vector with the same subtype) | ✅ (freely combinable) |
| Enums | ❌ (as int + documentation) | ✅ (H5T_ENUM) |
| Bitfields | ❌ | ✅ (H5T_BITFIELD) |
| Binary blobs | ✅ (binary with MIME type) | ✅ (H5T_OPAQUE) |
| Complex numbers | ❌ | ✅ (since HDF5 2.0) |
| Half floats (f16) | ❌ | ✅ (since 1.14.4) |
| References to other data | ❌ | ✅ (object/region references) |
| Geolocation | ✅ (gpslocation as first-class) | only rebuilt via compound |

### 3.3 Metadata Flexibility

**OSF**: Metadata is **strictly schematized** in the metablock — fixed fields per channel (`physicalunit`, `displayname`, `comment`, `reference`, `mimetype`, etc.) plus a generic `infos[]` list for free key-value pairs.

**HDF5**: Attributes can be attached **freely** to any group or any dataset, with an arbitrary datatype and shape — including arrays of attributes, compound attributes, etc.

### 3.4 Extensibility at Runtime

| Operation | OSF | HDF5 |
|---|---|---|
| Append new samples | ✅ Trivial (append block) | ✅ Via `H5Dset_extent` + hyperslab (chunking required) |
| Create a new channel/dataset | ❌ Not after the metablock has been written | ✅ At any time (not in SWMR mode) |
| Modify existing data | ❌ Append-only | ✅ Random write allowed |
| Delete data | ❌ | ⚠️ Logically yes, but space is not freed without `h5repack` |

### 3.5 Flexibility Rating

```
OSF        ████░░░░░░  4/10   Specialized, clearly delimited
HDF5       █████████░  9/10   Almost universal
```

---

## 4. Simplicity — Detailed Analysis

### 4.1 Specification Effort

**OSF**: A single Markdown document (~875 lines) covers the entire format. The Rust reference implementation comprises ~10,000 lines of code in 16 clearly arranged modules.

**HDF5**: The format specification comprises hundreds of pages and several layers (superblock, B-trees v1/v2, fractal heap, local heap, global heap, symbol table, object header messages, filter pipeline, etc.). The C library has several hundred public API functions.

### 4.2 Implementation Effort

| Task | OSF | HDF5 |
|---|---|---|
| Minimal reader from scratch | ~500 LOC | practically impossible without libhdf5 |
| Minimal writer from scratch | ~1000 LOC | practically impossible without libhdf5 |
| Embedded suitability | Very good (no dependencies) | Difficult (libhdf5 is large: >5 MB binary) |
| Language bindings required | Optional (clear binary format) | Practically mandatory (libhdf5 is in C) |

### 4.3 API Complexity

**OSF (Rust example)** — read a file:
```rust
let manager = DataManager::load_from_file("data.osf")?;
for channel in manager.channels() {
    println!("{}: {} samples", channel.name(), channel.sample_count());
}
```

**HDF5 (C example)** — read a dataset:
```c
hid_t file  = H5Fopen("data.h5", H5F_ACC_RDONLY, H5P_DEFAULT);
hid_t dset  = H5Dopen2(file, "/values", H5P_DEFAULT);
hid_t space = H5Dget_space(dset);
hsize_t dims[1];
H5Sget_simple_extent_dims(space, dims, NULL);
double* buf = malloc(dims[0] * sizeof(double));
H5Dread(dset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf);
H5Sclose(space); H5Dclose(dset); H5Fclose(file);
```

HDF5 forces very precise resource management (each `hid_t` must be closed explicitly) and a three-layer separation of **datatype**, **dataspace**, and **property list**.

### 4.4 Cognitive Load for Format Consumers

**OSF**: Anyone who wants to read the format must understand:
- Magic header
- Metablock (XML or JSON)
- Block structure (index + length + control byte + payload)
- 4 active block types (Start/Continued/AbsTimestamp/ContinuedRelStamp)
→ ~6 concepts

**HDF5**: Anyone who wants to read the format at the byte level must understand:
- Superblock (4 versions)
- B-trees v1 / v2 / fractal heaps
- Object header messages (~25 types)
- Symbol tables / link info
- Chunk index types (6 different ones)
- Filter pipeline
- Free space manager
- Global heap / local heap
→ ~30+ concepts

### 4.5 Simplicity Rating

```
OSF        █████████░  9/10   Specification readable in an afternoon
HDF5       ███░░░░░░░  3/10   Deep library, steep learning curve
```

---

## 5. Sensible Mapping OSF → HDF5

This direction is **well achievable and lossless**, since HDF5 is strictly more powerful than OSF.

### 5.1 Structural Mapping

```
OSF                                HDF5
─────────────────────────────      ───────────────────────────────
File                          →    File
Magic header (version)        →    Root-group attribute "osf_version"
Metablock FileInfo            →    Root-group attributes
  created_utc                 →      "created_utc" (ISO 8601 string)
  creator                     →      "creator"
  created_at_latitude/lon/alt →      "geo_lat" / "geo_lon" / "geo_alt"
  reason                      →      "reason"
  namespacesep                →      "namespace_separator"
  tag, comment                →      attributes of the same name

Channel "Motor.Block.Temp"    →    Dataset /Motor/Block/Temp
                                   (split into groups via namespacesep)

Channel metadata              →    Dataset attributes (on the dataset)
  index                       →      "osf_channel_index" (u16)
  channeltype                 →      "osf_channel_type" ("scalar"/"vector"/"matrix")
  datatype                    →      "osf_datatype"
  timeincrement               →      "sample_period_ns" (i64)
  physicalunit                →      "units"               (CF convention)
  physicaldimension           →      "physical_dimension"
  displayname                 →      "long_name"           (CF convention)
  comment                     →      "comment"
  reference                   →      "reference_uuid"
  mimetype                    →      "mime_type"           (for binary)

Infos[]                       →    Group /info with attributes
  Info{name, type, value}     →      attribute name on /info

Data blocks                   →    Content of the dataset (compound type)

Trailer (channel 0xFFFF)      →    Root-group attributes
  finalized_utc               →      "finalized_utc"
  reason                      →      "finalized_reason"
  per-channel statistics      →      attributes on the respective datasets:
                                       "sample_count", "last_timestamp_ns"
```

### 5.2 Data Mapping per Channel Type

**Scalar equidistant** → 1D dataset with compound `{int64 timestamp_ns, T value}`:
- Advantage: self-explanatory, no state required
- Alternatively: two parallel datasets `values` + `timestamps`, connected via a dimension scale

**Scalar timestamped** → identical to equidistant (compound `{timestamp_ns, value}`).

**Vector** → 2D dataset `[N_samples × vector_length]`, timestamps as a parallel 1D dataset or dimension scale.

**Matrix** → 3D dataset `[N_samples × rows × cols]`, timestamps as with vector.

**Binary with MIME type** → 1D dataset with VLEN bytes or opaque type; MIME in the attribute.

**GPSLocation** → compound `{double lat, double lon, double alt}` (1:1).

### 5.3 DataType Mapping (1:1)

| OSF | HDF5 Standard (LE) | HDF5 Native (Memory) |
|---|---|---|
| `bool` | `H5T_STD_U8LE` | `H5T_NATIVE_UCHAR` |
| `int8` | `H5T_STD_I8LE` | `H5T_NATIVE_SCHAR` |
| `int16` | `H5T_STD_I16LE` | `H5T_NATIVE_SHORT` |
| `int32` | `H5T_STD_I32LE` | `H5T_NATIVE_INT` |
| `int64` | `H5T_STD_I64LE` | `H5T_NATIVE_LLONG` |
| `uint8..64` | `H5T_STD_U{8,16,32,64}LE` | `H5T_NATIVE_U{CHAR,SHORT,INT,LLONG}` |
| `float` | `H5T_IEEE_F32LE` | `H5T_NATIVE_FLOAT` |
| `double` | `H5T_IEEE_F64LE` | `H5T_NATIVE_DOUBLE` |
| `string` | `H5T_C_S1` + `H5Tset_size(H5T_VARIABLE)` | same as above |
| `binary` | `H5T_OPAQUE` (with tag = MIME) or VLEN bytes | same as above |
| `gpslocation` | Compound `{f64,f64,f64}` | same as above |

### 5.4 Timestamp Strategies (three sensible variants)

**A) Compound with an embedded timestamp** (recommended for standard measurement data)
```
Dataset: COMPOUND { int64 timestamp_ns, T value }
        Shape: [N_samples]
```
- Advantage: self-contained, a single row = one sample
- Disadvantage: somewhat larger on disk

**B) Parallel datasets with a dimension scale** (NetCDF-compatible)
```
Dataset values:     T               Shape [N]
Dataset timestamps: int64           Shape [N]
H5DSattach_scale(values, timestamps, 0);
```
- Advantage: compatible with NetCDF-CF tools (xarray, ncview)
- Disadvantage: the two datasets must stay synchronized

**C) Implicit timestamps via start + rate** (lossy — avoid)
```
Dataset values: T               Shape [N]
Attribute "start_timestamp_ns": int64
Attribute "sample_rate_hz":     double
```
- Only sensible if OSF is guaranteed to have a single `bcStartData` segment
- Loses drift corrections, therefore generally **not recommended**

### 5.5 Mapping Block Segments into HDF5

OSF allows multiple `bcStartData` segments per channel (drift correction, trigger restart). In HDF5 there are three options:

1. **Concatenate** into a flat dataset (variant A/B with explicit timestamps) — recommended
2. **Separate segment-index dataset** with `[segment_start_index, start_timestamp_ns, rate_hz]` as an attribute or a small compound dataset, if the segment boundaries are to be preserved
3. **Sub-groups per segment** (`/channel/segment_0`, `/channel/segment_1`, ...) — very involved, only for special cases

### 5.6 Mandatory Configuration of the HDF5 Dataset

So that streaming data lands efficiently in HDF5:

```c
hid_t dcpl = H5Pcreate(H5P_DATASET_CREATE);
hsize_t chunk[1] = {8192};         // approx. 100 KB for double
H5Pset_chunk(dcpl, 1, chunk);
H5Pset_shuffle(dcpl);              // improves float compression
H5Pset_deflate(dcpl, 4);           // GZIP level 4 (good balance)

hsize_t dims[1]    = {0};
hsize_t maxdims[1] = {H5S_UNLIMITED};
hid_t space = H5Screate_simple(1, dims, maxdims);
```

### 5.7 Loss Analysis OSF → HDF5

**Transferable without loss of information:**
- All sample values (1:1 bit-exact)
- All timestamps (int64 ns)
- All metablock fields (as attributes)
- Trailer statistics
- File info including geolocation

**Requires convention/documentation:**
- Segment boundaries from `bcStartData` changes (lost with plain concatenation without a segment index)
- The difference between "equidistant" and "timestamped" (lost when both land as `(timestamp, value)` pairs — reconstructable via the attribute `osf_was_equidistant: bool`)
- OSF block sizes (irrelevant for data consumption)

**Completely lost (uncritical):**
- Block-internal sample counts
- ControlByte bits
- `sizeoflengthvalue`

→ **Conclusion:** Lossless transfer of the **logical information** is possible if OSF-specific data is carried along as attributes (`osf_*` prefix).

---

## 6. Sensible Mapping HDF5 → OSF (Reverse Direction)

This direction is **strongly limited**, since HDF5 has many concepts that do not exist in OSF.

### 6.1 What Works Without Problems

| HDF5 construct | OSF equivalent |
|---|---|
| 1D dataset of numeric scalars with a parallel timestamp column | Scalar channel, timestamped |
| 1D dataset of numeric scalars without timestamps + `start_time` + `sample_rate` as attributes | Scalar channel, equidistant |
| 2D dataset `[N × M]` with timestamps per row | Vector channel |
| 3D dataset `[N × R × C]` with timestamps per frame | Matrix channel |
| String attributes `units`, `long_name`, `comment` | physicalunit, displayname, comment |
| Root attributes `created_utc`, `creator` | FileInfo fields |

### 6.2 What Is Problematic

| HDF5 construct | Problem in OSF |
|---|---|
| Deeply nested groups | Must be collapsed into flat channel names via `namespacesep` — works, but the hierarchy becomes a convention |
| Compound datasets with > 2 fields | No direct equivalent. Options: split into multiple channels, or serialize as binary |
| Variable-length arrays per sample | Not mappable (an OSF vector has a constant length per block; variation only across blocks) |
| Enum types | Map as int + documentation |
| References (object/region) | Not mappable — discard |
| Soft/external links | Resolve or discard |
| N-dim arrays with N > 3 | Reduce to matrix (3D) or discard |
| Datasets without a time axis | Store as an info entry in the metablock, **not** as a channel (a channel implies a time series) |
| Datasets with BE endianness | Convert to LE when reading (the HDF5 library does this automatically) |
| Non-uniform chunks / filter pipeline | Transparently decompressed by libhdf5 — irrelevant for conversion |

### 6.3 Structural Mapping HDF5 → OSF (sensible subset)

```
HDF5                              OSF
─────────────────────────────     ───────────────────────────────
File                          →   File
Root-group attributes         →   Metablock FileInfo (best-effort)
  "created_utc"               →     created_utc
  "creator"                   →     creator
  "units" / "long_name"       →     (to channel attributes, not the file)
  Other unknown attrs         →     as info entries

Group /A/B/C                  →   Name component in the channel name
                                   (with "." as the separator)

Dataset 1D + timestamps       →   Scalar channel (timestamped)
Dataset 1D + start/rate attrs →   Scalar channel (equidistant)
Dataset 2D + timestamps       →   Vector channel
Dataset 3D + timestamps       →   Matrix channel
Dataset without time          →   Info entry (static metadatum)

Dataset attributes            →   Channel metadata
  "units"                     →     physicalunit
  "long_name"                 →     displayname
  "comment"                   →     comment
  Others                      →     embed into displayname/comment or
                                    lose
```

### 6.4 Heuristic for Detecting Time Series in HDF5

Since HDF5 does not know "time series" as a concept, the converter needs heuristics:

1. **Convention-based**: Look for datasets with the attribute `coordinates: "time"` (CF convention) or `_FillValue`, `units`, etc.
2. **Compound detection**: A dataset with a compound type whose first field is an int64/double named `time`, `timestamp`, `t` → interpret as a time series
3. **Sibling detection**: A dataset `values` with a sibling dataset `timestamps` of the same length → pair into a timestamped channel
4. **Dimension scale**: If a dataset has a dimension scale named `time` attached → equidistant or timestamped (depending on the scale content)

### 6.5 Loss Analysis HDF5 → OSF

**Frequently lost:**
- Depth of the group hierarchy (collapsed into names)
- Arbitrary compound structures
- N > 3-dimensional data
- Filter/chunking configuration (the data itself remains, the storage form is lost)
- Sub-group-specific attributes that do not fit the OSF schema

**Preservable only via convention:**
- Time-series semantics (HDF5 does not know what a timestamp is)

→ **Conclusion:** HDF5 → OSF works **only for a strictly limited subset** of HDF5 files — namely those that already carry time-series semantics. A universal converter is sensibly not possible.

---

## 7. Recommendations

### 7.1 When to Choose OSF?

- Embedded device with continuous sensor recording
- Strict requirement for crash robustness without flush discipline
- No C stack / no way to link libhdf5
- Streaming over a network (clear block boundaries)
- Data consumers expect time-series semantics as first-class

### 7.2 When to Choose HDF5?

- Heterogeneous data in one file (tables + images + scalars + tensors)
- Random access to large datasets
- Integration into scientific tooling (NumPy/HDFView/ParaView/MATLAB)
- ML training data
- The need to write/read selectively within datasets

### 7.3 Hybrid Strategy (recommended for the optiMEAS toolchain)

```
Embedded device  ─── OSF ───►   Edge server     ─── OSF→HDF5 ───►   Analysis platform
(robust, simple)                (conversion)                          (tools, ML, plots)
```

**Rationale:**
- OSF retains its strengths on the recording path (embedded, robust, small)
- HDF5 unfolds its strengths on the consumer path (tools, random access, standardization)
- The OSF → HDF5 conversion is **lossless** (see 5.7) and one-time

### 7.4 Concrete Recommendation for the Export Filter

1. **Compound variant (5.4.A)** as the default — self-explanatory, no synchronization problem
2. **Optional dimension-scale variant (5.4.B)** via a CLI flag for NetCDF toolchain compatibility
3. **`osf_*` prefix convention** for attributes that exist only in OSF — enables a theoretical reverse conversion
4. **`libver_bounds = V110 ... LATEST`** for broad tool compatibility
5. **Mandatory: chunking + shuffle + deflate(4)** for a reasonable file size

---

## 8. Evaluation Matrix (Summary)

| Criterion | OSF | HDF5 |
|---|---|---|
| Flexibility | ████░░░░░░ | █████████░ |
| Simplicity | █████████░ | ███░░░░░░░ |
| Tooling ecosystem | ██░░░░░░░░ | █████████░ |
| Embedded suitability | █████████░ | ██░░░░░░░░ |
| Streaming suitability | █████████░ | █████░░░░░ |
| Crash robustness | █████████░ | ██████░░░░ |
| Native time-series semantics | █████████░ | ██░░░░░░░░ |
| Random access | ███░░░░░░░ | █████████░ |
| Data type variety | ████░░░░░░ | █████████░ |
| Spec stability | █████████░ | █████████░ |

OSF and HDF5 are **complementary, not competing**. OSF wins at the recording end of the toolchain through simplicity and robustness; HDF5 wins at the analysis end through flexibility and tooling.

An **OSF → HDF5 export filter** is the naturally correct bridge — the reverse direction makes sense only for HDF5 files with already-present time-series semantics.
