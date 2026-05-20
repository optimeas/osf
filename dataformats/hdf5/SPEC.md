# OSF → HDF5 Mapping Specification

Authoritative rules for converting an OSF4/OSF5 file into a semantically
equivalent HDF5 file. Every language implementation of an OSF→HDF5
exporter follows this spec.

The conversion direction OSF → HDF5 is **lossless for the logical
information** as long as OSF-specific fields are carried along as
`osf_*`-prefixed attributes. HDF5 is strictly more expressive than OSF.

---

## 1. Data source

The exporter never re-implements OSF parsing. It consumes an
already-loaded high-level OSF data model — in the Delphi implementation
that is `TOSFDataManager`, which exposes a flat list of channels, each
carrying its samples, timestamps and metadata. The exporter iterates the
active channels and maps each to an HDF5 dataset.

There is no block/control-byte streaming on the exporter side; the data
manager has resolved all of that already.

---

## 2. File level → root attributes

| OSF source | HDF5 root attribute | Type |
|---|---|---|
| Magic-header version | `osf_version` | string (`"OSF4"` / `"OSF5"`) |
| `created_utc` | `created_utc` | string, ISO 8601 |
| `creator` | `creator` | string |
| `created_at_latitude` | `geo_lat` | f64 |
| `created_at_longitude` | `geo_lon` | f64 |
| `created_at_altitude` | `geo_alt` | f64 |
| `reason` | `reason` | string |
| `namespacesep` | `namespace_separator` | string |
| `tag` | `tag` | string |
| `comment` | `comment` | string |
| `infos[]` | group `/info` with one same-named attribute per entry | — |
| trailer `finalized_utc` (if present) | `finalized_utc` | string |
| trailer `reason` (if present) | `finalized_reason` | string |

---

## 3. Channel → dataset

### 3.1 Path construction

Split the channel name on the namespace separator and create the
intermediate HDF5 groups. Example — channel `Motor.Block.Temperatur`
with separator `.`:

- group `/Motor`
- group `/Motor/Block`
- dataset `/Motor/Block/Temperatur`

Set `H5Pset_create_intermediate_group(lcpl, 1)` on the Link Creation
Property List so HDF5 creates missing intermediate groups automatically.

### 3.2 Dataset shape

| Channel type | HDF5 dataset |
|---|---|
| `scalar` | 1-D dataset, compound `{ int64 timestamp_ns; T value; }` |
| `vector` | 2-D dataset `[N × vector_length]` + parallel `..._timestamps` dataset |
| `matrix` | 3-D dataset `[N × rows × cols]` + parallel `..._timestamps` dataset |

The compound layout for scalar channels (one timestamp + one value per
row) is self-describing and needs no external state — it is the default
and recommended strategy.

### 3.3 Timestamp conversion

The data model exposes per-sample timestamps as a `TDateTime` (days
since 1899-12-30). HDF5 stores `int64` nanoseconds since the Unix epoch:

```
ns = Round((dt - UnixEpochAsTDateTime) * SecsPerDay * 1.0e9)
```

with `UnixEpochAsTDateTime = EncodeDate(1970, 1, 1) = 25569.0`,
`SecsPerDay = 86400`, so the day→ns factor is `86400 * 1.0e9 = 8.64e13`.

---

## 4. Channel metadata → dataset attributes

| OSF channel field | HDF5 attribute | Type |
|---|---|---|
| `index` | `osf_channel_index` | u16 |
| `channeltype` | `osf_channel_type` | string |
| `datatype` | `osf_datatype` | string |
| `timeincrement` | `sample_period_ns` | i64 (0 = timestamped) |
| `physicalunit` | `units` | string (CF convention) |
| `physicaldimension` | `physical_dimension` | string |
| `displayname` | `long_name` | string (CF convention) |
| `comment` | `comment` | string |
| `reference` | `reference_uuid` | string |
| `mimetype` | `mime_type` | string |
| derived from `timeincrement` | `osf_was_equidistant` | bool |

`osf_was_equidistant` records whether the source timestamps were
equidistant (reconstructed from a start time + sample rate) or genuinely
timestamped, so a later HDF5 → OSF conversion can tell the two apart.

---

## 5. Data-type mapping

Always little-endian on the storage side.

| OSF datatype | HDF5 storage type | HDF5 memory type |
|---|---|---|
| `bool`, `uint8` | `H5T_STD_U8LE` | `H5T_NATIVE_UINT8` |
| `int8` | `H5T_STD_I8LE` | `H5T_NATIVE_INT8` |
| `int16` | `H5T_STD_I16LE` | `H5T_NATIVE_INT16` |
| `uint16` | `H5T_STD_U16LE` | `H5T_NATIVE_UINT16` |
| `int32` | `H5T_STD_I32LE` | `H5T_NATIVE_INT32` |
| `uint32` | `H5T_STD_U32LE` | `H5T_NATIVE_UINT32` |
| `int64` | `H5T_STD_I64LE` | `H5T_NATIVE_INT64` |
| `uint64` | `H5T_STD_U64LE` | `H5T_NATIVE_UINT64` |
| `float` | `H5T_IEEE_F32LE` | `H5T_NATIVE_FLOAT` |
| `double` | `H5T_IEEE_F64LE` | `H5T_NATIVE_DOUBLE` |
| `string` | `H5Tcopy(H5T_C_S1)` + `H5Tset_size(H5T_VARIABLE)` + `H5Tset_cset(UTF8)` | same |
| `binary` | `H5T_OPAQUE` with `H5Tset_tag(mimetype)` | same |
| `gpslocation` | compound `{ double lat; double lon; double alt; }` | same |

In HDF5, all these pre-defined type identifiers (`H5T_STD_*`,
`H5T_IEEE_*`, `H5T_NATIVE_*`) are **run-time `_g` variables exported by
the DLL**, not compile-time constants — see `WISSENSBASIS.md`.

---

## 6. Dataset creation property list (DCPL)

Every time-series dataset is created chunked, with shuffle and deflate:

```
dcpl       := H5Pcreate(H5P_DATASET_CREATE)
chunk[0]   := ChunkSize            // configurable, default 8192
H5Pset_chunk(dcpl, 1, @chunk[0])   // MANDATORY for an unlimited dimension
H5Pset_shuffle(dcpl)               // optional; improves float compression
H5Pset_deflate(dcpl, DeflateLevel) // configurable, default 4
```

The dataspace is created with `H5S_UNLIMITED` as the maximum dimension
so samples can be appended:

```
dims[0]    := 0
maxdims[0] := H5S_UNLIMITED
space      := H5Screate_simple(1, @dims[0], @maxdims[0])
```

File Access Property List: `H5Pset_libver_bounds(fapl, H5F_LIBVER_V110,
H5F_LIBVER_LATEST)` — modern features, broad reader compatibility.

---

## 7. Per-channel write sequence

The OSF data model has all samples already in memory, so no streaming
from a block stream is needed. The exporter still writes in batches
(default 8192 samples) so the compound staging buffer never has to hold
every sample of a channel at once:

```
for each channel in active channels:
    create dataset, 0 samples, UNLIMITED max
    for batchStart := 0 to sampleCount-1 step chunkSize:
        n   := min(chunkSize, sampleCount - batchStart)
        buf := array[0..n-1] of compound record
        for i := 0 to n-1:
            buf[i].timestamp_ns := TDateTimeToUnixNs(channel.TimestampUtcAt(batchStart+i))
            buf[i].value        := channel.<typed value accessor>(batchStart+i)
        H5Dset_extent(dset, [batchStart + n])
        fileSpace := H5Dget_space(dset)
        H5Sselect_hyperslab(fileSpace, H5S_SELECT_SET, [batchStart], nil, [n], nil)
        memSpace  := H5Screate_simple(1, [n], nil)
        H5Dwrite(dset, compoundType, memSpace, fileSpace, H5P_DEFAULT, @buf[0])
        H5Sclose(memSpace); H5Sclose(fileSpace)
```

### 7.1 Compound record (Delphi side)

The staging record **must be `packed`**, otherwise the field offsets
will not match the `H5Tinsert` offsets and the data is silently
corrupted:

```pascal
type
  TSampleRecDouble = packed record
    TimestampNs: Int64;   // offset 0
    Value:       Double;  // offset 8
  end;                    // SizeOf = 16
```

```
ct := H5Tcreate(H5T_COMPOUND, SizeOf(TSampleRecDouble))   // 16
H5Tinsert(ct, 'timestamp_ns', 0, H5T_STD_I64LE)
H5Tinsert(ct, 'value',        8, H5T_IEEE_F64LE)
```

Analogous `packed record` types exist for every other value datatype.

---

## 8. Block / segment handling

OSF allows multiple `bcStartData` segments per channel (drift
correction, triggered restart). For HDF5:

1. **Concatenate** segments into one flat dataset with explicit
   per-sample timestamps — the recommended default. The data model
   already presents the channel as a single flat sample list, so this
   needs no extra work.
2. If segment boundaries must be preserved, add a small companion
   dataset `<channel>__segments` with `[start_index, start_ns, rate_hz]`
   per segment.

---

## 9. Loss analysis

**Transferred without loss:**

- All sample values (bit-exact)
- All timestamps (`int64` ns)
- All metablock fields (as attributes)
- Trailer statistics
- File-info including geolocation

**Preserved only by convention:**

- Segment boundaries from `bcStartData` changes (lost on plain
  concatenation unless a `__segments` dataset is written)
- The equidistant-vs-timestamped distinction — recoverable via the
  `osf_was_equidistant` attribute

**Lost (uncritical):**

- OSF block-internal sample counts
- Control-byte bits
- `sizeoflengthvalue`

Carrying the OSF-specific fields as `osf_*` attributes keeps the logical
information complete and makes a theoretical HDF5 → OSF round-trip
possible.
